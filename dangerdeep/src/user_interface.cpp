// user interface common code
// subsim (C)+(W) Thorsten Jordan. SEE LICENSE

#include <GL/gl.h>
#include <GL/glu.h>
#include <SDL/SDL.h>
#include <sstream>
#include "user_interface.h"
#include "system.h"
#include "game.h"
#include "texts.h"

#define SKYSEGS 16

vector<float> user_interface::allwaveheights;

void user_interface::init_water_data(void)
{
	vector<float> dwave(WAVES);
	for (int i = 0; i < WAVES; ++i)
		dwave[i] = WAVETIDEHEIGHT*sin(i*2*M_PI/WAVES);
	vector<unsigned char> waterheight(WATERSIZE*WATERSIZE);
	for (int j = 0; j < WATERSIZE; ++j)
		for (int i = 0; i < WATERSIZE; ++i)
			waterheight[j*WATERSIZE+i] = rand()%256;
	allwaveheights.resize(WAVES*WATERSIZE*WATERSIZE);
	vector<float>::iterator it = allwaveheights.begin();
	for (int k = 0; k < WAVES; ++k) {
		for (int j = 0; j < WATERSIZE; ++j) {
			for (int i = 0; i < WATERSIZE; ++i) {
				*it++ = dwave[(int(waterheight[j*WATERSIZE+i])+k)%WAVES];
			}
		}
	}
}

inline float user_interface::get_waterheight(int x, int y, int wave)
{
	return allwaveheights[((wave&(WAVES-1))*WATERSIZE+(y&(WATERSIZE-1)))*WATERSIZE+(x&(WATERSIZE-1))];
}

float user_interface::get_waterheight(float x_, float y_, int wave)	// bilinear sampling
{
	float px = x_/WAVESIZE;
	float py = y_/WAVESIZE;
	int x = int(floor(px));
	int y = int(floor(py));
	float h0 = get_waterheight(x, y, wave);
	float h1 = get_waterheight(x+1, y, wave);
	float h2 = get_waterheight(x, y+1, wave);
	float h3 = get_waterheight(x+1, y+1, wave);
	float dx = (px - floor(px));
	float dy = (py - floor(py));
	float h01 = h0*(1-dx) + h1*dx;
	float h23 = h2*(1-dx) + h3*dx;
	return h01*(1-dy) + h23*dy;
}

void user_interface::draw_view(class system& sys, class game& gm, const vector3& viewpos,
	angle direction, bool withplayer, bool withunderwaterweapons)
{
	sea_object* player = get_player();
	int wave = int(gm.get_time()*WAVES/WAVETIDECYCLETIME);
	
	glRotatef(-90,1,0,0);
	glRotatef(direction.value(),0,0,1);
	glTranslatef(-viewpos.x, -viewpos.y, -viewpos.z);
	
	double max_view_dist = gm.get_max_view_distance();

	// ************ sky ***************************************************************
	glDisable(GL_LIGHTING);
	glPushMatrix();
	glTranslatef(viewpos.x, viewpos.y, 0);
	glScalef(max_view_dist, max_view_dist, max_view_dist);	// fixme dynamic
	unsigned dt = get_day_time(gm.get_time());
	color skycol1, skycol2;
	if (dt < 2) {
		skycol1 = color(16,16,64);
		skycol2 = color(0, 0, 32);
	} else {
		skycol1 = color(165,192,247);
		skycol2 = color(24,47,244);
	}
	skyhemisphere->display(false, &skycol1, &skycol2);
	glBindTexture(GL_TEXTURE_2D, clouds->get_opengl_name());
	float skysin[SKYSEGS], skycos[SKYSEGS];
	for (int i = 0; i < SKYSEGS; ++i) {
		float t = i*2*M_PI/SKYSEGS;
		skycos[i] = cos(t);
		skysin[i] = sin(t);
	}
	glBegin(GL_QUADS);	// fixme: quad strips!
	float rl = 0.95, ru = 0.91;
	float hl = 0.1, hu = 0.4;
	for (int j = 0; j < SKYSEGS; ++j) {
		int t = (j+1) % SKYSEGS;
		glColor4f(1,1,1,0);
		glTexCoord2f((j+1)*0.5, 0);
		glVertex3f(rl * skycos[t], rl * skysin[t], hl);
		glTexCoord2f((j  )*0.5, 0);
		glVertex3f(rl * skycos[j], rl * skysin[j], hl);
		glColor4f(1,1,1,1);
		glTexCoord2f((j  )*0.5, 1);
		glVertex3f(ru * skycos[j], ru * skysin[j], hu);
		glTexCoord2f((j+1)*0.5, 1);
		glVertex3f(ru * skycos[t], ru * skysin[t], hu);
	}
	glEnd();
	glPopMatrix();
	glEnable(GL_LIGHTING);

	// ************ water *************************************************************
	glDisable(GL_LIGHTING);
	glPushMatrix();
	int wx = int(floor(viewpos.x/WAVESIZE)) & (WATERSIZE-1);
	int wy = int(floor(viewpos.y/WAVESIZE)) & (WATERSIZE-1);
	glTranslatef(ceil(viewpos.x/WAVESIZE)*WAVESIZE-WATERRANGE, ceil(viewpos.y/WAVESIZE)*WAVESIZE-WATERRANGE, 0);

	float wd = (wave%(8*WAVES))/float(8*WAVES);
	float t0 = wd;
	float t1 = wd + (max_view_dist - WATERRANGE)/64;
	float t2 = wd + (max_view_dist + WATERRANGE)/64;
	float t3 = wd + 2*max_view_dist/64;
	float c0 = -max_view_dist+WATERRANGE;
	float c1 = 0;
	float c2 = 2*WATERRANGE;
	float c3 = max_view_dist+WATERRANGE;

	// fixme: glclearcolor depends on daytime, too

	// color of water depends on daytime
	if (dt < 2) {
		glColor3f(0.3,0.3,0.3);	// night
	} else {
		glColor3f(1,1,1);	// day
	}

	// fixme: with swimming the missing anisotropic filtering causes
	// the water to shine unnatural. a special distant_water texture doesn't help
	// just looks worse
	// fixme: while moving the distant water texture coordinates jump wildly.
	// we have to adjust its texture coordinates by remainder of viewpos.xy/WAVESIZE
	// fixme use multitexturing for distant water with various moving around the
	// texture for more realism?
	
	glBindTexture(GL_TEXTURE_2D, water->get_opengl_name());
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(t0,t3);
	glVertex3f(c0,c3,0);
	glTexCoord2f(t1,t2);
	glVertex3f(c1,c2,0);
	glTexCoord2f(t3,t3);
	glVertex3f(c3,c3,0);
	glTexCoord2f(t2,t2);
	glVertex3f(c2,c2,0);
	glTexCoord2f(t3,t0);
	glVertex3f(c3,c0,0);
	glTexCoord2f(t2,t1);
	glVertex3f(c2,c1,0);
	glTexCoord2f(t0,t0);
	glVertex3f(c0,c0,0);
	glTexCoord2f(t1,t1);
	glVertex3f(c1,c1,0);
	glTexCoord2f(t0,t3);
	glVertex3f(c0,c3,0);
	glTexCoord2f(t1,t2);
	glVertex3f(c1,c2,0);
	glEnd();
	
	//fixme waterheight of �u�erstem rand des allwaveheight-gemachten wassers auf 0
	//damit keine l�cken zu obigem wasser da sind SCHNELLER machen
	//fixme vertex lists
	//fixme visibility detection: 75% of the water are never seen but drawn

	glBindTexture(GL_TEXTURE_2D, water->get_opengl_name());
	glBegin(GL_QUADS);
	int y = wy;
	for (int j = 0; j < WATERSIZE; ++j) {
		int x = wx;
		int j2 = y%4;
		float j3 = j*WAVESIZE;
		for (int i = 0; i < WATERSIZE; ++i) {
			int i2 = x%4;
			float i3 = i*WAVESIZE;
			// fixme vertex lists
			glTexCoord2f(i2/4.0, j2/4.0);
			glVertex3f(i3,j3,(i == 0 || j == 0) ? 0 : get_waterheight(x, y, wave));
			glTexCoord2f((i2+1)/4.0, j2/4.0);
			glVertex3f(i3+WAVESIZE,j3,(i == WATERSIZE-1 || j == 0) ? 0 : get_waterheight(x+1, y, wave));
			glTexCoord2f((i2+1)/4.0, (j2+1)/4.0);
			glVertex3f(i3+WAVESIZE,j3+WAVESIZE,(i == WATERSIZE-1 || j == WATERSIZE-1) ? 0 : get_waterheight(x+1, y+1, wave));
			glTexCoord2f(i2/4.0, (j2+1)/4.0);
			glVertex3f(i3,j3+WAVESIZE,(i == 0 || j == WATERSIZE-1) ? 0 : get_waterheight(x, y+1, wave));
			x = (x + 1) & (WATERSIZE-1);
		}
		y = (y + 1) & (WATERSIZE-1);
	}
	glEnd();
	glPopMatrix();
	glEnable(GL_LIGHTING);
	glColor3f(1,1,1);

	// ******************** ships & subs *************************************************

	float dwave = sin((wave%WAVES)*2*M_PI/WAVES);
	list<ship*> ships = gm.visible_ships(player->get_pos());
	for (list<ship*>::const_iterator it = ships.begin(); it != ships.end(); ++it) {
		if (!withplayer && *it == player) continue;	// only ships or subs playable!
		glPushMatrix();
		glTranslatef((*it)->get_pos().x, (*it)->get_pos().y, (*it)->get_pos().z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
		glRotatef((3*dwave-3)/4.0,1,0,0);
		glRotatef((3*dwave-3)/4.0,0,1,0);
		(*it)->display();
		glPopMatrix();
	}

	list<submarine*> submarines = gm.visible_submarines(player->get_pos());
	for (list<submarine*>::const_iterator it = submarines.begin(); it != submarines.end(); ++it) {
		if (!withplayer && *it == player) continue; // only ships or subs playable!
		glPushMatrix();
		glTranslatef((*it)->get_pos().x, (*it)->get_pos().y, (*it)->get_pos().z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
		if ((*it)->get_pos().z > -15) {
			glRotatef((3*dwave-3)/4.0,1,0,0);
			glRotatef((3*dwave-3)/4.0,0,1,0);
		}
		(*it)->display();
		glPopMatrix();
	}

	list<airplane*> airplanes = gm.visible_airplanes(player->get_pos());
	for (list<airplane*>::const_iterator it = airplanes.begin(); it != airplanes.end(); ++it) {
		glPushMatrix();
		glTranslatef((*it)->get_pos().x, (*it)->get_pos().y, (*it)->get_pos().z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);	// simulate pitch, roll etc.
		(*it)->display();
		glPopMatrix();
	}

	if (withunderwaterweapons) {
		list<torpedo*> torpedoes = gm.visible_torpedoes(player->get_pos());
		for (list<torpedo*>::const_iterator it = torpedoes.begin(); it != torpedoes.end(); ++it) {
			glPushMatrix();
			glTranslatef((*it)->get_pos().x, (*it)->get_pos().y, (*it)->get_pos().z);
			glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
			(*it)->display();
			glPopMatrix();
		}
		list<depth_charge*> depth_charges = gm.visible_depth_charges(player->get_pos());
		for (list<depth_charge*>::const_iterator it = depth_charges.begin(); it != depth_charges.end(); ++it) {
			glPushMatrix();
			glTranslatef((*it)->get_pos().x, (*it)->get_pos().y, (*it)->get_pos().z);
			glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
			(*it)->display();
			glPopMatrix();
		}
	}

	list<gun_shell*> gun_shells = gm.visible_gun_shells(player->get_pos());
	for (list<gun_shell*>::const_iterator it = gun_shells.begin(); it != gun_shells.end(); ++it) {
		glPushMatrix();
		glTranslatef((*it)->get_pos().x, (*it)->get_pos().y, (*it)->get_pos().z);
		glRotatef(-(*it)->get_heading().value(), 0, 0, 1);
	glScalef(100,100,100);//fixme: to control functionality for now
		(*it)->display();
		glPopMatrix();
	}
}

bool user_interface::time_scale_up(void)
{
	if (time_scale < 512) {
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
