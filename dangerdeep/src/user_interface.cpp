// user interface common code
// subsim (C)+(W) Thorsten Jordan. SEE LICENSE

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "oglext/OglExt.h"
#include <GL/glu.h>
#include <SDL.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include "user_interface.h"
#include "system.h"
#include "game.h"
#include "texts.h"
#include "sound.h"
#include "logbook.h"
#include "model.h"
#include "airplane.h"
#include "depth_charge.h"
#include "gun_shell.h"
#include "water_splash.h"
#include "ships_sunk_display.h"
#include "vector3.h"
#include "widget.h"
#include "tokencodes.h"
#include "command.h"
#include "submarine_interface.h"
//#include "ship_interface.h"
//#include "airplane_interface.h"
#include "sky.h"
#include "water.h"
#include "matrix4.h"
using namespace std;

#define MAX_PANEL_SIZE 256


/*
	a note on our coordinate system (11/10/2003):
	We simulate earth by projecting objects according to curvature from earth
	space to Euclidian space. This projection is yet a identity projection, that means
	we ignore curvature yet.
	The map forms a cylinder around the earth, that means x,y position on the map translates
	to longitude,latitude values. Hence valid coordinates go from -20000km...20000km in x
	direction and -10000km to 10000km in y direction. (we could use exact values, around
	20015km). The wrap around is a problem, but that's somewhere in the Pacific ocean, so
	we just ignore it. This mapping leads to some distorsion and wrong distance values
	when coming to far north or south on the globe. We just ignore this for simplicity's
	sake. The effect shouldn't be noticeable.
*/

user_interface::user_interface(sea_object* player, game& gm) :
	pause(false), time_scale(1), player_object ( player ),
	panel_visible(true), bearing(0), elevation(0),
	viewmode(4), target(0), zoom_scope(false), mapzoom(0.1), mysky(0), mywater(0),
	mycoastmap(get_map_dir() + "default.xml"), freeviewsideang(0), freeviewupang(0), freeviewpos()
{
	init(gm);
	panel = new widget(0, 768-128, 1024, 128, "", 0, panelbackgroundimg);
	panel_messages = new widget_list(8, 8, 512, 128 - 2*8);
	panel->add_child(panel_messages);
	panel->add_child(new widget_text(528, 8, 0, 0, texts::get(1)));
	panel->add_child(new widget_text(528, 8+24+5, 0, 0, texts::get(4)));
	panel->add_child(new widget_text(528, 8+48+10, 0, 0, texts::get(5)));
	panel->add_child(new widget_text(528, 8+72+15, 0, 0, texts::get(2)));
	panel->add_child(new widget_text(528+160, 8, 0, 0, texts::get(98)));
	panel_valuetexts[0] = new widget_text(528+100, 8, 0, 0, "000");
	panel_valuetexts[1] = new widget_text(528+100, 8+24+5, 0, 0, "000");
	panel_valuetexts[2] = new widget_text(528+100, 8+48+10, 0, 0, "000");
	panel_valuetexts[3] = new widget_text(528+100, 8+72+15, 0, 0, "000");
	panel_valuetexts[4] = new widget_text(528+160+100, 8, 0, 0, "000");
	for (unsigned i = 0; i < 5; ++i)
		panel->add_child(panel_valuetexts[i]);
}

user_interface* user_interface::create(game& gm)
{
	sea_object* p = gm.get_player();
	submarine* su = dynamic_cast<submarine*>(p); if (su) return new submarine_interface(su, gm);
	//ship* sh = dynamic_cast<ship*>(p); if (sh) return new ship_interface(sh, gm);
	//airplane* ap = dynamic_cast<airplane*>(p); if (ap) return new airplane_interface(ap, gm);
	return 0;
}

user_interface::~user_interface ()
{
	delete panel;

	delete captains_logbook;
	delete ships_sunk_disp;

	delete mysky;
	delete mywater;
}

void user_interface::init(game& gm)
{
	// if the constructors of these classes may ever fail, we should use C++ exceptions.
	captains_logbook = new captains_logbook_display(gm);
	ships_sunk_disp = new ships_sunk_display(gm);

	mysky = new sky();
	mywater = new class water(128, 128, 0.0);
}

/* 2003/07/04 idea.
   simulate earth curvature by drawing several horizon faces
   approximating the curvature.
   earth has medium radius of 6371km, that means 40030km around it.
   A ship with 15m height above the waterline disappears behind
   the horizon at ca. 13.825km distance (7.465 sm)
   
   exact value 40030.17359km. (u), earth radius (r)
   
   height difference in view: (h), distance (d). Formula:
   
   h = r * (1 - cos( 360deg * d / u ) )
   
   or
   
   d = arccos ( 1 - h / r ) * u / 360deg
   
   draw ships with height -h. so (dis)appearing of ships can be
   simulated properly.
   
   highest ships are battleships (approx. 30meters), they disappear
   at 19.551km (10.557 sm).
   
   That's much shorter than I thought! But there is a mistake:
   The viewer's height is not 0 but around 6-8m for submarines,
   so the formulas are more difficult:
   
   The real distance is twice the formula, once for the viewer's
   height, once for the object:
   
   d = (arccos(1 - myh/r) + arccos(1 - h/r)) * u / 360deg
   
   or for the watched object
   
   h = r * (1 - cos( 360deg * (d - (arccos(1 - myh/r)) / u ) )
   
   so for a watcher in 6m height and other ships we have
   arccos(1-myh/r) = 0.07863384deg
   15m in height -> dist: 22.569km (12.186sm)
   30m in height -> dist: 28.295km (15.278sm)
   
   This values are useful for computing "normal" simulation's
   maximum visibility.
   Waves are disturbing sight but are ignored here.
*/	   

void user_interface::rotate_by_pos_and_wave(const vector3& pos,
	double rollfac, bool inverse) const
{
	vector3f rz = mywater->get_normal(pos.xy(), rollfac);
	vector3f rx = vector3f(1, 0, -rz.x).normal();
	vector3f ry = vector3f(0, 1, -rz.y).normal();
	if (inverse) {
		float mat[16] = {
			rx.x, ry.x, rz.x, 0,
			rx.y, ry.y, rz.y, 0,
			rx.z, ry.z, rz.z, 0,
			0,    0,    0,    1 };
		glMultMatrixf(&mat[0]);
	} else {
		float mat[16] = {
			rx.x, rx.y, rx.z, 0,
			ry.x, ry.y, ry.z, 0,
			rz.x, rz.y, rz.z, 0,
			0,    0,    0,    1 };
		glMultMatrixf(&mat[0]);
	}
}

void user_interface::draw_terrain(const vector3& viewpos, angle dir,
	double max_view_dist) const
{
#if 1
	glPushMatrix();
	glTranslatef(0, 0, -viewpos.z);
	terraintex->set_gl_texture();
	mycoastmap.render(viewpos.xy());
	glPopMatrix();
#endif
}

void user_interface::draw_view(class game& gm, const vector3& viewpos,
	int vpx, int vpy, int vpw, int vph,
	angle dir, angle elev, bool aboard, bool drawbridge, bool withunderwaterweapons)
{
//2004-03-07 fixme, the position fix doesn't fix the ship's flickering
	double max_view_dist = gm.get_max_view_distance();

	sea_object* player = get_player();

	// fixme: make use of game::job interface, 3600/256 = 14.25 secs job period
	mysky->set_time(gm.get_time());
	
	mywater->set_time(gm.get_time());

	color lightcol = gm.compute_light_color(viewpos);
	
	// compute light source position and brightness fixme to class sky or better class game?
	GLfloat lambient[4] = {0,0,0,1};//{0.2, 0.2, 0.2, 1};//lightcol.r/255.0/2.0, lightcol.g/255.0/2.0, lightcol.b/255.0/2.0, 1};
	GLfloat ldiffuse[4] = {lightcol.r/255.0, lightcol.g/255.0, lightcol.b/255.0, 1};
	GLfloat lposition[4] = {0,1,1,0};	//fixed for now. fixme
	GLfloat lspecular[4] = {0,0,0,0};
	glLightfv(GL_LIGHT0, GL_AMBIENT, lambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, ldiffuse);
	glLightfv(GL_LIGHT0, GL_POSITION, lposition);
	glLightfv(GL_LIGHT0, GL_SPECULAR, lspecular);

	//fixme: get rid of this, it conflicts with water because it swaps y and z coords.
	//instead adapt rotation matrices for scope/bridge/free view
	//glRotatef(-90,1,0,0);
	
	// if we're aboard the player's vessel move the world instead of the ship
	if (aboard) {
		//fixme: use player height correctly here
		glTranslatef(0, 0, -player->get_pos().z - mywater->get_height(player->get_pos().xy()));
		double rollfac = (dynamic_cast<ship*>(player))->get_roll_factor();
		rotate_by_pos_and_wave(player->get_pos(), rollfac, true);
	}

	glRotatef(-elev.value(),1,0,0);

	// This should be a negative angle, but nautical view dir is clockwise, OpenGL uses ccw values, so this is a double negation
	glRotatef(dir.value(),0,0,1);

	// ************ compute water reflection ******************************************
	// save projection matrix
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	// set up old projection matrix (new width/height of course) with a bit larger fov
	//   that means completely new proj matrix ;-)
//the wrong mirror effect is caused by this projection matrix.
//we should use the current matrix, but with a slightly bigger fov.
//	glLoadIdentity();
//	system::sys().gl_perspective_fovx(90.0 /*fixme*/, 1.0, 2.0, gm.get_max_view_distance());//fixme
	// set up new viewport size s*s with s<=max_texure_size and s<=w,h of old viewport
	unsigned vps = mywater->get_reflectiontex_size();
	glViewport(0, 0, vps, vps);
	// clear depth buffer (fixme: maybe clear color with upwelling color, use a bit alpha)
	glClear(GL_DEPTH_BUFFER_BIT);
	// shear one clip plane to match world space z=0 plane
	//fixme
	// flip light!
	lposition[2] = -lposition[2];
	glLightfv(GL_LIGHT0, GL_POSITION, lposition);
	// flip geometry at z=0 plane
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glScalef(1.0f, 1.0f, -1.0f);
	// draw all parts of the scene that are (partly) above the water:
	//   sky
	glCullFace(GL_FRONT);
	glPushMatrix();
	mysky->display(gm, viewpos, max_view_dist, true);
	glPopMatrix();
	//   terrain
	glColor4f(1,1,1,1);//fixme: fog is missing
	draw_terrain(viewpos, dir, max_view_dist);
	//fixme
	//   models/smoke
	//fixme
	glCullFace(GL_BACK);
	// copy viewport pixel data to reflection texture
	glBindTexture(GL_TEXTURE_2D, mywater->get_reflectiontex());
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, vps, vps, 0);

/*
	vector<Uint8> scrn(vps*vps*3);
	glReadPixels(0, 0, vps, vps, GL_RGB, GL_UNSIGNED_BYTE, &scrn[0]);
	ofstream oss("mirror.ppm");
	oss << "P6\n" << vps << " " << vps << "\n255\n";
	oss.write((const char*)(&scrn[0]), vps*vps*3);
*/

	// clear depth buffer
	//glClear(GL_DEPTH_BUFFER_BIT); // if znear/far are the same as in the scene, this clear should be enough
	// clean up
	glViewport(vpx, vpy, vpw, vph);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glClear(GL_DEPTH_BUFFER_BIT);

	// restore light! fixme the models are dark now. maybe we have to use the same modelview matrix that we used when creating the initial pos.?!
	lposition[2] = -lposition[2];
	glLightfv(GL_LIGHT0, GL_POSITION, lposition);

	// ************ sky ***************************************************************
	mysky->display(gm, viewpos, max_view_dist, false);

	// ********* fog test ************ fog color is fixme ************************
	GLfloat fog_color[4] = {0.5, 0.5, 0.5, 1.0};
	glFogi(GL_FOG_MODE, GL_LINEAR );
	glFogfv(GL_FOG_COLOR, fog_color);
	glFogf(GL_FOG_DENSITY, 1.0);	// not used in linear mode
	glHint(GL_FOG_HINT, GL_NICEST /*GL_FASTEST*/ /*GL_DONT_CARE*/);
	glFogf(GL_FOG_START, max_view_dist*0.75);	// ships disappear earlier :-(
	glFogf(GL_FOG_END, max_view_dist);
	glEnable(GL_FOG);	


	// ******* water ***************************************************************
	//mywater->update_foam(1.0/25.0);  //fixme: deltat needed here
	//mywater->spawn_foam(vector2(myfmod(gm.get_time(),256.0),0));
	mywater->display(viewpos, dir, max_view_dist);

	// ******** terrain/land ********************************************************
	glDisable(GL_FOG);	//testing with new 2d bspline terrain.
	draw_terrain(viewpos, dir, max_view_dist);
	glEnable(GL_FOG);	

/* test hack, remove
	glCullFace(GL_FRONT);
	glPushMatrix();
	glScalef(1,1,-1);
	draw_terrain(viewpos, dir, max_view_dist);
	glCullFace(GL_BACK);
	glPopMatrix();
*/

	// ******************** ships & subs *************************************************
	// 2004/03/07
	// simulate horizon: d is distance to object (on perimeter of earth)
	// z is additional height (negative!), r is earth radius
	// z = r*sin(PI/2 - d/r) - r
	// d = PI/2*r - r*arcsin(z/r+1)

	glColor3f(1,1,1);
	
	// rest of scene is displayed relative to world coordinates
	glTranslatef(-viewpos.x, -viewpos.y, -viewpos.z);

	list<ship*> ships;
	gm.visible_ships(ships, player);
	for (list<ship*>::const_iterator it = ships.begin(); it != ships.end(); ++it) {
		if (aboard && *it == player) continue;	// only ships or subs playable!
		glPushMatrix();
		vector3 pos = (*it)->get_pos();
//		pos.z += EARTH_RADIUS * (sin(M_PI/2 - pos.xy().length()/EARTH_RADIUS) - 1.0);
		glTranslated(pos.x, pos.y, pos.z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
	// fixme: z translate according to water height here
		rotate_by_pos_and_wave((*it)->get_pos(), (*it)->get_roll_factor());
		(*it)->display();
		glPopMatrix();

		if ((*it)->has_smoke()) {
			double view_dir = 90.0f - angle ( (*it)->get_pos ().xy () - player->get_pos ().xy () ).value ();
			(*it)->smoke_display (view_dir);
		}
	}

	list<submarine*> submarines;
	gm.visible_submarines(submarines, player);
	for (list<submarine*>::const_iterator it = submarines.begin(); it != submarines.end(); ++it) {
		if (aboard && *it == player) continue; // only ships or subs playable!
		glPushMatrix();
		vector3 pos = (*it)->get_pos();
		glTranslated(pos.x, pos.y, pos.z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
	// fixme: z translate according to water height here
		if ((*it)->get_pos().z > -15) {
			rotate_by_pos_and_wave((*it)->get_pos(), (*it)->get_roll_factor());
		}
		(*it)->display();
		glPopMatrix();
	}

	list<airplane*> airplanes;
	gm.visible_airplanes(airplanes, player);
	for (list<airplane*>::const_iterator it = airplanes.begin(); it != airplanes.end(); ++it) {
		glPushMatrix();
		vector3 pos = (*it)->get_pos();
		glTranslated(pos.x, pos.y, pos.z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);	// simulate pitch, roll etc.
		(*it)->display();
		glPopMatrix();
	}

	if (withunderwaterweapons) {
		list<torpedo*> torpedoes;
		gm.visible_torpedoes(torpedoes, player);
		for (list<torpedo*>::const_iterator it = torpedoes.begin(); it != torpedoes.end(); ++it) {
			glPushMatrix();
			vector3 pos = (*it)->get_pos();
			glTranslated(pos.x, pos.y, pos.z);
			glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
			(*it)->display();
			glPopMatrix();
		}
		list<depth_charge*> depth_charges;
		gm.visible_depth_charges(depth_charges, player);
		for (list<depth_charge*>::const_iterator it = depth_charges.begin(); it != depth_charges.end(); ++it) {
			glPushMatrix();
			vector3 pos = (*it)->get_pos();
			glTranslated(pos.x, pos.y, pos.z);
			glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
			(*it)->display();
			glPopMatrix();
		}
	}

	list<gun_shell*> gun_shells;
	gm.visible_gun_shells(gun_shells, player);
	for (list<gun_shell*>::const_iterator it = gun_shells.begin(); it != gun_shells.end(); ++it) {
		glPushMatrix();
		vector3 pos = (*it)->get_pos();
		glTranslated(pos.x, pos.y, pos.z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
		glScalef(10,10,10);//fixme: to control functionality for now
		(*it)->display();
		glPopMatrix();
	}

	//fixme: water splashes are bright in glasses mode, but too dark in normal mode. this is true also for other models. light seems to be ignored somewhere
	list<water_splash*> water_splashs;
	gm.visible_water_splashes ( water_splashs, player );
	for ( list<water_splash*>::const_iterator it = water_splashs.begin ();
		it != water_splashs.end (); it ++ )
	{
		double view_dir = 90.0f - angle ( (*it)->get_pos ().xy () - player->get_pos ().xy () ).value ();
		glPushMatrix ();
		vector3 pos = (*it)->get_pos();
		glTranslated(pos.x, pos.y, pos.z);
		glRotatef ( view_dir, 0.0f, 0.0f, 1.0f );
		(*it)->display ();
		glPopMatrix ();
	}

	if (aboard && drawbridge) {
		// after everything was drawn, draw conning tower (new projection matrix needed)
		glClear(GL_DEPTH_BUFFER_BIT);
//		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		system::sys().gl_perspective_fovx (90.0, 4.0/3.0 /* fixme may change */, 0.5, 100.0);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glRotatef(-elev.value()-90,1,0,0);
		glRotatef((dir - player->get_heading()).value(),0,0,1);
		conning_tower_typeVII->display();
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
//		glEnable(GL_DEPTH_TEST);
	}
	
	glDisable(GL_FOG);	
	glColor3f(1,1,1);
}

bool user_interface::time_scale_up(void)
{
	if (time_scale < 4096) {
		time_scale *= 2;
		return true;
	}
	return false;
}

bool user_interface::time_scale_down(void)
{
	if (time_scale > 1) {
		time_scale /= 2;
		return true;
	}
	return false;
}

void user_interface::draw_infopanel(class game& gm) const
{
	if (panel_visible) {
		ostringstream os0;
		os0 << setw(3) << left << get_player()->get_heading().ui_value();
		panel_valuetexts[0]->set_text(os0.str());
		ostringstream os1;
		os1 << setw(3) << left << unsigned(fabs(round(sea_object::ms2kts(get_player()->get_speed()))));
		panel_valuetexts[1]->set_text(os1.str());
		ostringstream os2;
		os2 << setw(3) << left << unsigned(round(-get_player()->get_pos().z));
		panel_valuetexts[2]->set_text(os2.str());
		ostringstream os3;
		os3 << setw(3) << left << bearing.ui_value();
		panel_valuetexts[3]->set_text(os3.str());
		ostringstream os4;
		os4 << setw(3) << left << time_scale;
		panel_valuetexts[4]->set_text(os4.str());

		panel->draw();
		// let aside the fact that we should divide DRAWING and INPUT HANDLING
		// the new process_input function eats SDL_Events which we don't have here
//		panel->process_input(true);
	}
}


texture* user_interface::torptex(unsigned type)
{
	switch (type) {
		case torpedo::T1: return torpt1;
		case torpedo::T2: return torpt2;
		case torpedo::T3: return torpt3;
		case torpedo::T3a: return torpt3a;
		case torpedo::T4: return torpt4;
		case torpedo::T5: return torpt5;
		case torpedo::T11: return torpt11;
		case torpedo::T1FAT: return torpt1fat;
		case torpedo::T3FAT: return torpt3fat;
		case torpedo::T6LUT: return torpt6lut;
	}
	return torpempty;
}

void user_interface::draw_gauge(class game& gm,
	unsigned nr, int x, int y, unsigned wh, angle a, const string& text, angle a2) const
{
	set_display_color ( gm );
	switch (nr) {
		case 1:	gauge1->draw(x, y, wh, wh); break;
		case 2:	gauge2->draw(x, y, wh, wh); break;
		case 3:	gauge3->draw(x, y, wh, wh); break;
		case 4:	gauge4->draw(x, y, wh, wh); break;
		default: return;
	}
	vector2 d = a.direction();
	int xx = x+wh/2, yy = y+wh/2;
	pair<unsigned, unsigned> twh = font_arial->get_size(text);

	color font_color ( 255, 255, 255 );
	if ( !gm.is_day_mode () )
		font_color = color ( 255, 127, 127 );

	font_arial->print(xx-twh.first/2, yy-twh.second/2, text, font_color);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (a2 != a) {
		vector2 d2 = a2.direction();
		glColor3f(0.2,0.8,1);
		glBegin(GL_LINES);
		glVertex2i(xx, yy);
		glVertex2i(xx + int(d2.x*wh*3/8),yy - int(d2.y*wh*3/8));
		glEnd();
	}
	glColor3f(1,0,0);
	glBegin(GL_TRIANGLES);
	glVertex2i(xx - int(d.y*4),yy - int(d.x*4));
	glVertex2i(xx + int(d.y*4),yy + int(d.x*4));
	glVertex2i(xx + int(d.x*wh*3/8),yy - int(d.y*wh*3/8));
	glEnd();
	glColor3f(1,1,1);
}

void user_interface::draw_clock(class game& gm,
	int x, int y, unsigned wh, double t, const string& text) const
{
	unsigned seconds = unsigned(fmod(t, 86400));
	unsigned minutes = seconds / 60;
	bool is_day_mode = gm.is_day_mode ();

	set_display_color ( gm );
	if (minutes < 12*60)
		clock12->draw(x, y, wh, wh);
	else
		clock24->draw(x, y, wh, wh);
	minutes %= 12*60;
	int xx = x+wh/2, yy = y+wh/2;
	pair<unsigned, unsigned> twh = font_arial->get_size(text);

	color font_color ( 255, 255, 255 );
	if ( !is_day_mode )
		font_color = color ( 255, 127, 127 );

	font_arial->print(xx-twh.first/2, yy-twh.second/2, text, font_color);
	vector2 d;
	int l;

	glBindTexture(GL_TEXTURE_2D, 0);
	glBegin(GL_TRIANGLES);

	d = (angle(minutes * 360 / (12*60))).direction();
	l = wh/4;
	if ( is_day_mode )
		glColor3f(0,0,0.5);
	else
		glColor3f ( 0.5f, 0.0f, 0.5f );
	glVertex2i(xx - int(d.y*4),yy - int(d.x*4));
	glVertex2i(xx + int(d.y*4),yy + int(d.x*4));
	glVertex2i(xx + int(d.x*l),yy - int(d.y*l));

	d = (angle((minutes%60) * 360 / 60)).direction();
	l = wh*3/8;
	if ( is_day_mode )
		glColor3f(0,0,1);
	else
		glColor3f ( 0.5f, 0.0f, 1.0f );
	glVertex2i(xx - int(d.y*4),yy - int(d.x*4));
	glVertex2i(xx + int(d.y*4),yy + int(d.x*4));
	glVertex2i(xx + int(d.x*l),yy - int(d.y*l));

	d = (angle((seconds%60) * 360 / 60)).direction();
	l = wh*7/16;
	glColor3f(1,0,0);
	glVertex2i(xx - int(d.y*4),yy - int(d.x*4));
	glVertex2i(xx + int(d.y*4),yy + int(d.x*4));
	glVertex2i(xx + int(d.x*l),yy - int(d.y*l));

	glEnd();
	glColor3f(1,1,1);
}

void user_interface::draw_turnswitch(class game& gm, int x, int y,
	unsigned firstdescr, unsigned nrdescr, unsigned selected, unsigned extradescr, unsigned title) const
{
	double full_turn = (nrdescr <= 2) ? 90 : 270;
	double begin_turn = (nrdescr <= 2) ? -45 : -135;
	turnswitchbackgr->draw(x, y);
	double degreesperpos = (nrdescr > 1) ? full_turn/(nrdescr-1) : 0;
	glColor4f(1,1,1,1);
	for (unsigned i = 0; i < nrdescr; ++i) {
		vector2 d = angle(begin_turn+degreesperpos*i).direction();
		system::sys().no_tex();
		glBegin(GL_LINES);
		glVertex2f(x+128+d.x*36,y+128-d.y*36);
		glVertex2f(x+128+d.x*80,y+128-d.y*80);
		glEnd();
		font_arial->print_c(x+int(d.x*96)+128, y-int(d.y*96)+128, texts::get(firstdescr+i));
	}
	font_arial->print_c(x+128, y+196, texts::get(extradescr));
	turnswitch->draw_rot(x+128, y+128, begin_turn+degreesperpos*selected);
	font_arial->print_c(x+128, y+228, texts::get(title));
}

unsigned user_interface::turnswitch_input(int x, int y, unsigned nrdescr) const
{
	if (nrdescr <= 1) return 0;
	angle a(vector2(x-128, 128-y));
	double full_turn = (nrdescr <= 2) ? 90 : 270;
	double begin_turn = (nrdescr <= 2) ? -45 : -135;
	double degreesperpos = full_turn/(nrdescr-1);
	double ang = a.value_pm180() - begin_turn;
	if (ang < 0) ang = 0;
	if (ang > full_turn) ang = full_turn;
	return unsigned(round(ang/degreesperpos));
}

void user_interface::draw_vessel_symbol(const vector2& offset, sea_object* so, color c)
{
	vector2 d = so->get_heading().direction();
	float w = so->get_width()*mapzoom/2, l = so->get_length()*mapzoom/2;
	vector2 p = (so->get_pos().xy() + offset) * mapzoom;
	p.x += 512;
	p.y = 384 - p.y;

	double clickd = mapclick.square_distance(p);
	if (clickd < mapclickdist) {
		target = so;	// fixme: message?
		mapclickdist = clickd;
	}

	c.set_gl_color();
	glBindTexture(GL_TEXTURE_2D, 0);
	glBegin(GL_QUADS);
	glVertex2f(p.x - d.y*w, p.y - d.x*w);
	glVertex2f(p.x - d.x*l, p.y + d.y*l);
	glVertex2f(p.x + d.y*w, p.y + d.x*w);
	glVertex2f(p.x + d.x*l, p.y - d.y*l);
	glEnd();
	glBegin(GL_LINES);
	glVertex2f(p.x - d.x*l, p.y + d.y*l);
	glVertex2f(p.x + d.x*l, p.y - d.y*l);
	glEnd();
	glColor3f(1,1,1);
}

void user_interface::draw_trail(sea_object* so, const vector2& offset)
{
	ship* shp = dynamic_cast<ship*>(so);
	if (shp) {
		list<vector2> l = shp->get_previous_positions();
		glColor4f(1,1,1,1);
		system::sys().no_tex();
		glBegin(GL_LINE_STRIP);
		vector2 p = (shp->get_pos().xy() + offset)*mapzoom;
		glVertex2f(512+p.x, 384-p.y);
		float la = 1.0/float(l.size()), lc = 0;
		for (list<vector2>::const_iterator it = l.begin(); it != l.end(); ++it) {
			glColor4f(1,1,1,1-lc);
			vector2 p = (*it + offset)*mapzoom;
			glVertex2f(512+p.x, 384-p.y);
			lc += la;
		}
		glEnd();
		glColor4f(1,1,1,1);
	}
}

//this function is OLD and not used any longer, remove it!!
void user_interface::display_gauges(game& gm)
{
	class system& sys = system::sys();
	sea_object* playerso = get_player ();
	ship* player = dynamic_cast<ship*>(playerso);	// ugly hack to allow compilation
	if (!player) return;
	sys.prepare_2d_drawing();
	set_display_color ( gm );
	for (int y = 0; y < 3; ++y)	// fixme: replace with gauges
		for (int x = 0; x < 4; ++x)
			psbackgr->draw(x*256, y*256, 256, 256);
	angle player_speed = player->get_speed()*360.0/sea_object::kts2ms(36);
	angle player_depth = -player->get_pos().z;
	draw_gauge(gm, 1, 0, 0, 256, player->get_heading(), texts::get(1),
		player->get_head_to());
	draw_gauge(gm, 2, 256, 0, 256, player_speed, texts::get(4));
	draw_gauge(gm, 4, 2*256, 0, 256, player_depth, texts::get(5));
	draw_clock(gm, 3*256, 0, 256, gm.get_time(), texts::get(61));

	draw_infopanel(gm);
	sys.unprepare_2d_drawing();

	// mouse handling
	int mx, my, mb = sys.get_mouse_buttons();
	sys.get_mouse_position(mx, my);

	if (mb & sys.left_button) {
		int marea = (my/256)*4+(mx/256);
		int mareax = (mx/256)*256+128;
		int mareay = (my/256)*256+128;
		angle mang(vector2(mx - mareax, mareay - my));
		if ( marea == 0 )
		{
			gm.send(new command_head_to_ang(player, mang, mang.is_cw_nearer(
				player->get_heading())));
		}
		else if ( marea == 1 )
		{}
		else if ( marea == 2 )
		{
			submarine* sub = dynamic_cast<submarine*> ( player );
			if ( sub )
			{
				gm.send(new command_dive_to_depth(sub, mang.ui_value()));
			}
		}
	}

	// keyboard processing
	int key = sys.get_key().sym;
	while (key != 0) {
		if (!keyboard_common(key, gm)) {
			// specific keyboard processing
		}
		key = sys.get_key().sym;
	}
}

void user_interface::display_bridge(game& gm)
{
	class system& sys = system::sys();
	unsigned res_x = system::sys().get_res_x();
	unsigned res_y = system::sys().get_res_y();
	sea_object* player = get_player();
    
	glClear(GL_DEPTH_BUFFER_BIT /* | GL_COLOR_BUFFER_BIT */);	// fixme remove color buffer bit for speedup
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glRotatef(-90, 1,0,0);	// swap y and z coordinates (camera looks at +y axis)

	vector2 phd = player->get_heading().direction();
	vector3 viewpos = player->get_pos() + vector3(0, 0, 6) + phd.xy0();
	// no torpedoes, no DCs, with player
	draw_view(gm, viewpos, 0,0,res_x,res_y, player->get_heading()+bearing, elevation, true, true, false);

	sys.prepare_2d_drawing();
	draw_infopanel(gm);
	sys.unprepare_2d_drawing();

	int mmx, mmy;
	sys.get_mouse_motion(mmx, mmy);	
	if (sys.get_mouse_buttons() & system::right_button) {
		SDL_ShowCursor(SDL_DISABLE);
		bearing += angle(float(mmx)/4);
		float e = elevation.value_pm180() - float(mmy)/4; // make this - to a + to invert mouse look
		if (e < 0) e = 0;
		if (e > 90) e = 90;
		elevation = angle(e);
	} else {
		SDL_ShowCursor(SDL_ENABLE);
	}

	// keyboard processing
	int key = sys.get_key().sym;
	while (key != 0) {
		if (!keyboard_common(key, gm)) {
			// specific keyboard processing
			switch ( key )
			{
				// Zoom view
				case SDLK_y:
					zoom_scope = true;
					break;
			}
		}
		key = sys.get_key().sym;
	}
}

void user_interface::draw_pings(class game& gm, const vector2& offset)
{
	// draw pings (just an experiment, you can hear pings, locate their direction
	//	a bit fuzzy but not their origin or exact shape).
	const list<game::ping>& pings = gm.get_pings();
	for (list<game::ping>::const_iterator it = pings.begin(); it != pings.end(); ++it) {
		const game::ping& p = *it;
		// vector2 r = player_object->get_pos ().xy () - p.pos;
		vector2 p1 = (p.pos + offset)*mapzoom;
		vector2 p2 = p1 + (p.dir + p.ping_angle).direction() * p.range * mapzoom;
		vector2 p3 = p1 + (p.dir - p.ping_angle).direction() * p.range * mapzoom;
		glBegin(GL_TRIANGLES);
		glColor4f(0.5,0.5,0.5,1);
		glVertex2f(512+p1.x, 384-p1.y);
		glColor4f(0.5,0.5,0.5,0);
		glVertex2f(512+p2.x, 384-p2.y);
		glVertex2f(512+p3.x, 384-p3.y);
		glEnd();
		glColor4f(1,1,1,1);
	}
}

void user_interface::draw_sound_contact(class game& gm, const sea_object* player,
	double max_view_dist, const vector2& offset)
{
	// draw sound contacts
	list<ship*> ships;
	gm.sonar_ships(ships, player);
	for (list<ship*>::iterator it = ships.begin(); it != ships.end(); ++it) {
		vector2 ldir = (*it)->get_pos().xy() - player->get_pos().xy();
		ldir = ldir.normal() * 0.666666 * max_view_dist*mapzoom;
		vector2 pos = (player_object->get_pos().xy() + offset) * mapzoom;
		if ((*it)->get_class() == ship::MERCHANT)
			glColor3f(0,0,0);
		else if ((*it)->get_class() == ship::WARSHIP)
			glColor3f(0,0.5,0);
		else if ((*it)->get_class() == ship::ESCORT)
			glColor3f(1,0,0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBegin(GL_LINES);
		glVertex2f(512+pos.x, 384-pos.y);
		glVertex2f(512+pos.x+ldir.x, 384-pos.y-ldir.y);
		glEnd();
		glColor3f(1,1,1);
	}

	list<submarine*> submarines;
	gm.sonar_submarines ( submarines, player );
	for ( list<submarine*>::iterator it = submarines.begin ();
		it != submarines.end (); it ++ )
	{
		vector2 ldir = (*it)->get_pos().xy() - player->get_pos().xy();
		ldir = ldir.normal() * 0.666666 * max_view_dist*mapzoom;
		// Submarines are drawn in blue.
		glColor3f(0,0,1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBegin(GL_LINES);
		glVertex2f(512,384);
		glVertex2f(512+ldir.x, 384-ldir.y);
		glEnd();
		glColor3f(1,1,1);
	}
}

void user_interface::draw_visual_contacts(class game& gm,
    const sea_object* player, const vector2& offset)
{
	// draw vessel trails and symbols (since player is submerged, he is drawn too)
	list<ship*> ships;
	gm.visible_ships(ships, player);
	list<submarine*> submarines;
	gm.visible_submarines(submarines, player);
	list<airplane*> airplanes;
	gm.visible_airplanes(airplanes, player);
	list<torpedo*> torpedoes;
	gm.visible_torpedoes(torpedoes, player);

   	// draw trails
   	for (list<ship*>::iterator it = ships.begin(); it != ships.end(); ++it)
   		draw_trail(*it, offset);
   	for (list<submarine*>::iterator it = submarines.begin(); it != submarines.end(); ++it)
   		draw_trail(*it, offset);
   	for (list<airplane*>::iterator it = airplanes.begin(); it != airplanes.end(); ++it)
   		draw_trail(*it, offset);
   	for (list<torpedo*>::iterator it = torpedoes.begin(); it != torpedoes.end(); ++it)
   		draw_trail(*it, offset);

   	// draw vessel symbols
   	for (list<ship*>::iterator it = ships.begin(); it != ships.end(); ++it)
   		draw_vessel_symbol(offset, *it, color(192,255,192));
   	for (list<submarine*>::iterator it = submarines.begin(); it != submarines.end(); ++it)
   		draw_vessel_symbol(offset, *it, color(255,255,128));
   	for (list<airplane*>::iterator it = airplanes.begin(); it != airplanes.end(); ++it)
   		draw_vessel_symbol(offset, *it, color(0,0,64));
   	for (list<torpedo*>::iterator it = torpedoes.begin(); it != torpedoes.end(); ++it)
   		draw_vessel_symbol(offset, *it, color(255,0,0));
}

void user_interface::draw_square_mark ( class game& gm,
	const vector2& mark_pos, const vector2& offset, const color& c )
{
	c.set_gl_color ();
	system::sys().no_tex();
	glBegin ( GL_LINE_LOOP );
	vector2 p = ( mark_pos + offset ) * mapzoom;
	int x = int ( round ( p.x ) );
	int y = int ( round ( p.y ) );
	glVertex2i ( 512-4+x,384-4-y );
	glVertex2i ( 512+4+x,384-4-y );
	glVertex2i ( 512+4+x,384+4-y );
	glVertex2i ( 512-4+x,384+4-y );
	glEnd ();
}

void user_interface::display_map(game& gm)
{
	class system& sys = system::sys();
	
	// get mouse values before drawing (mapclick is used in draw_vessel_symbol)
	int mx, my, mb = sys.get_mouse_buttons();
	sys.get_mouse_position(mx, my);
	mapclick = vector2(mx, my);
	mapclickdist = (mb & sys.left_button) ? 1e20 : -1;

	int mmx, mmy;
	sys.get_mouse_motion(mmx, mmy);
	if (mb & sys.middle_button) {
		mapoffset.x += mmx / mapzoom;
		mapoffset.y += -mmy / mapzoom;
	}

	sea_object* player = get_player ();
	bool is_day_mode = gm.is_day_mode ();

	if ( is_day_mode )
		glClearColor ( 0.0f, 0.0f, 1.0f, 1.0f );
	else
		glClearColor ( 0.0f, 0.0f, 0.75f, 1.0f );
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	double max_view_dist = gm.get_max_view_distance();

	vector2 offset = player->get_pos().xy() + mapoffset;
//unsigned detl = 0xffffff;	// fixme: remove, lod test hack
//if (mb&2) detl = my*10/384;

	sys.prepare_2d_drawing();

	float delta = MAPGRIDSIZE*mapzoom;
	float sx = myfmod(512, delta)-myfmod(offset.x, MAPGRIDSIZE)*mapzoom;
	float sy = 768.0 - (myfmod(384.0f, delta)-myfmod(offset.y, MAPGRIDSIZE)*mapzoom);
	int lx = int(1024/delta)+2, ly = int(768/delta)+2;

	// draw grid
	if (mapzoom >= 0.01) {
		glColor3f(0.5, 0.5, 1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBegin(GL_LINES);
		for (int i = 0; i < lx; ++i) {
			glVertex2f(sx, 0);
			glVertex2f(sx, 768);
			sx += delta;
		}
		for (int i = 0; i < ly; ++i) {
			glVertex2f(0, sy);
			glVertex2f(1024, sy);
			sy -= delta;
		}
		glEnd();
	}

	glColor3f(1,1,1);
	glPushMatrix();
	glTranslatef(512, 384, 0);
	glScalef(mapzoom, mapzoom, 1);
	glScalef(1,-1,1);
	glTranslatef(-offset.x, -offset.y, 0);
	mycoastmap.draw_as_map(offset, mapzoom);//, detl);
	glPopMatrix();
	sys.no_tex();

	// draw convoy positions	fixme: should be static and fade out after some time
	glColor3f(1,1,1);
	list<vector2> convoy_pos;
	gm.convoy_positions(convoy_pos);
	for (list<vector2>::iterator it = convoy_pos.begin(); it != convoy_pos.end(); ++it) {
		draw_square_mark ( gm, (*it), -offset, color ( 0, 0, 0 ) );
	}
	glColor3f(1,1,1);

	// draw view range
	glColor3f(1,0,0);
	sys.no_tex();
	glBegin(GL_LINE_LOOP);
	for (int i = 0; i < 512; ++i) {
		float a = i*2*M_PI/512;
		glVertex2f(512+(sin(a)*max_view_dist-mapoffset.x)*mapzoom, 384-(cos(a)*max_view_dist-mapoffset.y)*mapzoom);
	}
	glEnd();
	glColor3f(1,1,1);

	// draw vessel symbols (or noise contacts)
	submarine* sub_player = dynamic_cast<submarine*> ( player );
	if (sub_player && sub_player->is_submerged ()) {
		// draw pings
		draw_pings(gm, -offset);

		// draw sound contacts
		draw_sound_contact(gm, sub_player, max_view_dist, -offset);

		// draw player trails and player
		draw_trail(player, -offset);
		draw_vessel_symbol(-offset, sub_player, color(255,255,128));

		// Special handling for submarine player: When the submarine is
		// on periscope depth and the periscope is up the visual contact
		// must be drawn on map.
		if ((sub_player->get_depth() <= sub_player->get_periscope_depth()) &&
			sub_player->is_scope_up())
		{
			draw_visual_contacts(gm, sub_player, -offset);

			// Draw a red box around the selected target.
			if (target)
			{
				draw_square_mark ( gm, target->get_pos ().xy (), -offset,
					color ( 255, 0, 0 ) );
				glColor3f ( 1.0f, 1.0f, 1.0f );
			}
		}
	} 
	else	 	// enable drawing of all object as testing hack by commenting this, fixme
	{
		draw_visual_contacts(gm, player, -offset);

		// Draw a red box around the selected target.
		if (target)
		{
			draw_square_mark ( gm, target->get_pos ().xy (), -offset,
				color ( 255, 0, 0 ) );
			glColor3f ( 1.0f, 1.0f, 1.0f );
		}
	}

	// draw notepad sheet giving target distance, speed and course
	if (target) {
		int nx = 768, ny = 512;
		notepadsheet->draw(nx, ny);
		ostringstream os0, os1, os2;
		// fixme: use estimated values from target/tdc estimation here, make functions for that
		os0 << texts::get(3) << ": " << unsigned(target->get_pos().xy().distance(player->get_pos().xy())) << texts::get(206);
		os1 << texts::get(4) << ": " << unsigned(sea_object::ms2kts(target->get_speed())) << texts::get(208);
		os2 << texts::get(1) << ": " << unsigned(target->get_heading().value()) << texts::get(207);
		font_arial->print(nx+16, ny+40, os0.str(), color(0,0,128));
		font_arial->print(nx+16, ny+60, os1.str(), color(0,0,128));
		font_arial->print(nx+16, ny+80, os2.str(), color(0,0,128));
	}

	// draw world coordinates for mouse
	double mouserealmx = double(mx - 512) / mapzoom + offset.x;
	double mouserealmy = double(384 - my) / mapzoom + offset.y;
	unsigned degrx, degry, minutx, minuty;
	bool west, south;
	sea_object::meters2degrees(mouserealmx, mouserealmy, west, degrx, minutx, south, degry, minuty);
	ostringstream rwcoordss;
	rwcoordss	<< degry << "/" << minuty << (south ? "S" : "N") << ", "
			<< degrx << "/" << minutx << (west ? "W" : "E");
	font_arial->print(0, 0, rwcoordss.str(), color::white(), true);


	draw_infopanel(gm);
	sys.unprepare_2d_drawing();

	// further Mouse handling

	// keyboard processing
	int key = sys.get_key().sym;
	while (key != 0) {
		if (!keyboard_common(key, gm)) {
			// specific keyboard processing
			switch(key) {
				case SDLK_EQUALS :
				case SDLK_PLUS : if (mapzoom < 1) mapzoom *= 2; break;
				case SDLK_MINUS : if (mapzoom > 1.0/16384) mapzoom /= 2; break;
			}
		}
		key = sys.get_key().sym;
	}
}

void user_interface::display_logbook(game& gm)
{
	class system& sys = system::sys();

	// glClearColor ( 0.5f, 0.25f, 0.25f, 0 );
	glClearColor ( 0, 0, 0, 0 );
	glClear ( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	sys.prepare_2d_drawing ();
	captains_logbook->display ( gm );
	draw_infopanel ( gm );
	sys.unprepare_2d_drawing ();

	// mouse processing;
	int mx;
	int my;
	int mb = sys.get_mouse_buttons();
	sys.get_mouse_position(mx, my);
	if ( mb & sys.left_button )
		captains_logbook->check_mouse ( mx, my, mb );

	// keyboard processing
	int key = sys.get_key().sym;
	while (key != 0) {
		if (!keyboard_common(key, gm)) {
			// specific keyboard processing
			captains_logbook->check_key ( key, gm );
		}
		key = sys.get_key().sym;
	}
}

void user_interface::display_successes(game& gm)
{
	class system& sys = system::sys();

	// glClearColor ( 0, 0, 0, 0 );
	// glClear ( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	sys.prepare_2d_drawing ();
	ships_sunk_disp->display ( gm );
	draw_infopanel ( gm );
	sys.unprepare_2d_drawing ();

	// keyboard processing
	int key = sys.get_key ().sym;
	while ( key != 0 )
	{
		if ( !keyboard_common ( key, gm ) )
		{
			// specific keyboard processing
			ships_sunk_disp->check_key ( key, gm );
		}
		key = sys.get_key ().sym;
	}
}

#ifdef OLD
void user_interface::display_successes(game& gm)
{
	class system& sys = system::sys();

	glClearColor(0.25, 0.25, 0.25, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	sys.prepare_2d_drawing();
	font_arial->print(0, 0, "success records - fixme");
	font_arial->print(0, 100, "Ships sunk\n----------\n");
	unsigned ships = 0, tons = 0;
	for (list<unsigned>::const_iterator it = tonnage_sunk.begin(); it != tonnage_sunk.end(); ++it) {
		++ships;
		char tmp[20];
		sprintf(tmp, "%u BRT", *it);
		font_arial->print(0, 100+(ships+2)*font_arial->get_height(), tmp);
		tons += *it;
	}
	char tmp[40];
	sprintf(tmp, "total: %u BRT", tons);
	font_arial->print(0, 100+(ships+4)*font_arial->get_height(), tmp);
	sys.unprepare_2d_drawing();

	// keyboard processing
	int key = sys.get_key().sym;
	while (key != 0) {
		if (!keyboard_common(key, gm)) {
			// specific keyboard processing
		}
		key = sys.get_key().sym;
	}
}
#endif // OLD

void user_interface::display_freeview(game& gm)
{
	class system& sys = system::sys();
	unsigned res_x = system::sys().get_res_x();
	unsigned res_y = system::sys().get_res_y();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

//	SDL_ShowCursor(SDL_DISABLE);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(-90, 1,0,0);	// swap y and z coordinates (camera looks at +y axis)

	glPushMatrix();	
	glRotatef(freeviewsideang,0,0,1);
	glRotatef(freeviewupang,1,0,0);
	matrix4 viewmatrix = matrix4::get_gl(GL_MODELVIEW_MATRIX);
	vector3 sidestep = viewmatrix.row(0);
	vector3 upward = viewmatrix.row(1);
	vector3 forward = -viewmatrix.row(2);
	glPopMatrix();

	// draw everything (dir can be ignored/0, all water tiles must get drawn, not only the viewing cone, fixme)
	draw_view(gm, freeviewpos, 0,0,res_x,res_y, player_object->get_heading()+bearing+freeviewsideang, freeviewupang, false, false, true);

	int mx, my;
	sys.get_mouse_motion(mx, my);
	freeviewsideang += mx*0.5;
	freeviewupang -= my*0.5;

	sys.prepare_2d_drawing();
	draw_infopanel(gm);
	sys.unprepare_2d_drawing();

	// keyboard processing
	int key = sys.get_key().sym;
	while (key != 0) {
		if (!keyboard_common(key, gm)) {
			// specific keyboard processing
			switch(key) {
				case SDLK_KP8: freeviewpos -= forward * 5; break;
				case SDLK_KP2: freeviewpos += forward * 5; break;
				case SDLK_KP4: freeviewpos -= sidestep * 5; break;
				case SDLK_KP6: freeviewpos += sidestep * 5; break;
				case SDLK_KP1: freeviewpos -= upward * 5; break;
				case SDLK_KP3: freeviewpos += upward * 5; break;
			}
		}
		key = sys.get_key().sym;
	}
}

void user_interface::add_message(const string& s)
{
	panel_messages->append_entry(s);
	if (panel_messages->get_listsize() > panel_messages->get_nr_of_visible_entries())
		panel_messages->delete_entry(0);
/*
	panel_texts.push_back(s);
	if (panel_texts.size() > 1+MAX_PANEL_SIZE/font_arial->get_height())
		panel_texts.pop_front();
*/		
}

void user_interface::display_glasses(class game& gm)
{
	class system& sys = system::sys();
	sea_object* player = get_player();

	unsigned res_x = system::sys().get_res_x();
	unsigned res_y = system::sys().get_res_y();

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	sys.gl_perspective_fovx (10.0, 2.0/1.0, 2.0, gm.get_max_view_distance());
	glViewport(0, res_y-res_x/2, res_x, res_x/2);
	glClear(GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(-90, 1,0,0);	// swap y and z coordinates (camera looks at +y axis)

	vector3 viewpos = player->get_pos() + vector3(0, 0, 6);
	// no torpedoes, no DCs, no player
	draw_view(gm, viewpos, 0,res_y-res_x/2,res_x,res_x/2, player->get_heading()+bearing, 0, false/*true*/, false, false);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glViewport(0, 0, res_x, res_y);
	glMatrixMode(GL_MODELVIEW);

	sys.prepare_2d_drawing();
	glasses->draw(0, 0, 512, 512);
	glasses->draw_hm(512, 0, 512, 512);
	sys.no_tex();
	color::black().set_gl_color();
	sys.draw_rectangle(0, 512, 1024, 256);
	draw_infopanel(gm);
	sys.unprepare_2d_drawing();

	// keyboard processing
	int key = sys.get_key().sym;
	while (key != 0) {
		if (!keyboard_common(key, gm)) {
			// specific keyboard processing
			switch ( key )
			{
				case SDLK_y:
					zoom_scope = false;
					break;
			}
		}
		key = sys.get_key().sym;
	}
}

void user_interface::add_rudder_message()
{
	ship* s = dynamic_cast<ship*>(player_object);
	if (!s) return;	// ugly hack to allow compilation
    switch (s->get_rudder_to())
    {
        case ship::rudderfullleft:
            add_message(texts::get(35));
            break;
        case ship::rudderleft:
            add_message(texts::get(33));
            break;
        case ship::ruddermidships:
            add_message(texts::get(42));
            break;
        case ship::rudderright:
            add_message(texts::get(34));
            break;
        case ship::rudderfullright:
            add_message(texts::get(36));
            break;
    }
}

#define DAY_MODE_COLOR() glColor3f ( 1.0f, 1.0f, 1.0f )

#define NIGHT_MODE_COLOR() glColor3f ( 1.0f, 0.4f, 0.4f )

void user_interface::set_display_color ( color_mode mode ) const
{
	switch ( mode )
	{
		case night_color_mode:
			NIGHT_MODE_COLOR ();
			break;
		default:
			DAY_MODE_COLOR ();
			break;
	}
}

void user_interface::set_display_color ( const class game& gm ) const
{
	if ( gm.is_day_mode () )
		DAY_MODE_COLOR ();
	else
		NIGHT_MODE_COLOR ();
}

sound* user_interface::get_sound_effect ( sound_effect se ) const
{
	sound* s = 0;

	switch ( se )
	{
		case se_submarine_torpedo_launch:
			s = torpedo_launch_sound;
			break;
		case se_torpedo_detonation:
			{
				submarine* sub = dynamic_cast<submarine*>( player_object );

				if ( sub && sub->is_submerged () )
				{
					double sid = rnd ( 2 );
					if ( sid < 1.0f )
						s = torpedo_detonation_submerged[0];
					else if ( sid < 2.0f )
						s = torpedo_detonation_submerged[1];
				}
				else
				{
					double sid = rnd ( 2 );
					if ( sid < 1.0f )
						s = torpedo_detonation_surfaced[0];
					else if ( sid < 2.0f )
						s = torpedo_detonation_surfaced[1];
				}
			}
			break;
	}

	return s;
}

void user_interface::play_sound_effect ( sound_effect se, double volume ) const
{
	sound* s = get_sound_effect ( se );

	if ( s )
		s->play ( volume );
}

void user_interface::play_sound_effect_distance ( sound_effect se, double distance ) const
{
	sound* s = get_sound_effect ( se );

	if ( s )
		s->play ( ( 1.0f - player_object->get_noise_factor () ) * exp ( - distance / 3000.0f ) );
}

void user_interface::add_captains_log_entry ( class game& gm, const string& s)
{
	date d(unsigned(gm.get_time()));

	if ( captains_logbook )
		captains_logbook->add_entry( d, s );
}

inline void user_interface::record_sunk_ship ( const ship* so )
{
	ships_sunk_disp->add_sunk_ship ( so );
}

void user_interface::draw_manometer_gauge ( class game& gm,
	unsigned nr, int x, int y, unsigned wh, float value, const string& text) const
{
	set_display_color ( gm );
	switch (nr)
	{
		case 1:
			gauge5->draw ( x, y, wh, wh / 2 );
			break;
		default:
			return;
	}
	angle a ( 292.5f + 135.0f * value );
	vector2 d = a.direction ();
	int xx = x + wh / 2, yy = y + wh / 2;
	pair<unsigned, unsigned> twh = font_arial->get_size(text);

	// Draw text.
	color font_color ( 0, 0, 0 );
	font_arial->print ( xx - twh.first / 2, yy - twh.second / 2 - wh / 6,
		text, font_color );

	// Draw pointer.
	glColor3f ( 0.0f, 0.0f, 0.0f );
	glBindTexture ( GL_TEXTURE_2D, 0 );
	glBegin ( GL_LINES );
	glVertex2i ( xx + int ( d.x * wh / 16 ), yy - int ( d.y * wh / 16 ) );
	glVertex2i ( xx + int ( d.x * wh * 3 / 8 ), yy - int ( d.y * wh * 3 / 8 ) );
	glEnd ();
	glColor3f ( 1.0f, 1.0f, 1.0f );
}
