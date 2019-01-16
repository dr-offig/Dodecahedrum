/***** AudioNode.cpp *****/
#include "AudioNode.h"
#include <math.h>

#define PI 3.1415926535
#define TWOPI 6.28318531

AudioNode::AudioNode(u64 inChannels, u64 outChannels)
{
	setNumInputChannels(inChannels);
	setNumOutputChannels(outChannels);
}


AudioNode::AudioNode(const AudioNode& node)
{
	setNumInputChannels(node.getNumInputChannels());
	setNumOutputChannels(node.getNumOutputChannels());
}

void AudioNode::renderGraph(u64 frame, float* clientData)
{
	if (_lastFrameProcessed == frame)
		return;
	
	AudioNode* lnd = NULL;
	vector<AudioConnection>::iterator pConn;
	for (pConn=_inputConnections.begin(); pConn != _inputConnections.end(); pConn++) {
		AudioNode* nd = pConn->fromNode();
		
		// cascade processing up chain if needed
		if (nd != lnd) {
			nd->renderGraph(frame,clientData);
			lnd = nd;
		}
		const u64& fromChan = pConn->fromChannel();
		const u64& toChan = pConn->toChannel();
		_inputBuffer[toChan] = nd->outputSample(fromChan);
		
	}

	processFrame(frame,clientData);
	_lastFrameProcessed = frame;

	return;
}


void AudioNode::processFrame(u64 frame, float* clientData)
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


vector<AudioConnection>::iterator AudioNode::whatIsConnectedTo(const u64 channel)
{
	vector<AudioConnection>::iterator pConn;
	for (pConn=_inputConnections.begin(); pConn != _inputConnections.end(); pConn++) {
		const u64& toChan = pConn->toChannel();
		if (toChan == channel)
			return pConn;
	}
	return _inputConnections.end();	
}


void AudioNode::receiveConnectionFrom(AudioNode* upstream, u64 fromChan, u64 toChan)
{
	// automatically disconnect existing connection into that port
	vector<AudioConnection>::iterator existingConnection = whatIsConnectedTo(toChan);
	_inputConnections.erase(existingConnection);
	
	AudioConnection newConnection(upstream, fromChan, toChan);
	return;
}


void SineGenerator::processFrame(u64 frame, float* clientData)
{
	float& freq = _inputBuffer[0];
	float& amp = _inputBuffer[1];
	
	_phase += double(freq) / _sampleRate;
	_outputBuffer[0] = amp * sin(TWOPI * _phase);
	return;
}


void ConstantGenerator::processFrame(u64 frame, float* clientData)
{
	_outputBuffer[0] = value;
	return;	
}


