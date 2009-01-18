/*
Danger from the Deep - Open source submarine simulation
Copyright (C) 2003-2006  Thorsten Jordan, Luis Barrancos and others.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

// SDL/OpenGL based system services
// (C)+(W) by Thorsten Jordan. See LICENSE

#ifndef SYSTEM_H
#define SYSTEM_H

#include <SDL.h>
#include <list>
#include <set>
#include <string>
#include "vector2.h"
#include "singleton.h"

#ifdef WIN32
#ifndef M_PI
#define M_PI 3.1415926535897932	// should be in math.h, but not for Windows. *sigh*
#endif
#endif

#ifdef WIN32
#define system System
#endif

class font;
class texture;

///\brief This class groups system related functions like graphic output or user input.
class system : public singleton<class system>
{
	friend class singleton<system>;
public:
	/// used to handle out-of-order quit events, do NOT heir from std::exception!
	class quit_exception
	{
	public:
		int retval;
		quit_exception(int retval_ = 0) : retval(retval_) {}
	};

	enum button_type { left_button=0x1, right_button=0x2, middle_button=0x4, wheel_up=0x8, wheel_down=0x10 };
	system(double nearz_, double farz_, unsigned res_x=1024, unsigned res_y=768, bool fullscreen=true);
	~system();
	void set_video_mode(unsigned res_x_, unsigned res_y_, bool fullscreen);
	void swap_buffers();

	// must be called once per frame (or the OS will think your app is dead)
	// the events are also bypassed to the application.
	// if you want to interpret the events yourself, just call flush_key_queue()
	// after poll_event_queue to avoid filling the queue.
	std::list<SDL_Event> poll_event_queue();
	//fixme: mouse position translation is now missing!!!!
	//but if the position is translated here, e.g. from high to a low resolution
	//many small movements may get mapped to < 1.0 movements, they are zero ->
	//mouse movement is NEVER noticed.
	// We have to translate the events and handle screen res, mouse pos/movement
	//must be correct also for subpixel cases! big fixme!

	void screen_resize(unsigned w, unsigned h, double nearz, double farz);
	
	void draw_console_with(const font* fnt, const texture* background = 0);
	void prepare_2d_drawing();	// must be called as pair!
	void unprepare_2d_drawing();

	unsigned long millisec();	// returns time in milliseconds

	static inline system& sys() { return instance(); }
	
	void set_screenshot_directory(const std::string& s) { screenshot_dir = s; }
	void screenshot(const std::string& filename = std::string());

	// takes effect only after next prepare_2d_drawing()
	void set_res_2d(unsigned x, unsigned y) { res_x_2d = x; res_y_2d = y; }

	// set maximum fps rate (0 for unlimited)
	void set_max_fps(unsigned fps) { maxfps = fps; }

	// give FOV X in degrees, aspect (w/h), znear and zfar.	
	void gl_perspective_fovx(double fovx, double aspect, double znear, double zfar);

	unsigned get_res_x() const { return res_x; }
	unsigned get_res_y() const { return res_y; }
	unsigned get_res_x_2d() const { return res_x_2d; }
	unsigned get_res_y_2d() const { return res_y_2d; }
	vector2i get_res() const { return vector2i(res_x, res_y); }
	vector2i get_res_2d() const { return vector2i(res_x_2d, res_y_2d); }
	unsigned get_res_area_2d_x() const { return res_area_2d_x; }
	unsigned get_res_area_2d_y() const { return res_area_2d_y; }
	unsigned get_res_area_2d_w() const { return res_area_2d_w; }
	unsigned get_res_area_2d_h() const { return res_area_2d_h; }
	
	// is a given OpenGL extension supported?
	bool extension_supported(const std::string& s) const;

	const std::list<vector2i>& get_available_resolutions() const { return available_resolutions; }
	bool is_fullscreen_mode() const { return is_fullscreen; }
	
private:
	system();
	system(const system& );
	system& operator= (const system& );

	unsigned res_x, res_y;		// virtual resolution (fixed for project)
	double nearz, farz;
	bool is_fullscreen;
	bool show_console;
	const font* console_font;
	const texture* console_background;	
	
	std::set<std::string> supported_extensions;	// memory supported OpenGL extensions
	
	bool draw_2d;
	unsigned res_x_2d, res_y_2d;	// real resolution (depends on user settings)
	unsigned res_area_2d_x;
	unsigned res_area_2d_y;
	unsigned res_area_2d_w;
	unsigned res_area_2d_h;
	
	void draw_console();
	int transform_2d_x(int x);
	int transform_2d_y(int y);

	unsigned long time_passed_while_sleeping;	// total sleep time
	unsigned long sleep_time;	// time when system gets to sleep
	bool is_sleeping;
	
	unsigned maxfps;		// limit fps, give 0 for no limit.
	unsigned long last_swap_time;	// time of last buffer swap
	
	double xscal_2d, yscal_2d;	// factors needed for 2d drawing
	
	int screenshot_nr;
	std::string screenshot_dir;

	// list of available resolutions
	std::list<vector2i> available_resolutions;
};

// to make user's code even shorter
inline class system& sys() { return system::sys(); }

#endif
