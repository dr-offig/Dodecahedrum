/***** Brain.h *****/

#ifndef __BRAIN__
#define __BRAIN__

#include <Bela.h>
#include "Tonality.h"

class Brain
{
public:
	Brain();
	~Brain();
	Brain(const Brain&);
	Brain& operator=(const Brain&);    

	void receiveLetter(int bar, int beat, int tick, string letter, int velocity);	
	//void sendNoteOn();
	
};





#endif