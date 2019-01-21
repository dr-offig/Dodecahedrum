/***** Brain.h *****/

#ifndef __BRAIN__
#define __BRAIN__

#include <Bela.h>
#include <OSCClient.h>
#include "Tonality.h"
#include <vector>

class Brain
{
public:
	Brain() : _pClient(NULL), _bar(0), _beat(0), _tick(0) {}
	~Brain();
	Brain(const Brain& b) : _pClient(b._pClient), _bar(b._bar), _beat(b._beat), _tick(b._tick) {} 
	//Brain& operator=(const Brain&);    

	void think();
	void registerHit(int bar, int beat, int tick, int panel, int velocity);	
	void registerBeat(int bar, int beat);
	void sendHit(int bar, int beat, int tick, int panel, int velocity);

	
private:
	OSCClient* _pClient;
	int _bar;
	int _beat;
	int _tick;
		
};





#endif