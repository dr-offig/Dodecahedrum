#include "signal.h"
#include <OSCServer.h>
#include <OSCClient.h>
//#include <Scope.h>
#include <algorithm>
#include <string>
#include <deque>
//#include <list>
//#include <map>
#include <utility>
#include "AudioNode.h"
#include "Tonality.h"

///// capacitive touch stuff///
#include "I2C_MPR121.h"
// How many pins there are
#define NUM_TOUCH_PINS 5
#define NUM_SOLENOIDS 5
#define NUM_ANALOG_SENSORS 3

using namespace std;

///// capacitive touch stuff///
// Change this to change how often the MPR121 is read (in Hz)
//int readInterval = 1000;
// Change this threshold to set the minimum amount of touch
int threshold = 40;

// This array holds the continuous sensor values
int capacitiveSensorValue[NUM_TOUCH_PINS];
float analogSensorValues[NUM_ANALOG_SENSORS];
I2C_MPR121 mpr121;			// Object to handle MPR121 sensing
AuxiliaryTask i2cTask;		// Auxiliary task to read I2C
//int readCount = 0;			// How long until we read again...
//int readIntervalSamples = 0; // How many samples between reads
void readMPR121(void*);
///////////////////////////////


///// contact mic and accelerometer and triggering //////
float capacitiveIn[NUM_TOUCH_PINS];
float capacitiveSmoothed[NUM_TOUCH_PINS];
float capacitiveDetail[NUM_TOUCH_PINS];
float capacitiveSmoothedDetail[NUM_TOUCH_PINS];
float capacitiveRS[NUM_TOUCH_PINS] = { 0.05f, 0.05f, 0.05f, 0.05f, 0.05f };
float capacitiveRD[NUM_TOUCH_PINS] = { 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
int capacitiveKerbang[NUM_TOUCH_PINS] = { 0, 0, 0, 0, 0 };
TimedSchottky capacitiveTrigger[NUM_TOUCH_PINS] = { TimedSchottky(0.05f, 0.000f, 1024), TimedSchottky(0.05f, 0.000f, 1024), TimedSchottky(0.05f, 0.000f, 1024),
													TimedSchottky(0.05f, 0.000f, 1024), TimedSchottky(0.05f, 0.000f, 1024) };

float accIn[NUM_TOUCH_PINS];
// float accSmoothed[NUM_TOUCH_PINS];
// float accDetail[NUM_TOUCH_PINS];
// float accSmoothedDetail[NUM_TOUCH_PINS];
// float accRS[NUM_TOUCH_PINS] = { 0.05f, 0.05f, 0.05f, 0.05f, 0.05f };
// float accRD[NUM_TOUCH_PINS] = { 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };

int solenoidForChannel[8] = { 0, -1, 1, 2, 3, 4, -1, -1};
//----------------------------------------------------//

//--- OSC communication ----//
AuxiliaryTask oscServerTask;
AuxiliaryTask oscClientTask;

OSCServer oscServer;
OSCClient oscClient;
deque<oscpkt::Message> outbox;
//------------------------//
AuxiliaryTask playTask;


//------- Tonality -------//
Scale scale = MajorPentatonic(PitchClass(0));

//Scope scope;
int localPort = 8000;
OneShot hits[NUM_TOUCH_PINS];

// struct transport {
// 	int bar;
// 	int beat;
// 	int tick;
// };

// typedef struct transport transport;

deque<pair<uint64_t,int> > scheduledOnsets[NUM_SOLENOIDS];

int gAudioFramesPerAnalogFrame;

int bar=0; int beat=0; int tick=0;
int B=4; int D=12;  int cc=0;
uint64_t globalTick;
bool tickedOff = false;

float tempo;
int analogFramesPerTick;

void changeTempo(float new_tempo, float sr)
{
	tempo = new_tempo;
	analogFramesPerTick = int(sr * 60.0f / (tempo * float(D)));
}


bool earlier(pair<uint64_t,int> note1, pair<uint64_t,int> note2) { return note1.first < note2.first; } 

void scheduleNote(int bar, int beat, int tick, int panel, int velocity)
{
	uint64_t onset = bar * B * D + beat * D + tick;
	scheduledOnsets[panel].push_back(make_pair(onset,velocity));
	//rt_printf("Pushed note on panel %d for onset %d\n", panel, onset);
	sort(scheduledOnsets[panel].begin(),scheduledOnsets[panel].end(),earlier);
}


void scheduleNotes(int bar, int beat, int tick, string letter, int velocity)
{
	if (letter == "g") {
		scheduleNote(bar, beat, tick, 0, velocity);
		scheduleNote(bar, beat, tick, 1, velocity);
		scheduleNote(bar, beat, tick, 2, velocity);
	} else if (letter == "f") {
		scheduleNote(bar, beat, tick, 0, velocity);
		scheduleNote(bar, beat, tick, 1, velocity);
	} else if (letter == "e") {
		scheduleNote(bar, beat, tick, 4, velocity);
	} else if (letter == "d") {
		scheduleNote(bar, beat, tick, 3, velocity);
	} else if (letter == "c") {
		scheduleNote(bar, beat, tick, 2, velocity);
	} else if (letter == "b") {
		scheduleNote(bar, beat, tick, 1, velocity);
	} else if (letter == "a") {
		scheduleNote(bar, beat, tick, 0, velocity);
	}
	
}



// ------- OSC message parsing -------- //
int parseMessage(oscpkt::Message msg){
    
    //printf("received message to: %s\n", msg.addressPattern().c_str());
    
    int a; int b; int bar; int beat; int tick; int velocity;
    string ipAddress; string letter;
    
    
    if (msg.match("/host").popStr(ipAddress).popInt32(b).isOkNoMoreArgs()){
        //rt_printf("Connecting to %s:%d\n", ipAddress.c_str(), b);
        oscClient.setup(b, ipAddress.c_str());
    
    	
    } else if (msg.match("/noteon").popInt32(a).popInt32(b).isOkNoMoreArgs()) {
    	unsigned int p = a % 3;
    	OneShot& h = hits[p];
    	if (b > 0) {
	    	//h.amplitude = float(b) / 127.0;
	    	h.amplitude = 1.0f;
	    	h.duration = uint64_t(512.0f * float(b) / 127.0f);
    		h.trigger = true;
    	}
    	
    } else if (msg.match("/letter").popInt32(bar).popInt32(beat).popInt32(tick).popStr(letter).popInt32(velocity).isOkNoMoreArgs()) {
    	//rt_printf("Scheduling letter %s at %d %d %d\n", letter.c_str(), bar, beat, tick);
    	scheduleNotes(bar+1, beat, tick, letter, velocity);
    }
     
    return b;
}


void sendLetter(int bar, int beat, int tick, int vel1, int vel2, int vel3)
{
	bool send = true;
	string letter;
	int velocity = 0;
	if (vel1 && vel2 && vel3) {
		letter = "g"; velocity = (vel1 + vel2 + vel3) / 3;
	} else if (vel1 && vel2) {
		letter = "f"; velocity = (vel1 + vel2) / 2;
	} else if (vel2 && vel3) {
		letter = "e"; velocity = (vel2 + vel3) / 2;
	} else if (vel1 && vel3) {
		letter = "d"; velocity = (vel1 + vel3) / 2;
	} else if (vel3) {
		letter = "c"; velocity = vel3;
	} else if (vel2) {
		letter = "b"; velocity = vel2;
	} else if (vel1) {
		letter = "a"; velocity = vel1;
	} else {
		letter = "o";
		send = false;
	}

	if (send) {
		oscpkt::Message msg = oscClient.newMessage.to("/letter").add(bar).add(beat).add(tick).add(letter).add(velocity).end();
		outbox.push_back(msg);
	}

}


// Auxiliary task callbacks
void receiveOSC(void *clientData)
{
	while (oscServer.messageWaiting())
        parseMessage(oscServer.popMessage());
}


void sendOSC(void *clientData)
{
	while (!outbox.empty()) {
		oscClient.sendMessageNow(outbox.front());
		outbox.pop_front();
	}		
}
// -------------------------------------- //



//----- Audio Graph -------//
ExternalAudioSource* audioInNode;
SensorInput* sensors;
ADSREnvelopeGenerator* env[NUM_TOUCH_PINS];
ConstantGenerator* freq[NUM_TOUCH_PINS];
SineGenerator* lfo[NUM_TOUCH_PINS];
Vibrator* vibrator[NUM_TOUCH_PINS];
SawGenerator* cycle[NUM_TOUCH_PINS];
StereoMixer* mixer;
//------------------------//


void playNotes(void *clientData)
{
	for (int i=0; i<3; i++)
	{
		if (!scheduledOnsets[i].empty()) {
			uint64_t nextOnset = scheduledOnsets[i].front().first;
			if (nextOnset <= globalTick) {
				//rt_printf("Playing note on panel %d scheduled for %lld at actual time %lld\n\n", i, nextOnset, globalTick);
				OneShot& h = hits[i];
        		h.amplitude = float(scheduledOnsets[i].front().second) / 127.0;
    			h.trigger = true;
     			scheduledOnsets[i].pop_front();
			}
		}
	}
	
}

// Auxiliary task to read the I2C board
void readMPR121(void*)
{
	for(int i = 0; i < NUM_TOUCH_PINS; i++) {
		capacitiveSensorValue[i] = -(mpr121.filteredData(i) - mpr121.baselineData(i));
		capacitiveSensorValue[i] -= threshold;
		if(capacitiveSensorValue[i] < 0)
			capacitiveSensorValue[i] = 0;
	}

	// You can use this to read binary on/off touch state more easily
	//rt_printf("Touched: %x\n", mpr121.touched());
}




bool setup(BelaContext *context, void *userData)
{
	oscServer.setup(localPort);
	oscClient.setup(localPort, "localhost");
	oscServerTask = Bela_createAuxiliaryTask(receiveOSC, BELA_AUDIO_PRIORITY - 30, "receive-osc", NULL);
	oscClientTask = Bela_createAuxiliaryTask(sendOSC, BELA_AUDIO_PRIORITY - 20, "send-osc", NULL);
	playTask = Bela_createAuxiliaryTask(playNotes, BELA_AUDIO_PRIORITY - 10,"play", NULL);

	//analogSensorValues = (float*)malloc(sizeof(float) * context->analogInChannels);
	audioInNode = new ExternalAudioSource();
	sensors = new SensorInput(context->analogInChannels);
	//delay = new DelayLine(320);
	//joltFactor = new SensorInput();
	//filter = new FIRFilter(8); filter->setTap(0,1.0f); filter->setTap(7,-1.0f);
	
	//for (unsigned k=0;k<16;k++)
	//	filter->setTap(k,0.0625f);
	//filter->receiveConnectionFrom(sensors,0,0);
	mixer = new StereoMixer(NUM_TOUCH_PINS);	
	for (u64 j=0; j<NUM_TOUCH_PINS; j++) { // u64 a, u64 d, float s, u64 r, u64 st, u64 dur, float amplitude
		env[j] = new ADSREnvelopeGenerator(2000, 1000, 0.8f, 5000, 0, 10000, 1.0f);
		cycle[j] = new SawGenerator();
		vibrator[j] = new Vibrator();
		lfo[j] = new SineGenerator(); lfo[j]->setDefaultInput(0,5.0f); lfo[j]->setDefaultInput(1,0.2f); 
		freq[j] = new ConstantGenerator(mtof(scale.pitch(j,6)));
		vibrator[j]->receiveConnectionFrom(freq[j],0,0);
		vibrator[j]->receiveConnectionFrom(lfo[j],0,1);
		
		cycle[j]->receiveConnectionFrom(vibrator[j],0,0);
		cycle[j]->receiveConnectionFrom(env[j],0,1);
		mixer->receiveConnectionFrom(cycle[j],0,j);
		mixer->setPan(j,0.0f);
	}	
	
	gAudioFramesPerAnalogFrame = context->audioFrames / context->analogFrames;
	changeTempo(120.0f, context->analogSampleRate);
	
	if(!mpr121.begin(1, 0x5A)) {
		rt_printf("Error initialising MPR121\n");
		return false;
	}
	
	i2cTask = Bela_createAuxiliaryTask(readMPR121, BELA_AUDIO_PRIORITY - 15, "bela-mpr121");
	//readIntervalSamples = context->audioSampleRate / readInterval;
	
	//scope.setup(3,context->audioSampleRate);
	
	// render(context,userData);
	// render(context,userData);
	//printf("Scale: %d %d %d %d %d %d %d\n", scale.pitch(0,5), scale.pitch(1,5), scale.pitch(2,5), scale.pitch(3,5),
	//										scale.pitch(4,5), scale.pitch(5,5), scale.pitch(6,5));
	
	return true;
}

void render(BelaContext *context, void *userData)
{
	Bela_scheduleAuxiliaryTask(oscServerTask);
	Bela_scheduleAuxiliaryTask(oscClientTask);
	Bela_scheduleAuxiliaryTask(playTask);
	Bela_scheduleAuxiliaryTask(i2cTask);
	
	for (unsigned int frame = 0; frame < context->audioFrames; ++frame)
	{
		uint64_t t = context->audioFramesElapsed + frame;
		//float jolt = 0.0f;
		
		// Capacitive Sensing
		for(int i = 0; i < NUM_TOUCH_PINS; i++) {
			float value = capacitiveSensorValue[i] / 400.f;
			capacitiveIn[i] = value;
					
			// Exponential filter bank (separates DC from AC)
			capacitiveSmoothed[i] = ewma(value, capacitiveSmoothed[i], capacitiveRS[i]);
			capacitiveDetail[i] = value - capacitiveSmoothed[i];

			// Now smooth the AC component
			capacitiveSmoothedDetail[i] = ewma(capacitiveDetail[i], capacitiveSmoothedDetail[i], capacitiveRD[i]);
					
			// Check for sudden loud inputs
			if (capacitiveTrigger[i].update(fabs(capacitiveSmoothedDetail[i]))) {
				
				OneShot& h = hits[i];
    		    h.amplitude = value * 10.0f;
    		    capacitiveKerbang[i] = int(h.amplitude * 127.0);
	    		h.duration = uint64_t(512.0f * 2.0f * value);
	    		h.trigger = true;
	
				
			}
				
							
	
		}
		//scope.log(amps[0],amps[1],amps[2]);
		
		// Analog input channels
		// Analog sample rate is slower than audio sample rate
		if(!(frame % gAudioFramesPerAnalogFrame)) 
		{
			int n = frame / gAudioFramesPerAnalogFrame; cc++;
			for (unsigned int ch = 0; ch < context->analogOutChannels; ch++)
			{
	
				/***** accelerometer is on input channels 0,1,2 ****/
				if (ch < 3) 
				{
					float analogValue = analogRead(context, n, ch);
					
					unsigned int i = ch;
					accIn[i] = lerp(analogValue, 0.30f, 0.49f, -1.0f, 1.0f); 
					
					// accSmoothed[i] = ewma(analogValue, accSmoothed[i], accRS[i]);
					// accDetail[i] = analogValue - accSmoothed[i];

					// // Now smooth the AC component
					// accSmoothedDetail[i] = ewma(accDetail[i], accSmoothedDetail[i], accRD[i]);
					
					//analogSensorValues[i] = analogValue;
					analogSensorValues[i] = accIn[i];
					//jolt += accIn[i] * accIn[i];
					//float gforce = sqrt(analogIn[0]*analogIn[0] + analogIn[1] * analogIn[1] + analogIn[2] * analogIn[2]);
					
				}
				/**************************************************/
				
				
				// Outputs
				
				int j = solenoidForChannel[ch];
				if (j >= 0 && j < NUM_TOUCH_PINS)
				{
					if (hits[j].trigger) {
						hits[j].start = t;
						env[j]->retrigger(t, hits[j].amplitude);
						hits[j].trigger = false;
					}

					analogWrite(context, n, ch, hits[j].value(t));
					//analogWrite(context, n, ch, 0);
				}
				
			}
			
			sensors->updateBufferFromSource(analogSensorValues,NUM_ANALOG_SENSORS);
			//sensors->updateBufferFromSource(analogSensorValues,context->analogInChannels);
			//scope.log(analogSensorValues[0],analogSensorValues[1],analogSensorValues[2]);
			
			//jolt = 0.1f * sqrtf(jolt);
			//joltFactor->updateBufferFromSource(&jolt,1);
		
		}
		
		// Audio input channels
		// float inputData[2];
		// inputData[0] = audioRead(context,frame,0);
		// inputData[1] = audioRead(context,frame,1);
		// audioInNode->updateBufferFromSource(inputData,2);

		// Output
		float left_out=0.0f; float right_out=0.0f;
		
		// Render the audio Graph
		mixer->renderGraph(t,NULL);
		left_out += mixer->outputSample(0);
		right_out += mixer->outputSample(1);
		
		audioWrite(context,frame,0,left_out);
		audioWrite(context,frame,1,right_out);
		
		//scope.log(lfo[0]->outputSample(0),cycle[0]->outputSample(0),mixer->outputSample(0));
	}

	
	// update transport
	if (cc >= analogFramesPerTick) {
		tickedOff = true;
		globalTick++;
		cc = cc % analogFramesPerTick;
		if (++tick >= D) {
			tick = tick % D;
			if (++beat >= B) {
				beat = beat % B;
				++bar;
			}
		}
	}
	
	
	// send message for this tick
	if (tickedOff) 
	{
		sendLetter(bar, beat, tick, capacitiveKerbang[0], capacitiveKerbang[1], capacitiveKerbang[2]);
		capacitiveKerbang[0] = 0; capacitiveKerbang[1] = 0; capacitiveKerbang[2] = 0;
		tickedOff = false;
	}


}

void cleanup(BelaContext *context, void *userData)
{
	delete(mixer);
	delete(audioInNode);
	delete(sensors);
	for (unsigned j=0; j<NUM_TOUCH_PINS; j++) {
		delete(freq[j]);
		delete(cycle[j]);
		delete(env[j]);
		delete(lfo[j]);
		delete(vibrator[j]);
	}
	
	//free(analogSensorValues);

}


