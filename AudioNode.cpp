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
	_outputBuffer[0] = fabs(amp) * float(sin(TWOPI * _phase));
	return;
}


void SawGenerator::processFrame(u64 frame, void* clientData)
{
	float& freq = _inputBuffer[0];
	float& amp = _inputBuffer[1];
	
	_phase += double(freq) / _sampleRate;
	_outputBuffer[0] = fabs(amp) * float(_phase);
	return;
}



void ConstantGenerator::processFrame(u64 frame, void* clientData)
{
	_outputBuffer[0] = value;
	return;	
}


ADSREnvelopeGenerator::ADSREnvelopeGenerator(u64 a, u64 d, float s, u64 r, u64 st, u64 dur, float amp)
	: AudioNode(0,1), attack(a), decay(d), sustain(s), release(r) , start_time(st), duration(dur), amplitude(amp),
		_crossFading(false), _crossFadeDuration(100), _crossFadeTime(0), _holdValue(0.0f) 
	{}


ADSREnvelopeGenerator::ADSREnvelopeGenerator(const ADSREnvelopeGenerator& adsr)
	: AudioNode(adsr), attack(adsr.attack), decay(adsr.decay), sustain(adsr.sustain), release(adsr.release), 
		start_time(adsr.start_time), duration(adsr.duration), amplitude(adsr.amplitude),
		_crossFading(false), _crossFadeDuration(adsr._crossFadeDuration), _crossFadeTime(0), _holdValue(0.0f) 
	{}


bool ADSREnvelopeGenerator::active(u64 frame)
{
	return (start_time <= frame && frame < start_time + duration + release);	
}


void ADSREnvelopeGenerator::processFrame(u64 frame, void* clientData)
{
	float val = 0.0f;
	if (active(frame)) {
		u64 elapsed = frame - start_time;
		if (elapsed < attack)
		{
			val = float(elapsed) / float(attack);
		} else if (elapsed < attack + decay) {
			float t = float(elapsed - attack) / float(decay);
			val = t*sustain + (1.0f - t);
		} else if (elapsed < duration) {
			val = sustain;
		} else if (elapsed < duration + release) {
			float t = float(elapsed - duration) / float(release);
			val = (1.0f - t)*sustain;			
		} else {
			val = 0.0f;
		}
	}
	val *= amplitude;
		
	if (_crossFading) {
		float t = float(_crossFadeTime) / float(_crossFadeDuration);
		val = val * t + _holdValue * (1.0f - t);
		if (++_crossFadeTime > _crossFadeDuration) {
			_crossFading = false;
		}
	}
	
	_outputBuffer[0] = val;
}


void ADSREnvelopeGenerator::retrigger(u64 a, u64 d, float s, u64 r, u64 st, u64 dur, float amp) {
	_crossFading = true;
	_crossFadeTime = 0;
	_holdValue = _outputBuffer[0];
	attack = a; decay = d; sustain = s; release = r; start_time = st; duration = dur; amplitude = amp;
}


void ADSREnvelopeGenerator::retrigger(u64 st, float amp) {
	_crossFading = true;
	_crossFadeTime = 0;
	_holdValue = _outputBuffer[0];
	start_time = st;
	amplitude = amp;
}


void ADSREnvelopeGenerator::retrigger(u64 st) {
	_crossFading = true;
	_crossFadeTime = 0;
	_holdValue = _outputBuffer[0];
	start_time = st;
}




/************* Effects ************/

void DelayLine::processFrame(u64 frame, void* clientData)
{
	_pipeline.shift(_inputBuffer[0],_outputBuffer);
}



FIRFilter::FIRFilter(unsigned n) : AudioNode(1,1), _pipeline(n, 0.0f), _order(n), _sampleRate(44100.0)
{
	_filterCoefficients = (float *)malloc(sizeof(float) * n);
	_filterCoefficients[0] = 1.0f;
	for (unsigned j=1; j<n; j++)
		_filterCoefficients[j] = 0.0f;
}


FIRFilter::~FIRFilter() {
	free(_filterCoefficients);
}


FIRFilter::FIRFilter(const FIRFilter& fir) : AudioNode(fir), _pipeline(fir._pipeline), _order(fir._order), _sampleRate(fir._sampleRate)
{
	_filterCoefficients = (float *)malloc(sizeof(float) * _order);
	for (unsigned j = 0; j < _order; j++)
		_filterCoefficients[j] = fir._filterCoefficients[j];
}


void FIRFilter::processFrame(u64 frame, void* clientData)
{
	_outputBuffer[0] = 0.0f;
	for (unsigned j = 0; j < _order; j++)
		_outputBuffer[0] += _pipeline[j] * _filterCoefficients[j];
		
	_pipeline.add(_inputBuffer[0]);
}


void FIRFilter::setTap(unsigned t, float h)
{
	if (t < _order)
		_filterCoefficients[t] = h;
	
	printf("Set filter tap %d to %1.2f\t",t,h);
	this->print();
}


void FIRFilter::print()
{
	printf("[ ");
	for (unsigned j = 0; j < _order; j++)
		printf("%1.2f ", _filterCoefficients[j]);
	printf("]\n");
}

/*
import numpy as np
 
fc = 0.1  # Cutoff frequency as a fraction of the sampling rate (in (0, 0.5)).
b = 0.08  # Transition band, as a fraction of the sampling rate (in (0, 0.5)).
N = int(np.ceil((4 / b)))
if not N % 2: N += 1  # Make sure that N is odd.
n = np.arange(N)
 
# Compute sinc filter.
h = np.sinc(2 * fc * (n - (N - 1) / 2.))
 
# Compute Blackman window.
w = 0.42 - 0.5 * np.cos(2 * np.pi * n / (N - 1)) + \
    0.08 * np.cos(4 * np.pi * n / (N - 1))
 
# Multiply sinc filter with window.
h = h * w
 
# Normalize to get unity gain.
h = h / np.sum(h)
*/


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


StereoMixer::StereoMixer(u64 numInputChannels) : AudioNode(numInputChannels,2), _c(sqrt(2.0)/2.0), _pan(NULL), _a(NULL), _b(NULL)
{ 
	// This is a bit of a hack, needed since the parent class has 
	// initialised some input channels, and we want to call the overloaded
	// initialisation assuming 0 pre-existing channels
	_inputChannels = 0;
	
	// Now call the overloaded initialiser
	setNumInputChannels(numInputChannels);
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
	_outputBuffer[0] = 0.0f;
	_outputBuffer[1] = 0.0f;
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


void Affine::processFrame(u64 frame, void* clientData)
{
	_outputBuffer[0] = _inputBuffer[0] + _inputBuffer[1] * _inputBuffer[2];
}


void Vibrator::processFrame(u64 frame, void* clientData)
{
	_outputBuffer[0] = _inputBuffer[0] * (1.0f + _inputBuffer[1]);
}



/************* Routing **************/

void Splitter::processFrame(u64 frame, void* clientData)
{
	for (u64 j=0; j < _outputChannels; j++) {
		_outputBuffer[j] = _inputBuffer[0];
	}
}



/************ Audio Input ************/

void ExternalAudioSource::updateBufferFromSource(float* inData, u64 inChans)
{
	for (u64 j=0; j<inChans; j++) {
		_outputBuffer[j] = inData[j];
	}
}


void SensorInput::updateBufferFromSource(float* inData, u64 inChans)
{
	for (u64 j=0; j<inChans; j++) {
		_outputBuffer[j] = inData[j];
	}
}



