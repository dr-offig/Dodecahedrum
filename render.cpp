#include "signal.h"
#include <OSCServer.h>
#include <OSCClient.h>
#include <Scope.h>
#include <algorithm>
#include <string>
#include <deque>
#include <list>
#include <map>
#include <utility>
#include "AudioNode.h"
#include "Tonality.h"

///// capacitive touch stuff///
#include "I2C_MPR121.h"
// How many pins there are
#define NUM_TOUCH_PINS 3
// Define this to print data to terminal
#undef DEBUG_MPR121
//////////////////////////////

#define NUM_ANALOG_SENSORS 3

#define ENV_INFO_BUF_SIZE 16

using namespace std;

///// capacitive touch stuff///
// Change this to change how often the MPR121 is read (in Hz)
//int readInterval = 1000;
// Change this threshold to set the minimum amount of touch
int threshold = 40;
// This array holds the continuous sensor values
int capacitiveSensorValue[NUM_TOUCH_PINS];
float *analogSensorValues;
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
float capacitiveRS[NUM_TOUCH_PINS] = { 0.05f, 0.05f, 0.05f };
float capacitiveRD[NUM_TOUCH_PINS] = { 0.25f, 0.25f, 0.25f };
bool capacitiveKerbang[NUM_TOUCH_PINS] = { false, false, false };
TimedSchottky capacitiveTrigger[NUM_TOUCH_PINS] = { TimedSchottky(0.05f, 0.000f, 1024), TimedSchottky(0.05f, 0.000f, 1024), TimedSchottky(0.05f, 0.000f, 1024) };

float accIn[NUM_TOUCH_PINS];
float accSmoothed[NUM_TOUCH_PINS];
float accDetail[NUM_TOUCH_PINS];
float accSmoothedDetail[NUM_TOUCH_PINS];
float accRS[NUM_TOUCH_PINS] = { 0.05f, 0.05f, 0.05f };
float accRD[NUM_TOUCH_PINS] = { 0.25f, 0.25f, 0.25f };

int solenoidForChannel[8] = { 0, -1, 1, 2, -1, -1, -1, -1};
//----------------------------------------------------//

//--- OSC communication ----//
AuxiliaryTask oscServerTask;
AuxiliaryTask oscClientTask;
AuxiliaryTask playTask;
//AuxiliaryTask createEnvelopesTask;
//AuxiliaryTask cleanupInactiveEnvelopesTask;
OSCServer oscServer;
OSCClient oscClient;
deque<oscpkt::Message> outbox;
//------------------------//




//------- Tonality -------//
Scale scale = MajorPentatonic(PitchClass(0));

Scope scope;
int localPort = 8000;
OneShot hits[3];


/******************** Old Envelope Stuff ***********************
map<uint32_t,Carrier*>cycles;
Carrier* baseFaceCarriers[NUM_TOUCH_PINS];
list<Envelope*>envelopes;
UniqueID id_gen(NUM_TOUCH_PINS);

uint32_t addEnvelope(const ADSR& adsr) {
	ADSR* env = new ADSR(adsr);
	envelopes.push_back(env);
	//printf("Added Envelope. Start: %lld  duration: %lld  id: %lld  carrier: %lld  attack: %lld  decay: %lld  sustain: %1.2f  release: %lld\n",
				//env->start,env->duration,env->id,env->carrier_id,env->attack,env->decay,env->sustain,env->release);
	return envelopes.size();
}

ADSR toBeAdded[ENV_INFO_BUF_SIZE];
uint32_t toBeAddedIndex = 0;
uint32_t markEnvelopeForAdding(ADSR& adsr)
{
	toBeAdded[toBeAddedIndex] = adsr;
	toBeAddedIndex = (++toBeAddedIndex) % ENV_INFO_BUF_SIZE;	
	return toBeAddedIndex;	
}

void addEnvelopes(void *clientData)
{
	for (int i=0; i<ENV_INFO_BUF_SIZE; i++)
	{
		ADSR& adsr = toBeAdded[i];
		if (adsr.id > 0) {
			addEnvelope(adsr);
			adsr.id = 0;
		}
	}
}


void cleanupInactiveEnvelopes(void *clientData)
{
	BelaContext *context = (BelaContext*)clientData;
	uint64_t time = context->audioFramesElapsed; 
	
	list<Envelope*>::iterator it = envelopes.begin();
	//list<Envelope*>::iterator end  = items.end();

	while (it != envelopes.end()) {
    	Envelope* env = *it;

    	if (env->active(time)) {
        	++it;
    	} else {
        	delete(env);
        	it = envelopes.erase(it);
		}
	}
	return;
}
****************************************************************/



// struct transport {
// 	int bar;
// 	int beat;
// 	int tick;
// };

// typedef struct transport transport;

deque<uint64_t> scheduledOnsets[3];

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

void scheduleNote(int bar, int beat, int tick, int panel)
{
	uint64_t onset = bar * B * D + beat * D + tick;
	scheduledOnsets[panel].push_back(onset);
	//rt_printf("Pushed note on panel %d for onset %d\n", panel, onset);
	sort(scheduledOnsets[panel].begin(),scheduledOnsets[panel].end());
}


void scheduleNotes(int bar, int beat, int tick, string letter)
{
	if (letter == "g") {
		scheduleNote(bar, beat, tick, 0);
		scheduleNote(bar, beat, tick, 1);
		scheduleNote(bar, beat, tick, 2);
	} else if (letter == "f") {
		scheduleNote(bar, beat, tick, 0);
		scheduleNote(bar, beat, tick, 1);
	} else if (letter == "e") {
		scheduleNote(bar, beat, tick, 1);
		scheduleNote(bar, beat, tick, 2);
	} else if (letter == "d") {
		scheduleNote(bar, beat, tick, 1);
		scheduleNote(bar, beat, tick, 3);
	} else if (letter == "c") {
		scheduleNote(bar, beat, tick, 2);
	} else if (letter == "b") {
		scheduleNote(bar, beat, tick, 2);
	} else if (letter == "a") {
		scheduleNote(bar, beat, tick, 2);
	}
	
}



// OSC message parsing
int parseMessage(oscpkt::Message msg){
    
    printf("received message to: %s\n", msg.addressPattern().c_str());
    
    int a; int b; int bar; int beat; int tick;
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
    	
    } else if (msg.match("/letter").popInt32(bar).popInt32(beat).popInt32(tick).popStr(letter).isOkNoMoreArgs()) {
    	//rt_printf("Scheduling letter %s at %d %d %d\n", letter.c_str(), bar, beat, tick);
    	scheduleNotes(bar, beat, tick, letter);
    }
     
    return b;
}


void sendLetter(int bar, int beat, int tick, bool panel1, bool panel2, bool panel3)
{
	bool send = true;
	string letter;
	if (panel1 && panel2 && panel3)
		letter = "g";
	else if (panel1 && panel2)
		letter = "f";
	else if (panel2 && panel3)
		letter = "e";
	else if (panel1 && panel3)
		letter = "d";
	else if (panel3)
		letter = "c";
	else if (panel2)
		letter = "b";
	else if (panel1)
		letter = "a";
	else {
		letter = "o";
		send = false;
	}

	if (send) {
		oscpkt::Message msg = oscClient.newMessage.to("/letter").add(bar).add(beat).add(tick).add(letter).end();
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


void playNotes(void *clientData)
{
	for (int i=0; i<3; i++)
	{
		if (!scheduledOnsets[i].empty()) {
			uint64_t nextOnset = scheduledOnsets[i].front();
			if (nextOnset <= globalTick) {
				//rt_printf("Playing note on panel %d scheduled for %lld at actual time %lld\n\n", i, nextOnset, globalTick);
				OneShot& h = hits[i];
        		h.amplitude = 1.0;
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
#ifdef DEBUG_MPR121
		rt_printf("%d ", capacitiveSensorValue[i]);
#endif
	}
#ifdef DEBUG_MPR121
	rt_printf("\n");
#endif
	
	// You can use this to read binary on/off touch state more easily
	//rt_printf("Touched: %x\n", mpr121.touched());
}



//----- Audio Graph -------//
ExternalAudioSource* audioInNode;
SensorInput* sensors;
//SensorInput* joltFactor;
FIRFilter* highpass;
ADSREnvelopeGenerator* env[NUM_TOUCH_PINS];
ConstantGenerator* lfo_freq[NUM_TOUCH_PINS];
ConstantGenerator* lfo_amp[NUM_TOUCH_PINS];
ConstantGenerator* freq[NUM_TOUCH_PINS];
ConstantGenerator* amp[NUM_TOUCH_PINS];
SineGenerator* lfo[NUM_TOUCH_PINS];
Vibrator* vibrator[NUM_TOUCH_PINS];
SineGenerator* cycle[NUM_TOUCH_PINS];
StereoMixer* mixer;
//------------------------//

bool setup(BelaContext *context, void *userData)
{
	oscServer.setup(localPort);
	oscServerTask = Bela_createAuxiliaryTask(receiveOSC, BELA_AUDIO_PRIORITY - 30, "receive-osc", NULL);
	oscClientTask = Bela_createAuxiliaryTask(sendOSC, BELA_AUDIO_PRIORITY - 20, "send-osc", NULL);
	playTask = Bela_createAuxiliaryTask(playNotes, BELA_AUDIO_PRIORITY - 10,"play", NULL);
	//createEnvelopesTask = Bela_createAuxiliaryTask(addEnvelopes, BELA_AUDIO_PRIORITY - 40,"add-envelopes", NULL);
	//cleanupInactiveEnvelopesTask = Bela_createAuxiliaryTask(cleanupInactiveEnvelopes, BELA_AUDIO_PRIORITY - 50,"cleanup-envelopes", context);
	
	// baseFaceCarriers[0] = new SawWave(1,5000.0f,1.0f);
	// baseFaceCarriers[1] = new SawWave(2,7500.0f,1.0f);
	// baseFaceCarriers[2] = new SawWave(3,10000.0f,1.0f);
	// cycles.insert(make_pair(1,baseFaceCarriers[0]));
	// cycles.insert(make_pair(2,baseFaceCarriers[1]));
	// cycles.insert(make_pair(3,baseFaceCarriers[2]));
	
	analogSensorValues = (float*)malloc(sizeof(float) * context->analogInChannels);
	audioInNode = new ExternalAudioSource();
	//affineGain = new ConstantGenerator(1000.0f);
	sensors = new SensorInput(context->analogInChannels);
	//joltFactor = new SensorInput();
	highpass = new FIRFilter(8); highpass->setTap(0,1.0f); highpass->setTap(7,-1.0f);
	mixer = new StereoMixer(NUM_TOUCH_PINS);	
	for (u64 j=0; j<NUM_TOUCH_PINS; j++) { // u64 a, u64 d, float s, u64 r, u64 st, u64 dur, float amplitude
		env[j] = new ADSREnvelopeGenerator(2000, 1000, 0.8f, 10000, 0, 20000, 1.0f);
		cycle[j] = new SineGenerator();
		vibrator[j] = new Vibrator();
		lfo[j] = new SineGenerator();
		freq[j] = new ConstantGenerator(mtof(scale.pitch(j,6)));
		lfo_freq[j] = new ConstantGenerator(10.0f);
		lfo_amp[j] = new ConstantGenerator(0.5f);
		lfo[j]->receiveConnectionFrom(lfo_freq[j],0,0);
		highpass->receiveConnectionFrom(sensors,0,0);
		lfo[j]->receiveConnectionFrom(highpass,0,1);
		
		vibrator[j]->receiveConnectionFrom(freq[j],0,0);
		vibrator[j]->receiveConnectionFrom(lfo[j],0,1);
				
		//cycle[j]->receiveConnectionFrom(freq[j],0,0);
		cycle[j]->receiveConnectionFrom(vibrator[j],0,0);
		cycle[j]->receiveConnectionFrom(env[j],0,1);
		//cycle[j]->receiveConnectionFrom(audioInNode,0,1);
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
	
	scope.setup(3,context->audioSampleRate);
	
	// render(context,userData);
	// render(context,userData);
	printf("Scale: %d %d %d %d %d %d %d\n", scale.pitch(0,5), scale.pitch(1,5), scale.pitch(2,5), scale.pitch(3,5),
											scale.pitch(4,5), scale.pitch(5,5), scale.pitch(6,5));
	
	return true;
}

void render(BelaContext *context, void *userData)
{
	Bela_scheduleAuxiliaryTask(oscServerTask);
	Bela_scheduleAuxiliaryTask(oscClientTask);
	Bela_scheduleAuxiliaryTask(playTask);
	Bela_scheduleAuxiliaryTask(i2cTask);
	// Bela_scheduleAuxiliaryTask(createEnvelopesTask);
	// Bela_scheduleAuxiliaryTask(cleanupInactiveEnvelopesTask);
	
	for (unsigned int frame = 0; frame < context->audioFrames; ++frame)
	{
		uint64_t t = context->audioFramesElapsed + frame;
		//float jolt = 0.0f;
		
		// Capacitive Sensing
		//float amps[NUM_TOUCH_PINS] = {0.0f, 0.0f, 0.0f};
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
				capacitiveKerbang[i] = true;
				OneShot& h = hits[i];
    		    h.amplitude = 1.0f;
    		    
	    		h.duration = uint64_t(512.0f * 2.0f * value);
	    		h.trigger = true;
				//rt_printf("Triggered panel %d\n", i);
				//scope.trigger();
				
				// Also trigger sound output
				// ADSR adsr(t, 15000, 1.0f, 3000, 1000, 0.8f, 10000);
				// adsr.id = id_gen.generate();
				// adsr.carrier_id = i+1;
				// markEnvelopeForAdding(adsr);
				env[i]->retrigger(t, 10.0f * value);
			}
				
							
			/*** Check for touch events ******
			if (trigger[i].update(amplitude)) {
				kerbang[i] = true;
				//printf("Triggered panel %d\n", i);
				OneShot& h = hits[i];
    		    h.amplitude = 1.0f;
	    		h.duration = uint64_t(512.0f * 1.0f * amplitude);
	    		h.trigger = true;
    			//scope.trigger();
			}
			**********************************/		
		}
		//scope.log(amps[0],amps[1],amps[2]);
		
		// Analog input channels
		// Analog sample rate is slower than audio sample rate
		if(!(frame % gAudioFramesPerAnalogFrame)) 
		{
			int n = frame / gAudioFramesPerAnalogFrame; cc++;
			for (unsigned int ch = 0; ch < context->analogOutChannels; ch++)
			{
				/*** Inputs. Contact mics are on channels 3,4,5 ***
				if (3 <= ch && ch < 6) 
				{
					value = analogRead(context, n, ch);

					int i = ch - 3;

					// Exponential filter bank (separates DC from AC)
					smoothedIn[i] = ewma(value, smoothedIn[i], rs[i]);
					detailIn[i] = value - smoothedIn[i];

					// Now smooth the AC component
					smoothedDetail[i] = ewma(detailIn[i], smoothedDetail[i], rd[i]);
					
					// Check for sudden loud inputs
					if (trigger[i].update(fabs(smoothedDetail[i]))) {
						kerbang[i] = true;
						//rt_printf("Triggered panel %d\n", i);
						//scope.trigger();

					}
					
				}
				***************************************************/
				
				
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
				if (j >= 0)
				{
					
					if (hits[j].trigger) {
						hits[j].start = t;
						hits[j].trigger = false;
					}

					analogWrite(context, n, ch, hits[j].value(t));
					//analogWrite(context, n, ch, 0);
				}
				
			}
			
			sensors->updateBufferFromSource(analogSensorValues,context->analogInChannels);
			//scope.log(analogSensorValues[0],analogSensorValues[1],analogSensorValues[2]);
			
			//jolt = 0.1f * sqrtf(jolt);
			//joltFactor->updateBufferFromSource(&jolt,1);
			scope.log(sensors->outputSample(0), highpass->outputSample(0), 0.0f);
		}
		
		// Audio input channels
		float inputData[2];
		inputData[0] = audioRead(context,frame,0);
		inputData[1] = audioRead(context,frame,1);
		audioInNode->updateBufferFromSource(inputData,2);

		// Output
		float left_out=0.0f; float right_out=0.0f;
		
		// list<Envelope*>::iterator it;
		// for (it=envelopes.begin(); it!=envelopes.end(); it++) {
		// 	ADSR* env = (ADSR*) *it;
		// 	if (env->active(t)) {
		// 		Carrier* cycle = cycles[env->carrier_id];
		// 		left_out += env->value(t) * cycle->value(t, cycle->freq * accIn[env->carrier_id-1],1.0f);
		// 		//left_out += env->value(t) * sin(TWOPI * 1000.0f * ((float)t)/44100.0f);
		// 	}
			
		// }
		
		
		
		// Render the audio Graph
		mixer->renderGraph(t,NULL);
		left_out += mixer->outputSample(0);
		right_out += mixer->outputSample(1);
		
		audioWrite(context,frame,0,left_out);
		audioWrite(context,frame,1,right_out);
		
		//float ff = vibrator[0]->outputSample(0);
		//scope.log(ff/1000.0f,0.0,0.0);
		//scope.log(left_out,inputData[0],inputData[1]);
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
		capacitiveKerbang[0] = false; capacitiveKerbang[1] = false; capacitiveKerbang[2] = false;
		tickedOff = false;
	}


}

void cleanup(BelaContext *context, void *userData)
{
	delete(mixer);
	delete(audioInNode);
	for (unsigned j=0; j<NUM_TOUCH_PINS; j++) {
		delete(freq[j]);
		delete(cycle[j]);
		delete(env[j]);
	}
	
	free(analogSensorValues);
	//delete baseFaceCarriers[0];
	//delete baseFaceCarriers[1];
	//delete baseFaceCarriers[2];
	
}


