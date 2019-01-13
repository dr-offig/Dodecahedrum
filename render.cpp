#include "signal.h"
#include <OSCServer.h>
#include <OSCClient.h>
#include <Scope.h>
#include <algorithm>
#include <string>
#include <deque>

///// capacitive touch stuff///
#include "I2C_MPR121.h"
// How many pins there are
#define NUM_TOUCH_PINS 3
// Define this to print data to terminal
#undef DEBUG_MPR121
//////////////////////////////


using namespace std;

///// capacitive touch stuff///
// Change this to change how often the MPR121 is read (in Hz)
//int readInterval = 1000;
// Change this threshold to set the minimum amount of touch
int threshold = 40;
// This array holds the continuous sensor values
int sensorValue[NUM_TOUCH_PINS];
I2C_MPR121 mpr121;			// Object to handle MPR121 sensing
AuxiliaryTask i2cTask;		// Auxiliary task to read I2C
//int readCount = 0;			// How long until we read again...
//int readIntervalSamples = 0; // How many samples between reads
void readMPR121(void*);
///////////////////////////////


///// contact mic and accelerometer and triggering //////
float analogIn[3];
float smoothedIn[3];
float detailIn[3];
float smoothedDetail[3];
float rs[3] = { 0.05f, 0.05f, 0.05f };
float rd[3] = { 0.25f, 0.25f, 0.25f };
bool kerbang[3] = { false, false, false };
TimedSchottky trigger[3] = { TimedSchottky(0.05f, 0.000f, 1024), TimedSchottky(0.05f, 0.000f, 1024), TimedSchottky(0.05f, 0.000f, 1024) };
int solenoidForChannel[8] = { 0, -1, 1, 2, -1, -1, -1, -1};
//----------------------------------------------------//

//--- OSC communication ----//
AuxiliaryTask oscServerTask;
AuxiliaryTask oscClientTask;
AuxiliaryTask playTask;
OSCServer oscServer;
OSCClient oscClient;
deque<oscpkt::Message> outbox;
//------------------------//

Scope scope;

int localPort = 8000;

OneShot hits[3];

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
		sensorValue[i] = -(mpr121.filteredData(i) - mpr121.baselineData(i));
		sensorValue[i] -= threshold;
		if(sensorValue[i] < 0)
			sensorValue[i] = 0;
#ifdef DEBUG_MPR121
		rt_printf("%d ", sensorValue[i]);
#endif
	}
#ifdef DEBUG_MPR121
	rt_printf("\n");
#endif
	
	// You can use this to read binary on/off touch state more easily
	//rt_printf("Touched: %x\n", mpr121.touched());
}



bool setup(BelaContext *context, void *userData)
{
	oscServer.setup(localPort);
	oscServerTask = Bela_createAuxiliaryTask(receiveOSC, BELA_AUDIO_PRIORITY - 30, "receive-osc", NULL);
	oscClientTask = Bela_createAuxiliaryTask(sendOSC, BELA_AUDIO_PRIORITY - 20, "send-osc", NULL);
	playTask = Bela_createAuxiliaryTask(playNotes, BELA_AUDIO_PRIORITY - 10,"play", NULL);
	
	gAudioFramesPerAnalogFrame = context->audioFrames / context->analogFrames;
	changeTempo(120.0f, context->analogSampleRate);
	
	if(!mpr121.begin(1, 0x5A)) {
		rt_printf("Error initialising MPR121\n");
		return false;
	}
	
	i2cTask = Bela_createAuxiliaryTask(readMPR121, 50, "bela-mpr121");
	//readIntervalSamples = context->audioSampleRate / readInterval;
	
	scope.setup(2,context->audioSampleRate);
	
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

		// Capacitive Sensing
		float amps[NUM_TOUCH_PINS] = {0.0f, 0.0f, 0.0f};
		for(int i = 0; i < NUM_TOUCH_PINS; i++) {
			float value = sensorValue[i] / 400.f;
			amps[i] = value;
					
			// Exponential filter bank (separates DC from AC)
			smoothedIn[i] = ewma(value, smoothedIn[i], rs[i]);
			detailIn[i] = value - smoothedIn[i];

			// Now smooth the AC component
			smoothedDetail[i] = ewma(detailIn[i], smoothedDetail[i], rd[i]);
					
			// Check for sudden loud inputs
			if (trigger[i].update(fabs(smoothedDetail[i]))) {
				kerbang[i] = true;
				OneShot& h = hits[i];
    		    h.amplitude = 1.0f;
	    		h.duration = uint64_t(512.0f * 2.0f * value);
	    		h.trigger = true;
				//rt_printf("Triggered panel %d\n", i);
				//scope.trigger();

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
					analogIn[ch] = analogRead(context, n, ch);
				}
				/**************************************************/
				
				
				// Outputs
				//if (ch < 3)
				int j = solenoidForChannel[ch];
				if (j >= 0)
				{
					uint64_t t = context->audioFramesElapsed;
					if (hits[j].trigger) {
						hits[j].start = t;
						hits[j].trigger = false;
					}

					analogWrite(context, n, ch, hits[j].value(t));
					//audioWrite(context, n, j, hits[j].value(t)/5.0f);
				}
				
			}
		
			//scope.log(analogIn[0],analogIn[1],analogIn[2]);
		}
		
		// Audio input channels
		float left_in = audioRead(context,frame,0);
		float right_in = audioRead(context,frame,1);
		
		// Pass through to Output
		audioWrite(context,frame,0,left_in);
		audioWrite(context,frame,1,right_in);
		scope.log(left_in,right_in);
		
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
		sendLetter(bar, beat, tick, kerbang[0], kerbang[1], kerbang[2]);
		kerbang[0] = false; kerbang[1] = false; kerbang[2] = false;
		tickedOff = false;
	}


}

void cleanup(BelaContext *context, void *userData)
{

}


