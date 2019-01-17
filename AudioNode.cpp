/***** AudioNode.cpp *****/
#include "AudioNode.h"
#include <math.h>
#include <algorithm>

#define PI 3.1415926535
#define TWOPI 6.28318531

AudioNode::AudioNode(u64 inChannels, u64 outChannels) : _inputConnections(), _lastFrameProcessed(0)
{
	setNumInputChannels(inChannels);
	setNumOutputChannels(outChannels);
}


AudioNode::AudioNode(const AudioNode& node) : _inputConnections(node._inputConnections), _lastFrameProcessed(node._lastFrameProcessed) 
{
	setNumInputChannels(node.getNumInputChannels());
	setNumOutputChannels(node.getNumOutputChannels());
}

void AudioNode::renderGraph(u64 frame, void* clientData)
{
	if (_lastFrameProcessed == frame)
		return;
	
	AudioNode* lnd = NULL;
	vector<AudioConnection*>::iterator j;
	for (j=_inputConnections.begin(); j != _inputConnections.end(); ++j) {
		AudioNode* nd = (*j)->fromNode();
		
		// cascade processing up chain if needed
		if (nd != lnd) {
			nd->renderGraph(frame,clientData);
			lnd = nd;
		}
		const u64& fromChan = (*j)->fromChannel();
		const u64& toChan = (*j)->toChannel();
		_inputBuffer[toChan] = nd->outputSample(fromChan);
		
	}

	processFrame(frame,clientData);
	_lastFrameProcessed = frame;

	return;
}


void AudioNode::processFrame(u64 frame, void* clientData)
{
	for (int i=0; i<_outputChannels; i++)
		_outputBuffer[i] = 0.0f;
		
	return;
}

void AudioNode::setNumOutputChannels(const u64 numChannels)
{
	if (_outputBuffer != NULL) {
		free(_outputBuffer);
		_outputBuffer = NULL;
	}

	_outputBuffer = (float*) calloc(sizeof(float),numChannels);
	_outputChannels = numChannels;
	return;
}


void AudioNode::setNumInputChannels(const u64 numChannels)
{
	if (_inputBuffer != NULL) {
		free(_inputBuffer);
		_inputBuffer = NULL;
	}

	_inputBuffer = (float*) calloc(sizeof(float),numChannels);
	_inputChannels = numChannels;
	return;
}


int AudioNode::indexOfConnection(u64 channel)
{
	for (unsigned int j=0; j < _inputConnections.size(); ++j) {
		AudioConnection* conn = _inputConnections.at(j);
		const u64& toChan = conn->toChannel();
		if (toChan == channel)
			return int(j);
	}
	return -1;	
}


AudioConnection* AudioNode::whatIsConnectedTo(const u64 channel)
{
	int index = indexOfConnection(channel);
	if (index < 0)
		return NULL;
	else
		return _inputConnections.at(index);
}


void AudioNode::receiveConnectionFrom(AudioNode* upstream, u64 fromChan, u64 toChan)
{
	// automatically disconnect existing connection into that port
	int existingConnectionIndex = indexOfConnection(toChan);
	if (existingConnectionIndex >= 0) {
		delete _inputConnections.at(existingConnectionIndex);
		_inputConnections.erase(_inputConnections.begin() + (unsigned)existingConnectionIndex);
	}
	
	AudioConnection* newConnection = new AudioConnection(upstream, fromChan, toChan);
	_inputConnections.push_back(newConnection);
	return;
}



/*********** Generators ***********/

void SineGenerator::processFrame(u64 frame, void* clientData)
{
	float& freq = _inputBuffer[0];
	float& amp = _inputBuffer[1];
	
	_phase += double(freq) / _sampleRate;
	_outputBuffer[0] = amp * sin(TWOPI * _phase);
	return;
}


void ConstantGenerator::processFrame(u64 frame, void* clientData)
{
	_outputBuffer[0] = value;
	return;	
}


ADSREnvelopeGenerator::ADSREnvelopeGenerator(u64 a, u64 d, float s, u64 r, u64 st, u64 dur)
	: AudioNode(0,1), attack(a), decay(d), sustain(s), release(r) , start_time(st), duration(dur) {}


ADSREnvelopeGenerator::ADSREnvelopeGenerator(const ADSREnvelopeGenerator& adsr)
	: AudioNode(adsr), attack(adsr.attack), decay(adsr.decay), 
		sustain(adsr.sustain), release(adsr.release), 
		start_time(adsr.start_time), duration(adsr.duration) {}




/************* Mixers *************/

StereoMixer::StereoMixer() : AudioNode(2,2), _c(sqrt(2.0)/2.0), _pan(NULL), _a(NULL), _b(NULL)
{ 
	// This is a bit of a hack, needed since the parent class has 
	// initialised 2 input channels, and we want to call the overloaded
	// initialisation assuming 0 pre-existing channels
	_inputChannels = 0;
	
	// Now call the overloaded initialiser
	setNumInputChannels(2);
}


StereoMixer::StereoMixer(const StereoMixer& sm) : AudioNode(sm), _c(sqrt(2.0)/2.0), _pan(NULL), _a(NULL), _b(NULL)
{
	_pan = (float *)malloc(_inputChannels * sizeof(*_pan));
	_a = (float *)malloc(_inputChannels * sizeof(*_b));
	_b = (float *)malloc(_inputChannels * sizeof(*_b));
	for (u64 j = 0; j < _inputChannels; j++) {
		_pan[j] = sm._pan[j];
		_a[j] = sm._a[j];
		_b[j] = sm._b[j];
	}
}


StereoMixer::~StereoMixer()
{
	free(_pan);
	free(_a);
	free(_b);
}

void StereoMixer::setNumInputChannels(u64 numChannels)
{
	u64 oldNumInputChannels = _inputChannels;
	AudioNode::setNumInputChannels(numChannels);
	_pan = (float *)realloc(_pan,numChannels * sizeof(*_pan));
	_a = (float *)realloc(_a,numChannels * sizeof(*_a));
	_b = (float *)realloc(_b,numChannels * sizeof(*_b));
	for (u64 j=oldNumInputChannels; j<numChannels; j++) {
		setPan(j, (j % 2 == 0) ? -1.0f : 1.0f);
	}
	
}


void StereoMixer::setPan(u64 channel, float value)
{
	_pan[channel] = value;
	_a[channel] = _c * (cos(PI * value) - sin(PI * value));
	_b[channel] = _c * (cos(PI * value) + sin(PI * value));
}


void StereoMixer::processFrame(u64 frame, void* clientData)
{
	for (u64 j = 0; j < _inputChannels; j++) {
		_outputBuffer[0] += _inputBuffer[j] * _a[j];
		_outputBuffer[1] += _inputBuffer[j] * _b[j];
	}
}



/************* Arithmetic ************/

void Multiplier::processFrame(u64 frame, void* clientData)
{
	
	float val = 1.0f;	
	for (u64 j=0; j < _inputChannels; j++) {
		val *= _inputBuffer[j];
	}
	_outputBuffer[0] = val;
}


void Adder::processFrame(u64 frame, void* clientData)
{
	
	float val = 0.0f;	
	for (u64 j=0; j < _inputChannels; j++) {
		val += _inputBuffer[j];
	}
	_outputBuffer[0] = val;
}



/************* Routing **************/

void Splitter::processFrame(u64 frame, void* clientData)
{
	for (u64 j=0; j < _outputChannels; j++) {
		_outputBuffer[j] = _inputBuffer[0];
	}
}