// SDL/OpenGL based textures
// (C)+(W) by Thorsten Jordan. See LICENSE

#ifndef TEXTURE_H
#define TEXTURE_H

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <SDL.h>

#include <gl.h>

#include <vector>
#include <string>
using namespace std;

class texture
{
protected:
	unsigned opengl_name, width, height;
	string texfilename;
	int format;		// GL_RGB, GL_RGBA, etc.
	int mapping;		// how GL draws the texture (GL_NEAREST, GL_LINEAR, etc.)
	vector<Uint8> data;	// texture data (only stored if user wishes that)
	
	texture() {};
	texture& operator=(const texture& other) { return *this; };
	texture(const texture& other) {};
	
	// share common constructor code
	void init(SDL_Surface* teximage, unsigned sx, unsigned sy, unsigned sw, unsigned sh,
		int clamp, bool keep);

public:
	texture(const string& filename, int mapping_ = GL_NEAREST, int clamp = GL_REPEAT, bool keep = false);

	// create texture from subimage of SDL surface.
	// sw,sh need not to be powers of two.
	texture(SDL_Surface* teximage, unsigned sx, unsigned sy, unsigned sw, unsigned sh,
		int mapping_ = GL_NEAREST, int clamp = GL_REPEAT, bool keep = true) {
			mapping = mapping_;
			init(teximage, sx, sy, sw, sh, clamp, keep);
		};

	// create texture from memory values (use openGl constants for format,etc.
	// w,h must be powers of two.
	// you may give a NULL pointer to pixels, the texture will then be inited with black.
	texture(Uint8* pixels, unsigned w, unsigned h, int format_,
		int mapping_, int clamp, bool keep = true);

	// create a RGB texture with normal values from heights (0-255, grey values)
	// give height of details, 1.0 = direct values
	static texture* make_normal_map(Uint8* heights, unsigned w, unsigned h, float detailh,
				 int mapping, int clamp);
	
	~texture();
	
	// (re)creates OpenGL texture from stored data
	void update(void);
	int get_format(void) const { return format; }
	unsigned get_bpp(void) const;
	vector<Uint8>& get_data(void) { return data; }

	unsigned get_opengl_name(void) const { return opengl_name; };
	void set_gl_texture(void) const;
	string get_name(void) const { return texfilename; };
	unsigned get_width(void) const { return width; };
	unsigned get_height(void) const { return height; };

	// 2d drawing must be turned on for this functions
	void draw(int x, int y) const;
	void draw_hm(int x, int y) const;	// horizontally mirrored
	void draw_vm(int x, int y) const;	// vertically mirrored
	void draw(int x, int y, int w, int h) const;
	void draw_hm(int x, int y, int w, int h) const;	// horizontally mirrored
	void draw_vm(int x, int y, int w, int h) const;	// vertically mirrored
	// draw rotated image around x,y (global)
	void draw_rot(int x, int y, double angle) const;
	// draw rotated image around x,y (global), rotate around local tx,ty
	void draw_rot(int x, int y, double angle, int tx, int ty) const;
	// repeat texture in tiles in the given screen rectangle
	void draw_tiles(int x, int y, int w, int h) const;
	// draw a sub image given by the tx,ty,tw,th values to a screen rectangle x,y,w,h
	void draw_subimage(int x, int y, int w, int h, unsigned tx, unsigned ty,
		unsigned tw, unsigned th) const;

	static unsigned get_max_size(void);

	// shader handling.
	// give GL_FRAGMENT_PROGRAM_ARB or GL_VERTEX_PROGRAM_ARB as type
	static GLuint create_shader(GLenum type, const string& filename);
	static void delete_shader(GLuint nr);
};

#endif
