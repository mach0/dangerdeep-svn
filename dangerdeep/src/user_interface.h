// user interface for controlling a sea_object
// subsim (C)+(W) Thorsten Jordan. SEE LICENSE

#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#include <list>
#include <vector>
using namespace std;
#include "sea_object.h"
#include "global_data.h"

#define WAVES 32        // must be 2^x
#define WATERSIZE 64   // must be 2^x
#define WAVESIZE 10	// meters from wave to wave
#define WATERRANGE WATERSIZE*WAVESIZE/2
#define WAVETIDEHEIGHT 1.5      // half amplitude of waves in meters
#define WAVETIDECYCLETIME 2.0

#define MAPGRIDSIZE 1000	// meters
#define MAXTRAILNUMBER 20
#define TRAILTIME 10

class user_interface
{
protected:
	bool quit;		// whishes user to quit?
	bool pause;
	unsigned time_scale;

	user_interface() : quit(false), pause(false), time_scale(1) {
		if (allwaveheights.size() == 0) init_water_data();
	}
	user_interface& operator= (const user_interface& other);
	user_interface(const user_interface& other);

	static vector<float> allwaveheights;
	static void init_water_data(void);
	static float get_waterheight(int x, int y, int wave);
	static float get_waterheight(float x_, float y_, int wave);	// bilinear sampling

public:	
	virtual ~user_interface() {};
	virtual void display(class system& sys, class game& gm) = 0;
	// if playerobj != 0 this obj is not drawn
	virtual void draw_view(class system& sys, class game& gm, const vector3& viewpos,
		angle direction, sea_object* playerobj, bool withunderwaterweapons);
	virtual bool user_quits(void) const { return quit; }
	virtual bool paused(void) const { return pause; }
	virtual unsigned time_scaling(void) const { return time_scale; }
};

#endif
