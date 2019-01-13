/***** signal.cpp *****/

#include "math.h"
#include "signal.h"


float ewma(float x,float s,float a)
{
	return (x * a + s * (1.f - a));	
}

float z_score(float mu, float sigma)
{
	if (sigma > 0.0f)
		return mu / sigma;
	else
		return 0.0f;
}


bool Schottky::update(float value)
{
	if (triggered) {
		triggered = (value > outThreshold);
		return false;
	} else if (value > inThreshold) {
		triggered = true;
		return true;
	} else {
		return false;
	}
}


bool TimedSchottky::update(float value)
{
	if (triggered) {
		triggered = ((value > outThreshold) || cc++ < d);
		return false;
	} else if (value > inThreshold) {
		cc = 0;
		triggered = true;
		return true;
	} else {
		return false;
	}
}


bool OneShot::active(uint64_t time)
{
	return (start <= time && time < start + duration);	
}


float OneShot::value(uint64_t time)
{
	return (active(time) ? amplitude : 0.0f);
}
