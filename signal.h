/***** signal.h *****/

#ifndef _DODECAHEDRUM_SIGNAL_
#define _DODECAHEDRUM_SIGNAL_

#include <Bela.h>


float ewma(float x, float s, float a);
float z_score(float mu, float sigma);

class Schottky
{
public:
	Schottky(float inThresh) : inThreshold(inThresh), outThreshold(0.5f * inThresh), triggered(false) {}
	Schottky(float inThresh, float outThresh) : inThreshold(inThresh), outThreshold(outThresh), triggered(false) {}
		
	virtual bool update(float value);

	float inThreshold;
	float outThreshold;

protected:
	bool triggered;
	
};


class TimedSchottky : Schottky
{
public:
	TimedSchottky(float inThresh, float outThresh, unsigned int suppressCount) : Schottky(inThresh, outThresh), d(suppressCount), cc(0) {}
	
	virtual bool update(float value);
	
	unsigned int d;

private:
	unsigned int cc;
	
};


class OneShot
{
public:
	// Constructors
	OneShot() : start(0), duration(512), amplitude(1.0f), trigger(false) {}
	OneShot(uint64_t strt, uint64_t dur, float amp) : start(strt), duration(dur), amplitude(amp), trigger(false) {}
	
	// member functions
	bool active(uint64_t time);
	float value(uint64_t time);
	
	// member variables
	uint64_t start;
	uint64_t duration;
	float amplitude;
	bool trigger;

};



#endif
