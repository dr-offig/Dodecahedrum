/***** AudioNode.h *****/

#ifndef __AUDIO_NODE__
#define __AUDIO_NODE__

#include <Bela.h>
#include <vector>
#include <stdlib.h>

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
	AudioNode() : _inputBuffer(NULL), _outputBuffer(NULL), _inputChannels(0), _outputChannels(0) {}
	AudioNode(u64 inChannels, u64 outChannels);
	virtual ~AudioNode() { free(_inputBuffer); free(_outputBuffer); }
	AudioNode(const AudioNode&);
		
	inline u64 getNumInputChannels() const { return _inputChannels; }
	inline u64 getNumOutputChannels() const { return _outputChannels; }
	void setNumInputChannels(const u64 numChannels);
	void setNumOutputChannels(const u64 numChannels);
	void renderGraph(u64 frame, float* clientData);
	virtual void processFrame(u64 frame, float* clientData);
		//{ for (u64 i=0; i < _outputChannels; i++) _outputBuffer[i]=0.0f;  }
	inline float outputSample(u64 channel) { return _outputBuffer[channel]; }
	vector<AudioConnection>::iterator whatIsConnectedTo(const u64 channel);
	void receiveConnectionFrom(AudioNode* fromNode, u64 fromChannel, u64 toChannel);
	
protected:
	float* _inputBuffer;
	float* _outputBuffer;
	u64 _inputChannels;
	u64 _outputChannels;
	vector<AudioConnection> _inputConnections;
	u64 _lastFrameProcessed;
};


class SineGenerator : public AudioNode
{
public:
	SineGenerator() : AudioNode(2,1), _phase(0.0), _sampleRate(44100.0) {}
	virtual ~SineGenerator() {}
	SineGenerator(const SineGenerator& sg) : AudioNode(sg), _phase(sg._phase), _sampleRate(sg._sampleRate) {}
	
	virtual void processFrame(u64 frame, float* clientData);
	
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
	
	virtual void processFrame(u64 frame, float *clientData);
	float value;
	
};

#endif
