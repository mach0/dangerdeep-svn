// sky simulation and display (OpenGL)
// subsim (C)+(W) Thorsten Jordan. SEE LICENSE

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "oglext/OglExt.h"
#include <glu.h>
#include <SDL.h>

#include "sky.h"
#include "model.h"
#include "texture.h"
#include "global_data.h"
#include "game.h"
#include "matrix4.h"

const double CLOUD_ANIMATION_CYCLE_TIME = 3600.0;

/* fixme: idea:
   use perlin noise data as height (3d depth) of cloud.
   compute normals from it and light clouds with bump mapping
   + ambient

   clouds have self-shadowing effects, and may be lit by sun from behind if they
   are thin!
   
   add rain! (GL_LINES with varying alpha)
*/



sky::sky(double tm) : mytime(tm), skycolorfac(0.0f),
	skyhemisphere(0), skycol(0), sunglow(0),
	clouds(0), suntex(0), moontex(0), clouds_dl(0), skyhemisphere_dl(0)
{
	// ******************************* create display list for sky background
	skyhemisphere = model::ptr(new model(get_model_dir() + "skyhemisphere.3ds", false, false));
	const model::mesh& skyhemisphere_mesh = skyhemisphere->get_mesh(0);

	unsigned smv = skyhemisphere_mesh.vertices.size();
	vector<vector2f> uv0(smv);
	vector<vector2f> uv1(smv);
	vector3f center = (skyhemisphere->get_min() + skyhemisphere->get_max()) * 0.5f;
	center.z = skyhemisphere->get_min().z;
	for (unsigned i = 0; i < smv; ++i) {
		vector3f d = (skyhemisphere_mesh.vertices[i] - center).normal();
		d.z = fabs(d.z);
		if (d.z > 1.0f) d.z = 1.0f;
		float alpha = acos(fabs(d.z));
		float sinalpha = sin(alpha);
		float u = 0.5f;
		float v = 0.5f;
		if (sinalpha != 0.0f) {
			u += (alpha*d.x)/(sinalpha*M_PI);
			v += (alpha*d.y)/(sinalpha*M_PI);
			if (u < 0.0f) u = 0.0f; if (u > 1.0f) u = 1.0f;
			if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
		}
		uv0[i] = vector2f(0.0f, float(2.0*alpha/M_PI));
		uv1[i] = vector2f(u, v);
	}

	skyhemisphere_dl = glGenLists(1);
	glNewList(skyhemisphere_dl, GL_COMPILE);
	glVertexPointer(3, GL_FLOAT, sizeof(vector3f), &(skyhemisphere_mesh.vertices[0]));
	glEnableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glClientActiveTexture(GL_TEXTURE0);
	glTexCoordPointer(2, GL_FLOAT, sizeof(vector2f), &uv0[0].x);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTexture(GL_TEXTURE1);
	glTexCoordPointer(2, GL_FLOAT, sizeof(vector2f), &uv1[0].x);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glDrawElements(GL_TRIANGLES, skyhemisphere_mesh.indices.size(), GL_UNSIGNED_INT, &(skyhemisphere_mesh.indices[0]));
	glDisableClientState(GL_VERTEX_ARRAY);
	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTexture(GL_TEXTURE0);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glEndList();

	// ******************************** create stars
	const unsigned nr_of_stars = 2000;
	stars_pos.reserve(nr_of_stars);
	stars_lumin.reserve(nr_of_stars*4);
	for (unsigned i = 0; i < nr_of_stars; ++i) {
		vector3f p(rnd() * 2.0f - 1.0f, rnd() * 2.0f - 1.0f, rnd());
		stars_pos.push_back(p.normal());
		float fl = rnd();
		fl = 1.0f - fl*fl*fl;
		Uint8 l = Uint8(255*fl);
		stars_lumin.push_back(l);
		stars_lumin.push_back(l);
		stars_lumin.push_back(l);
		stars_lumin.push_back(l);
	}

	// ******************************** create map for sky color
	vector<Uint8> skycolmap(256*256*3);
	for (int y = 0; y < 256; ++y) {
		// y is height in sky (top -> horizon color gradient)
		color a = color(color( 73, 164, 255), color(173, 200, 219), float(y)/255);
		color b = color(color(143, 148, 204), color(231, 166, 89 /* 138, 156, 168*/), float(y)/255);
		for (int x = 0; x < 256; ++x) {
			// x is daytime (blue -> orange at sunset)
			color c = color(a, b, float(x)/255);
			skycolmap[(y*256+x)*3+0] = c.r;
			skycolmap[(y*256+x)*3+1] = c.g;
			skycolmap[(y*256+x)*3+2] = c.b;
		}
	}
/*
	ofstream osg("testsky.ppm");
	osg << "P6\n256 256\n255\n";
	osg.write((const char*)(&skycolmap[0]), 256*256*3);
*/
	skycol = texture::ptr(new texture(&skycolmap[0], 256, 256, GL_RGB, GL_LINEAR, GL_CLAMP_TO_EDGE));
	skycolmap.clear();

	// ******************************** create maps for sun glow
	vector<Uint8> sunglowmap(256*256);
	for (int y = 0; y < 256; ++y) {
		for (int x = 0; x < 256; ++x) {
			float dist = sqrt(float(vector2i(x-128, y-128).square_length()))/64.0f;
			if (dist > 1.0f) dist = 1.0f;
			float val = 255*exp(-10.0*(dist-0.005));
			if (val > 255) val = 255;
			sunglowmap[y*256+x] = Uint8(val);
		}
	}
/*	
	ofstream osg("testglow.pgm");
	osg << "P5\n256 256\n255\n";
	osg.write((const char*)(&sunglowmap[0]), 256*256);
*/
	sunglow = texture::ptr(new texture(&sunglowmap[0], 256, 256, GL_LUMINANCE, GL_LINEAR, GL_CLAMP_TO_EDGE));
	sunglowmap.clear();

	// ********************************** init sun/moon	
	suntex = texture::ptr(new texture(get_texture_dir() + "thesun.png", GL_LINEAR));
	moontex = texture::ptr(new texture(get_texture_dir() + "themoon.png", GL_LINEAR));
	
	// ********************************** init clouds
	// clouds are generated with Perlin noise.
	// one big texture is rendered (1024x1024, could be dynamic) and distributed
	// over 4x4 textures.
	// We generate m levels of noise maps, m <= n, texture width = 2^n.
	// here n = 10.
	// Each level is a noise map of 2^(n-m+1) pixels wide and high, scaled to full size.
	// For m = 4 we have 4 maps, 128x128 pixel each, scaled to 1024x1024, 512x512, 256x256
	// and 128x128.
	// The maps are tiled and added on a 1024x1024 map (the result), with descending
	// factors, that means level m gives factor 1/(2^(m-1)).
	// So we add the 4 maps: 1*map0 + 1/2*map1 + 1/4*map2 + 1/8*map3.
	// To animate clouds, just interpolate one level's noise map between two random
	// noise maps.
	// The result s is recomputed: cover (0-255), sharpness (0-255)
	// clamp(clamp_at_zero(s - cover) * sharpness)
	// This is used as alpha value for the texture.
	// 0 = no clouds, transparent, 255 full cloud (white/grey)
	// The final map is mapped to a hemisphere:
	// Texture coords = height_in_sphere * cos/sin(direction_in_sphere).
	// That means a circle with radius 512 of the original map is used.

	cloud_levels = 5;
	cloud_coverage = 192;//128;	// 0-256 (none-full)
	cloud_sharpness = 256;	// 0-256
	cloud_animphase = 0;
	
	cloud_interpolate_func.resize(256);
	for (unsigned n = 0; n < 256; ++n)
		cloud_interpolate_func[n] = unsigned(128-cos(n*M_PI/256)*128);

	cloud_alpha.resize(256*256);
	for (unsigned y = 0; y < 256; ++y) {
		for (unsigned x = 0; x < 256; ++x) {
			float fx = float(x)-128;
			float fy = float(y)-128;
			float d = 1.0-sqrt(fx*fx+fy*fy)/128.0;
			d = 1.0-exp(-d*5);
			if (d < 0) d = 0;
			cloud_alpha[y*256+x] = Uint8(255*d);
		}
	}

	noisemaps_0 = compute_noisemaps();
	noisemaps_1 = compute_noisemaps();
	compute_clouds();

	// create cloud mesh display list, fixme: get it from sky hemisphere mesh data!
	unsigned skyvsegs = 16;
	unsigned skyhsegs = 4*skyvsegs;
	clouds_dl = glGenLists(1);
	glNewList(clouds_dl, GL_COMPILE);
	unsigned skysegs = skyvsegs*skyhsegs;
	vector<vector3f> points;
	points.reserve(skysegs+1);
	vector<vector2f> texcoords;
	texcoords.reserve(skysegs+1);
	for (unsigned beta = 0; beta < skyvsegs; ++beta) {
		float t = (1.0-float(beta)/skyvsegs)/2;
		float r = cos(M_PI/2*beta/skyvsegs);
		float h = sin(M_PI/2*beta/skyvsegs);
		for (unsigned alpha = 0; alpha < skyhsegs; ++alpha) {
			float x = cos(2*M_PI*alpha/skyhsegs);
			float y = sin(2*M_PI*alpha/skyhsegs);
			points.push_back(vector3f(x*r, y*r, h));
			texcoords.push_back(vector2f(x*t+0.5, y*t+0.5));
		}
	}
	points.push_back(vector3f(0, 0, 1));
	texcoords.push_back(vector2f(0.5, 0.5));
	glVertexPointer(3, GL_FLOAT, sizeof(vector3f), &points[0]);
	glTexCoordPointer(2, GL_FLOAT, sizeof(vector2f), &texcoords[0]);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	vector<unsigned> indices;
	indices.reserve(skysegs*4);
	for (unsigned beta = 0; beta < skyvsegs; ++beta) {
		for (unsigned alpha = 0; alpha < skyhsegs; ++alpha) {
			unsigned i0 = beta*skyhsegs+alpha;
//			if((alpha+beta)&1)continue;
			unsigned i1 = beta*skyhsegs+(alpha+1)%skyhsegs;
			unsigned i2 = (beta==skyvsegs-1) ? skysegs : (beta+1)*skyhsegs+alpha;
			unsigned i3 = (beta==skyvsegs-1) ? skysegs : (beta+1)*skyhsegs+(alpha+1)%skyhsegs;
			indices.push_back(i0);
			indices.push_back(i2);
			indices.push_back(i3);
			indices.push_back(i1);
		}
	}
	glDrawElements(GL_QUADS, skysegs*4 /* /2 */, GL_UNSIGNED_INT, &indices[0]);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glEndList();
}



sky::~sky()
{
	glDeleteLists(clouds_dl, 1);
	glDeleteLists(skyhemisphere_dl, 1);
}



void sky::setup_textures(void) const
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	// *********************** set up texture unit 0
	// skycol (tex0) is blended into previous color
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, skycol->get_opengl_name());
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
	glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
	glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
/*
	// not needed.
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
*/
	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(skycolorfac, 0.0f, 0.0f);
	glMatrixMode(GL_MODELVIEW);	

	// *********************** set up texture unit 1
	glActiveTexture(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, sunglow->get_opengl_name());
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
/*
	// not needed.
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
*/

	glActiveTexture(GL_TEXTURE0);

	glDisable(GL_LIGHTING);
}



void sky::cleanup_textures(void) const
{
	glColor4f(1,1,1,1);

	// ******************** clean up texture unit 0
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);

	// ******************** clean up texture unit 1
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();	// pushed in display(), fixme ugly
	glMatrixMode(GL_MODELVIEW);

	glEnable(GL_LIGHTING);

	glPopAttrib();
}



void sky::advance_cloud_animation(double fac)
{
	int oldphase = int(cloud_animphase*256);
	cloud_animphase += fac;
	int newphase = int(cloud_animphase*256);
	if (cloud_animphase >= 1.0) {
		cloud_animphase -= 1.0;
		noisemaps_0 = noisemaps_1;
		noisemaps_1 = compute_noisemaps();
	} else {
		if (newphase > oldphase)
			compute_clouds();
	}
}



void sky::compute_clouds(void)
{
	unsigned mapsize = 8 - cloud_levels;
	unsigned mapsize2 = (2<<mapsize);

	// fixme: could we interpolate between accumulated noise maps
	// to further speed up the process?
	// in theory, yes. formula says that we can...
	//fixme: use perlin noise generator here!
	vector<vector<Uint8> > cmaps = noisemaps_0;
	float f = cloud_animphase;
	for (unsigned i = 0; i < cloud_levels; ++i)
		for (unsigned j = 0; j < mapsize2 * mapsize2; ++j)
			cmaps[i][j] = Uint8(noisemaps_0[i][j]*(1-f) + noisemaps_1[i][j]*f);

	// create full map
	vector<Uint8> fullmap(256 * 256 * 2);
	unsigned fullmapptr = 0;
	vector2i sunpos(64,64);		// store sun coordinates here! fixme
	float maxsundist = 362;		// sqrt(2*256^2)
	for (unsigned y = 0; y < 256; ++y) {
		for (unsigned x = 0; x < 256; ++x) {
			unsigned v = 0;
			// accumulate values
			for (unsigned k = 0; k < cloud_levels; ++k) {
				unsigned tv = get_value_from_bytemap(x, y, k, cmaps[k]);
				v += (tv >> k);
			}
			// fixme generate a lookup table for this function, depending on coverage/sharpness
			if (v > 255) v = 255;
			unsigned invcover = 256-cloud_coverage;
			if (v < invcover)
				v = 0;
			else
				v -= invcover;
			// use sharpness for exp function
			v = 255 - v * 256 / cloud_coverage;	// equalize
			int sundist = int(255-192*sqrt(float(vector2i(x,y).square_distance(sunpos)))/maxsundist);
			fullmap[fullmapptr++] = sundist;	// luminance info is wasted here, but should be used, fixme
			fullmap[fullmapptr++] = Uint8(v * unsigned(cloud_alpha[y*256+x]) / 255);
		}
	}

	clouds = texture::ptr(new texture(&fullmap[0], 256, 256, GL_LUMINANCE_ALPHA, GL_LINEAR, GL_CLAMP_TO_EDGE));
}



vector<vector<Uint8> > sky::compute_noisemaps(void)
{
	unsigned mapsize = 8 - cloud_levels;
	unsigned mapsize2 = (2<<mapsize);
	vector<vector<Uint8> > noisemaps(cloud_levels);
	for (unsigned i = 0; i < cloud_levels; ++i) {
		noisemaps[i].resize(mapsize2 * mapsize2);
		for (unsigned j = 0; j < mapsize2 * mapsize2; ++j)
			noisemaps[i][j] = (unsigned char)(255*rnd());
		smooth_and_equalize_bytemap(mapsize2, noisemaps[i]);
	}
	return noisemaps;
}



Uint8 sky::get_value_from_bytemap(unsigned x, unsigned y, unsigned level,
	const vector<Uint8>& nmap)
{
	// x,y are in 0...255, shift them according to level
	unsigned shift = cloud_levels - 1 - level;
	unsigned mapshift = 9 - cloud_levels;
	unsigned mapmask = (1 << mapshift) - 1;
	unsigned rshift = 8 - shift;
	unsigned xfrac = ((x << rshift) & 255);
	unsigned yfrac = ((y << rshift) & 255);
	x = (x >> shift);
	y = (y >> shift);
	x = x & mapmask;
	y = y & mapmask;
	unsigned x2 = (x+1) & mapmask;
	unsigned y2 = (y+1) & mapmask;

	xfrac = cloud_interpolate_func[xfrac];
	yfrac = cloud_interpolate_func[yfrac];
	
	unsigned v0 = nmap[(y<<mapshift)+x];
	unsigned v1 = nmap[(y<<mapshift)+x2];
	unsigned v2 = nmap[(y2<<mapshift)+x];
	unsigned v3 = nmap[(y2<<mapshift)+x2];
	unsigned v4 = (v0*(256-xfrac)+v1*xfrac);
	unsigned v5 = (v2*(256-xfrac)+v3*xfrac);
	unsigned v6 = (v4*(256-yfrac)+v5*yfrac);
	return Uint8(v6 >> 16);
}



void sky::smooth_and_equalize_bytemap(unsigned s, vector<Uint8>& map1)
{
	vector<Uint8> map2 = map1;
	unsigned maxv = 0, minv = 255;
	for (unsigned y = 0; y < s; ++y) {
		unsigned y1 = (y+s-1)%s, y2 = (y+1)%s;
		for (unsigned x = 0; x < s; ++x) {
			unsigned x1 = (x+s-1)%s, x2 = (x+1)%s;
			unsigned v = (unsigned(map2[y1*s+x1]) + unsigned(map2[y1*s+x2]) + unsigned(map2[y2*s+x1]) + unsigned(map2[y2*s+x2])) / 16
				+ (unsigned(map2[y*s+x1]) + unsigned(map2[y*s+x2]) + unsigned(map2[y1*s+x]) + unsigned(map2[y2*s+x])) / 8
				+ (unsigned(map2[y*s+x])) / 4;
			map1[y*s+x] = Uint8(v);
			if (v < minv) minv = v;
			if (v > maxv) maxv = v;
		}
	}
	for (unsigned y = 0; y < s; ++y) {
		for (unsigned x = 0; x < s; ++x) {
			unsigned v = map1[y*s+x];
			map1[y*s+x] = Uint8((v - minv)*255/(maxv-minv));
		}
	}
}



void sky::set_time(double tm)
{
	mytime = tm;
	tm = myfmod(tm, 86400.0);
	double cf = myfrac(tm/CLOUD_ANIMATION_CYCLE_TIME) - cloud_animphase;
	if (fabs(cf) < (1.0/(3600.0*256.0))) cf = 0.0;
	if (cf < 0) cf += 1.0;
	advance_cloud_animation(cf);
}



void sky::display(const game& gm, const vector3& viewpos, double max_view_dist, bool isreflection) const
{
	//fixme: for reflections this is wrong, so get LIGHT_POS!
	vector3 sundir = gm.compute_sun_pos(viewpos).normal();
	color lightcol = gm.compute_light_color(viewpos);

	//fixme: check if all push and pops match!

	glPushMatrix();

	// 1) draw the stars on a black background

	// skyplanes:
	// 2) blue color, shading to grey haze to the horizon	(tex0, blend into background)
	// 3) sun glow						(tex1, added)
	// 4) clouds layer					(separate faces)
	// 5) sun lens flares					(later, added, one quad)
	// draw: time (night/day) changes blend factor for 2, maybe 3
	// blend factor may be set via global color alpha
	// set up texture matrix for texture unit 0 (x translation for weather conditions,
	// x in [0,1] means sunny...storm)
	// sun glow (it moves) must come from a texture

	glDisable(GL_LIGHTING);
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);

	// the brighter the sun, the deeper is the sky color
	float atmos = (sundir.z < -0.25) ? 0.0f : ((sundir.z < 0.0) ? 4*(sundir.z+0.25) : 1.0f);

	skycolorfac = (sundir.z > 0.5) ? 0.0 : (1.0 - sundir.z * 2);

	// if stars are drawn after the sky, they can appear in front of the sun glow, which is wrong.
	// sky should be blended into the stars, not vice versa, but then we would have to clear
	// the back buffer, that takes 2 frames... DST_ALPHA could be used IF sky stores its alpha

	// because the reflection map may have a lower resolution than the screen
	// we shouldn't draw stars while computing this map. They would appear to big.
	// in fact star light is to weak for water reflections, isn't it?
	// fixme: maybe rotate star positions every day a bit? rotate with earth rotation? that would be more realistic/cooler
	if (isreflection) {
		// no stars, blend sky into black
		glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
	} else {
		glPushMatrix();
		glScalef(max_view_dist * 0.95, max_view_dist * 0.95, max_view_dist * 0.95);
		glBindTexture(GL_TEXTURE_2D, 0);
		glEnableClientState(GL_COLOR_ARRAY);
	        glColorPointer(4, GL_UNSIGNED_BYTE, 0, &stars_lumin[0]);
        	glEnableClientState(GL_VERTEX_ARRAY);
	        glVertexPointer(3, GL_FLOAT, sizeof(vector3f), &stars_pos[0].x);
		glDrawArrays(GL_POINTS, 0, stars_pos.size());
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
		glPopMatrix();
	}

	glColor4f(1, 1, 1, atmos);
	setup_textures();

	glPushMatrix();
	// with scal=0.1 we draw a sky hemisphere with 3km radius. We don't write to
	// the z-buffer, so any scale within znear and zfar is ok. To avoid clipping of
	// parts of the sky hemisphere with the zfar plane, we scale it to a much smaller
	// size than 30km=zfar.
	double scal = 0.1;//max_view_dist / 30000.0;	// sky hemisphere is stored as 30km in radius
	glScaled(scal, scal, scal);

	// set texture coordinate translation for unit 1 (sunglow), why this isn't in setup_tex?! fixme
	double suntxr = ((sundir.z > 1.0) ? 0.0 : acos(sundir.z)) / M_PI;
	double suntxa = atan2(sundir.y, sundir.x);
	glActiveTexture(GL_TEXTURE1);
	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glTranslated(-cos(suntxa)*suntxr, -sin(suntxa)*suntxr, 0.0);
	glMatrixMode(GL_MODELVIEW);
	glActiveTexture(GL_TEXTURE0);	

	// ********* set up sky textures and call list
	glCallList(skyhemisphere_dl);
	
	color::white().set_gl_color();
	
	cleanup_textures();
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPopMatrix();	// remove scale

	// ******** the sun and the moon *****************************************************
	// draw sun, fixme draw flares/halo
	vector3 sunpos = sundir * (0.96 * max_view_dist);
	double suns = max_view_dist/100;		// make sun ~13x13 pixels
	glColor4f(1,1,1,0.25);
	suntex->set_gl_texture();
	glPushMatrix();
	glTranslated(sunpos.x, sunpos.y, sunpos.z);
	matrix4 tmpmat = matrix4::get_gl(GL_MODELVIEW_MATRIX);
	tmpmat.clear_rot();
	tmpmat.set_gl(GL_MODELVIEW);
	glBegin(GL_QUADS);
	glTexCoord2f(0,0);
	glVertex3f(-suns, -suns, 0);
	glTexCoord2f(1,0);
	glVertex3f( suns, -suns, 0);
	glTexCoord2f(1,1);
	glVertex3f( suns,  suns, 0);
	glTexCoord2f(0,1);
	glVertex3f(-suns,  suns, 0);
	glEnd();
	glPopMatrix();

	// set light position (sun position when sun is above horizon, else moon position, fixme)
	if (sunpos.z > 0.0) {
		GLfloat sunposgl[4] = { sunpos.x, sunpos.y, sunpos.z, 0.0f };
		glLightfv(GL_LIGHT0, GL_POSITION, sunposgl);
	}

	// draw moon
	vector3 moonpos = gm.compute_moon_pos(viewpos).normal() * (0.95 * max_view_dist);
	double moons = max_view_dist/120;	// make moon ~10x10 pixel
	glColor4f(1,1,1,1);
	moontex->set_gl_texture();
	glPushMatrix();
	glTranslated(moonpos.x, moonpos.y, moonpos.z);
	tmpmat = matrix4::get_gl(GL_MODELVIEW_MATRIX);
	tmpmat.clear_rot();
	tmpmat.set_gl(GL_MODELVIEW);
	glBegin(GL_QUADS);
	glTexCoord2f(0,0);
	glVertex3f(-moons, -moons, 0);
	glTexCoord2f(1,0);
	glVertex3f( moons, -moons, 0);
	glTexCoord2f(1,1);
	glVertex3f( moons,  moons, 0);
	glTexCoord2f(0,1);
	glVertex3f(-moons,  moons, 0);
	glEnd();
	glPopMatrix();


	// ******** clouds ********************************************************************
	lightcol.set_gl_color();	// cloud color depends on day time

	// fixme: cloud color varies with direction to sun (clouds aren't flat, but round, so
	// border are brighter if sun is above/nearby)
	// also thin clouds appear bright even when facing away from the sun (sunlight
	// passes through them, diffuse lighting in a cloud, radiosity).
	// Dynamic clouds are nice, but "real" clouds (fotographies) look much more realistic.
	// Realistic clouds can be computed, but the question is, how much time this would take.
	// Fotos are better, but static...
	// fixme: add flares after cloud layer (?)

	glScalef(3000, 3000, 333);	// bottom of cloud layer has altitude of 3km., fixme varies with weather
	clouds->set_gl_texture();
	glCallList(clouds_dl);

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
	color::white().set_gl_color();

	glPopMatrix();
}



color sky::get_horizon_color(const game& gm, const vector3& viewpos) const
{
	// fixme: same colors as above, store them at unique place!
	vector3 sundir = gm.compute_sun_pos(viewpos).normal();
	float daytimefac1 = (sundir.z > 0.5) ? 0.0 : (1.0 - sundir.z * 2);
	float daytimefac2 = (sundir.z < -0.25) ? 0.0f : ((sundir.z < 0.0) ? 4*(sundir.z+0.25) : 1.0f);
	return color(color(0, 0, 0), color(color(173, 200, 219), color(231, 166, 89), daytimefac1), daytimefac2);
}
