/***** AudioNode.h *****/

#ifndef __AUDIO_NODE__
#define __AUDIO_NODE__

#include <Bela.h>
#include <vector>
#include <stdlib.h>
#include <math.h>

#define u64 uint64_t

using namespace std;

class AudioNode;

class AudioConnection
{
public:
	AudioConnection() : _fromNode(NULL), _fromChannel(0), _toChannel(0) {}
	AudioConnection(AudioNode* fromNode, u64 fromChannel, u64 toChannel) : _fromNode(fromNode), _fromChannel(fromChannel), _toChannel(toChannel) {}
	~AudioConnection() {}
	AudioConnection(const AudioConnection& ac) { _fromNode = ac._fromNode; _fromChannel = ac._fromChannel; _toChannel = ac._toChannel; } 
	
	AudioNode* fromNode() { return _fromNode; }
	inline const u64& fromChannel() { return _fromChannel; }
	inline const u64& toChannel() { return _toChannel; }
	
protected:	
	AudioNode* _fromNode;
	u64 _fromChannel;
	u64 _toChannel;
};


class AudioNode
{
public:
	AudioNode() : _inputBuffer(NULL), _outputBuffer(NULL), _inputChannels(0), _outputChannels(0), _inputConnections(), _lastFrameProcessed(0) {}
	AudioNode(u64 inChannels, u64 outChannels);
	virtual ~AudioNode() { free(_inputBuffer); free(_outputBuffer); }
	AudioNode(const AudioNode&);
		
	inline u64 getNumInputChannels() const { return _inputChannels; }
	inline u64 getNumOutputChannels() const { return _outputChannels; }
	virtual void setNumInputChannels(const u64 numChannels);
	virtual void setNumOutputChannels(const u64 numChannels);
	void renderGraph(u64 frame, void* clientData);
	virtual void processFrame(u64 frame, void* clientData);
		//{ for (u64 i=0; i < _outputChannels; i++) _outputBuffer[i]=0.0f;  }
	inline float outputSample(u64 channel) { return _outputBuffer[channel]; }
	int indexOfConnection(const u64 channel);
	AudioConnection* whatIsConnectedTo(const u64 channel);
	void receiveConnectionFrom(AudioNode* fromNode, u64 fromChannel, u64 toChannel);
	
protected:
	float* _inputBuffer;
	float* _outputBuffer;
	u64 _inputChannels;
	u64 _outputChannels;
	vector<AudioConnection*> _inputConnections;
	u64 _lastFrameProcessed;
};


/*
class TimeDependentNode : public AudioNode
{
public:
	
};
*/


class SineGenerator : public AudioNode
{
public:
	SineGenerator() : AudioNode(2,1), _phase(0.0), _sampleRate(44100.0) {}
	virtual ~SineGenerator() {}
	SineGenerator(const SineGenerator& sg) : AudioNode(sg), _phase(sg._phase), _sampleRate(sg._sampleRate) {}
	
	virtual void processFrame(u64 frame, void* clientData);
	
private:
	double _phase;
	double _sampleRate;
};


class ConstantGenerator : public AudioNode
{
public:
	ConstantGenerator(float val) : AudioNode(0,1), value(val) {}
	virtual ~ConstantGenerator() {}
	ConstantGenerator(const ConstantGenerator& cg) : AudioNode(cg) { value = cg.value; } 
	
	virtual void processFrame(u64 frame, void *clientData);
	float value;
	
};


class ADSREnvelopeGenerator : public AudioNode{
	ADSREnvelopeGenerator(u64 a, u64 d, float s, u64 r, u64 st, u64 dur);
	virtual ~ADSREnvelopeGenerator() {}
	ADSREnvelopeGenerator(const ADSREnvelopeGenerator& adsr);
	
	virtual void processFrame(u64 frame, void *clientData);
	u64 attack;
	u64 decay;
	float sustain;
	u64 release;

	u64 start_time;
	u64 duration;

};


class StereoMixer : public AudioNode
{
public:
	StereoMixer();
	virtual ~StereoMixer();
	StereoMixer(const StereoMixer& sm);
	
	virtual void processFrame(u64 frame, void *clientData);
	virtual void setNumInputChannels(const u64 numChannels);
	void setPan(u64 channel,float value);

private:	
	
	float _c;
	float* _pan;
	float* _a;
	float* _b;
};


class Multiplier : public AudioNode
{
public:
	Multiplier() : AudioNode(2,1) {}
	Multiplier(u64 numInputChannels) : AudioNode(numInputChannels,1) {}
	virtual ~Multiplier() {}
	Multiplier(const Multiplier& mp) : AudioNode(mp) {}
	
	virtual void processFrame(u64 frame, void* clientData);
};


class Adder : public AudioNode
{
public:
	Adder() : AudioNode(2,1) {}
	Adder(u64 numInputChannels) : AudioNode(numInputChannels,1) {}
	virtual ~Adder() {}
	Adder(const Adder& ad) : AudioNode(ad) {}
	
	virtual void processFrame(u64 frame, void* clientData);
};


class Splitter : public AudioNode
{
public:
	Splitter() : AudioNode(1,2) {}
	Splitter(u64 numOutputChannels) : AudioNode(1,numOutputChannels) {}
	virtual ~Splitter() {}
	Splitter(const Splitter& sp) : AudioNode(sp) {}
	
	virtual void processFrame(u64 frame, void* clientData);
};



#endif
