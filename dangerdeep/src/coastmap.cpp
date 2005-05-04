// a coastmap
// subsim (C)+(W) Thorsten Jordan. SEE LICENSE

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "oglext/OglExt.h"

#include "coastmap.h"
#include "binstream.h"
#include "global_data.h"
#include "texture.h"
#include "system.h"
#include "triangulate.h"
#include "tinyxml/tinyxml.h"
#include <SDL_image.h>
#include <fstream>
#include <list>
#include <vector>
using namespace std;



const unsigned BSPLINE_SMOOTH_FACTOR = 16;//3;//16;	// should be 3...16
const double BSPLINE_DETAIL = 4.0;            // should be 1.0...x
const unsigned SEGSCALE = 65535;	// 2^16-1 so that per segment coordinates fit in a ushort value.

// order (0-3):
// 32
// 01
const int coastmap::dmx[4] = { -1,  0, 0, -1 };
const int coastmap::dmy[4] = { -1, -1, 0,  0 };
// order: left, down, right, up
const int coastmap::dx[4] = {  0, 1, 0,-1 };
const int coastmap::dy[4] = { -1, 0, 1, 0 };

bool coastmap::patternprocessok[16] = { 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0 };


/*
segcls are connected in ccw order.
vertices are stored in ccw order.
this means when iterating along the coast, land is left and sea is right (ccw).
lakes are not allowed (when they're completely inside a seg, because the
triangulation failes there).
borders are stored from 0-3 in ccw order: bottom,right,top,left.

the following patterns are possible when searching coastlines (0,15 illegal)

 0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
..  ..  ..  ..  .#  .#  .#  .#  #.  #.  #.  #.  ##  ##  ##  ##  32
..  #.  .#  ##  ..  #.  .#  ##  ..  #.  .#  ##  ..  #.  .#  ##  01

*/

// -1 illegal,0-3 down,right,up,left:    0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
const int coastmap::runlandleft[16]  = {-1, 3, 0, 3, 1,-1, 0, 3, 2, 2,-1, 2, 1, 1, 0,-1};
const int coastmap::runlandright[16] = {-1, 0, 1, 1, 2,-1, 2, 2, 3, 0,-1, 1, 3, 0, 3,-1};

#if 0
void coastline::create_points(vector<vector2>& points, float begint, float endt, int detail) const
{
	sys().myassert(begint < endt, "error: begint >= endt (%f %f)", begint, endt);

	// compute number of points to be generated
	unsigned totalpts = curve.control_points().size();
	unsigned nrpts = unsigned(round(float(totalpts) * (endt-begint)));
	nrpts = (nrpts << detail);
	if (nrpts < 2) nrpts = 2;

	points.reserve(points.size() + nrpts);
	float t = begint, tstep = (endt - begint) / float(nrpts - 1);
	for (unsigned i = 0; i < nrpts; ++i) {
		if (t > 1.0f) t = 1.0f;	// fixme: can this happen? only for last point?
		points.push_back(curve.value(t));
		t += tstep;
	}
}



void coastline::draw_as_map(int detail) const
{
	glBindTexture(GL_TEXTURE_2D, 0);
	glBegin(GL_LINE_STRIP);
	int z = 0;
	for (vector<vector2>::const_iterator it = curve.control_points().begin(); it != curve.control_points().end(); ++it) {
		float p = float(z % 8 + 1)/8;
		float p2 = float(z % 4 + 1)/4;
		glColor3f(0,p,p2);
		glVertex2f(it->x, it->y);
		++z;
	}
	glEnd();
	glColor3f(1,1,1);
}
#endif


#if 0
void coastline::render(const vector2& p, int detail) const
{
	// fixme: cache that somehow
	vector<vector2> pts = create_points(p, 1, 0, points.size(), detail);
	
	glPushMatrix();
	glTranslatef(-p.x, -p.y, 0);
	
	unsigned ps = pts.size();
	glBegin(GL_QUAD_STRIP);
	double coastheight = 50;
	float t = 0.0;
	for (unsigned s = 0; s < ps; ++s) {
		const vector2& p = pts[s];
		unsigned prevpt = (s > 0) ? s-1 : s;
		unsigned nextpt = (s < ps-1) ? s+1 : s;
		vector2 n = (pts[nextpt] - pts[prevpt]).orthogonal().normal() * 50.0;
		if (s == 0) {
			glTexCoord2f(t, 1);
			glVertex3f(p.x, p.y, -10);
			glTexCoord2f(t, 0);
			glVertex3f(p.x+n.x, p.y+n.y, coastheight);
		} else {
			glTexCoord2f(t, 0);	// texture coordinates are wrong, fixme
			glVertex3f(p.x+n.x, p.y+n.y, coastheight);
			glTexCoord2f(t, 1);
			glVertex3f(p.x, p.y, -10);
		}
		t += 1.0;
	}
	glEnd();
	
	glPopMatrix();
}
#endif



void coastsegment::segcl::push_back_point(const coastsegment::segpos& sp)
{
	if (points.empty() || !(points.back() == sp))
		points.push_back(sp);
}



// the least distance between two points is around 0.014, fixme test again
void coastsegment::cacheentry::push_back_point(const vector2& p)
{
	if (points.size() > 0) {
		double d = points.back().square_distance(p);
		if (d < 1.0f) return;//fixme test hack
		sys().myassert(d >= 1.0f, "error: points are too close %f    %f %f  %f %f", d, points.back().x, points.back().y, p.x, p.y);
	}
	points.push_back(p);
}



void coastsegment::generate_point_cache(const class coastmap& cm, int x, int y, int detail) const
{
	if (type > 1) {
		// cache generated and unchanged?
		if (pointcache.size() > 0 && pointcachedetail == detail) return;

		//	cout << "creating cache entry for segment " << roff << " empty? " << (pointcache.size() > 0) << " cached detail " << pointcachedetail << " new detail " << detail << "\n";
		// invalidate cache (detail changed or initial generation)
		pointcachedetail = detail;
		pointcache.clear();

		vector2 segoff(x*cm.segw_real + cm.realoffset.x, y*cm.segw_real + cm.realoffset.y);

		unsigned nrcl = segcls.size();
		vector<bool> cl_handled(nrcl, false);
		for (unsigned i = 0; i < nrcl; ++i) {
			if (cl_handled[i]) continue;

			cacheentry ce;

			// find area
			unsigned current = i;
			do {
				cout << "segpos: x " << x << " y " << y << "\n";
				cout << "current is : " << current << "\n";
				segcls[current].print();
				//erzeugt, weil endpos falsch ist, border pos. von altem segment
				//insel hat endpos=-1, obwohl sie segmentgrenze schneidet! startpos aber !=-1
				//scheint beides jetzt behoben. Triangulierungsfehler kommen daher, da�
				//Strecken in segcls vorkommen, die komplett auf dem rand liegen.
				//diese m��ten zu DEM segment geh�ren, das land enth�lt.
				//ein solcher �bergang m��te dann als segmentwechsel gehandhabt werden
				//if (cl_handled[current]) break;//test hack fixme
				if (cl_handled[current]) return;//test hack fixme
				sys().myassert(!cl_handled[current], "illegal .next values!");
				/* NOTE:
				   doubles shouldn't occour by design. But they do. So we do
				   an quick, simple and dirty workaround here and just avoid
				   inserting them. Finally we check if the last point is the
				   same as the first, in that case we remove it, too.
				   Reason for double points:
				   this seem to happen because sometimes endborder/beginborder
				   is >= 0 even if the corresponding points are not on the border.
				   so the coast lines are treated as coast, not as part of an
				   island -> end of first cl is equal to begin of next ->
				   double points result.
				   2004/05/28: fixme: is this because we take beginborder/endborder
				   values from the cl for island? (illegal combination
				   of island/non-island pair for beginborder/endborder)???
				*/
				const segcl& cl = segcls[current];
				ce.points.reserve(ce.points.size() + cl.points.size());
				for (unsigned j = 0; j < cl.points.size(); ++j) {
					// avoid double points here... fixme. they're removed later. see below
					ce.points.push_back(cm.segcoord_to_real(x, y, cl.points[j]));
				}
				int next = cl.next;
				cout << "startpos " << cl.beginpos << " endpos: " << cl.endpos << " next " << next << " next startpos: " << segcls[next].beginpos << "\n";
				//cout << "current="<<current<<" next="<<next<<"\n";
				cl_handled[current] = true;
				sys().myassert(next!=-1, "next unset?");//fixme: another unexplainable bug.
				//if(current==next)break;
				// insert corners if needed
				if (!cl.cyclic) {
					int b0 = cl.endpos, b1 = segcls[next].beginpos;
					cout << "fill: ed " << b0 << " bg " << b1 << "\n";
					if (b1 < b0) b1 += 4 * SEGSCALE;
					b0 = (b0 + SEGSCALE - 1) / SEGSCALE;
					b1 = b1 / SEGSCALE;
					cout << "fill2: ed " << b0 << " bg " << b1 << "\n";
					for (int j = b0; j <= b1; ++j) {
						int k = j % 4;
						// push back destination point of edge (0-3: br,tr,tl,bl)
						if (k == 0) ce.push_back_point(segoff);
						else if (k == 1) ce.push_back_point(segoff + vector2(cm.segw_real, 0));
						else if (k == 2) ce.push_back_point(segoff + vector2(cm.segw_real, cm.segw_real));
						else /*if (k == 3)*/ ce.push_back_point(segoff + vector2(0, cm.segw_real));
					}
				}
				current = unsigned(next);
			} while (current != i);

			//			cout << "compute triang for segment " << this << "\n";
			
			// fixme: 2004/05/30:
			// the remaining triangulation bugs have two reasons:
			// 1. double points (hack-fix with this code)
			// 2. "flat" polygons (degenerated), which can't be triangulated
			// we have 11 failed triangulations and with double point removal
			// only 6, all degenerated polygon cases.
			// this gives a clue to fix the triangulation problems finally...
			// check which segment fails to triangulate and check the map
			// image what case it is.
			// first case: north of iceland: the coastline lies exactly on a
			// segment border!!!
			cout << "segment no: " << (this - &cm.coastsegments[0]) << "\n";
			unsigned dbl = 0;
			for (unsigned i = 0; i + 1 < ce.points.size(); ++i) {
				unsigned j = i+1;
				if (ce.points[j].square_distance(ce.points[i]) < 0.1f) {
					ce.points.erase(ce.points.begin()+j);
					--i;
					++dbl;
				}
			}
			if (dbl > 0) cout << "erased " << dbl << " double points!\n";
			
			ce.indices = triangulate::compute(ce.points);
			pointcache.push_back(ce);
		}
	}
}



void coastsegment::draw_as_map(const class coastmap& cm, int x, int y, int detail) const
{
//cout<<"segment draw " << x << "," << y << " dtl " << detail << " tp " << type << "\n";
	// fixme, use display lists here
	if (type == 1) {
		vector2 roff(x*cm.segw_real + cm.realoffset.x, y*cm.segw_real + cm.realoffset.y);
		vector2f tc0 = cm.segcoord_to_texc(x, y, segpos(0, 0));
		vector2f tc1 = cm.segcoord_to_texc(x, y, segpos(SEGSCALE, SEGSCALE));
		glBegin(GL_QUADS);
		glTexCoord2f(tc0.x, tc0.y);
		glVertex2d(roff.x, roff.y);
		glTexCoord2f(tc1.x, tc0.y);
		glVertex2d(roff.x+cm.segw_real, roff.y);
		glTexCoord2f(tc1.x, tc1.y);
		glVertex2d(roff.x+cm.segw_real, roff.y+cm.segw_real);
		glTexCoord2f(tc0.x, tc1.y);
		glVertex2d(roff.x, roff.y+cm.segw_real);
		glEnd();
	} else if (type > 1) {
#if 1
		generate_point_cache(cm, x, y, detail);
		glBegin(GL_TRIANGLES);
		for (vector<cacheentry>::const_iterator cit = pointcache.begin(); cit != pointcache.end(); ++cit) {
			for (vector<unsigned>::const_iterator tit = cit->indices.begin(); tit != cit->indices.end(); ++tit) {
				vector2f tc;// = cm.segcoord_to_texc(x, y, segpos(0, 0 /*fixme*/));
				const vector2& v = cit->points[*tit];
				tc.x = (v.x - cm.realoffset.x)/cm.realwidth;
				tc.y = 1.0 - (v.y - cm.realoffset.y)/cm.realheight;//fixme: performance waste!
				glTexCoord2f(tc.x, tc.y);
				glVertex2f(v.x, v.y);
			}
		}
		glEnd();
#endif		
#if 0
		// test
//		glBindTexture(GL_TEXTURE_2D, 0);
		glBegin(GL_LINES);
		for (unsigned i = 0; i < segcls.size(); ++i) {
			vector2 p0 = cm.segcoord_to_real(x, y, segcls[i].points.front());
			vector2 p1 = cm.segcoord_to_real(x, y, segcls[i].points.back());
			glColor3f(1.0f, 1.0f, 0.0f);
			glVertex2f(p0.x, p0.y);
			glColor3f(1.0f, 0.0f, 1.0f);
			glVertex2f(p1.x, p1.y);
		}
		glEnd();
		glColor3f(1,1,1);
#endif
	}
}



//fixme:
//cache results.
//remove unnecessary flat quads in the distance/don't generate them
//introduces LOD.
//many more effects...
#include <sstream>
void coastsegment::render(const class coastmap& cm, int x, int y, const vector2& p, int detail) const
{
#if 0
	unsigned res = cm.pixels_per_seg;
	res = (res << detail);
	vector<vector3f> coords;
	coords.reserve((res+1)*(res+1));
	vector<vector2> uv0;
	uv0.reserve((res+1)*(res+1));
	vector2 segoff(x*cm.segw_real + cm.realoffset.x, y*cm.segw_real + cm.realoffset.y);
	for (unsigned yy = 0; yy <= res; ++yy) {
		for (unsigned xx = 0; xx <= res; ++xx) {
			float s = float(xx)/res;
			float t = float(yy)/res;
			float h = topo.value(s, t);
			coords.push_back(vector3f(segoff.x + s*cm.segw_real, segoff.y + t*cm.segw_real, h));
			uv0.push_back(vector2(s, t));
		}
	}
	vector<unsigned> indices;
	indices.reserve(res*res*4);
	for (unsigned yy = 0; yy < res; ++yy) {
		for (unsigned xx = 0; xx < res; ++xx) {
			indices.push_back(yy*(res+1)+xx);
			indices.push_back(yy*(res+1)+xx+1);
			indices.push_back((yy+1)*(res+1)+xx+1);
			indices.push_back((yy+1)*(res+1)+xx);
		}
	}
	
/*
	ostringstream oss;
	oss << "test_render_" << x << "_" << y << ".off";
	ofstream out(oss.str().c_str());
	out << "OFF\n" << coords.size() << " " << indices.size()/2 << " 0\n";
	for (unsigned i = 0; i < coords.size(); ++i)
		out << coords[i].x << " " << coords[i].y << " " << coords[i].z << "\n";
	for (unsigned i = 0; i < indices.size()/4; ++i) {
		out << "3 " << indices[i*4] << " " << indices[i*4+1] << " " << indices[i*4+3] << "\n";
		out << "3 " << indices[i*4+1] << " " << indices[i*4+3] << " " << indices[i*4+2] << "\n";
	}
*/
	
	glPushMatrix();

	terraintex->set_gl_texture();
	glColor4f(1,1,1,1);
	glNormal3f(0,0,1);//fixme
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(vector3f), &coords[0].x);
	glClientActiveTexture(GL_TEXTURE0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof(vector2), &uv0[0].x);
	glDrawElements(GL_QUADS, indices.size(), GL_UNSIGNED_INT, &(indices[0]));
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glBindTexture(GL_TEXTURE_2D, 0);
	
	glPopMatrix();

/*
	if (type > 1) {
		for (vector<coastline>::const_iterator it = coastlines.begin(); it != coastlines.end(); ++it) {
			it->render(p, detail);
		}
	}
*/
#endif
}



// return position on segment border
int coastmap::borderpos(const coastsegment::segpos& p) const
{
	if (p.y == 0) return p.x;
	else if (p.x == SEGSCALE) return SEGSCALE + p.y;
	else if (p.y == SEGSCALE) return 2*SEGSCALE + SEGSCALE - p.x;
	else if (p.x == 0) return 3*SEGSCALE + SEGSCALE - p.y;
	else return -1;
}



void coastsegment::segcl::print() const
{
	/*
	  cout << "segcl:\nmapclnr: " << mapclnr << " begint " << begint << " endt " << endt
	  << "\nbeginp: " << beginp << " endp: " << endp
	  << "\nbeginborder: " << beginborder << " endborder " << endborder
	  << "\ncyclic? " << cyclic << " next: " << next << "\n";
	*/
	cout << "segcl:\n" << points.size() << " points, beginpos " << beginpos << " beginb "
	     << " beginb " << ((beginpos == -1) ? -1 : beginpos / int(SEGSCALE)) << " endpos "
	     << endpos << " endb " << ((endpos == -1) ? -1 : endpos / int(SEGSCALE)) << " next "
	     << next << " cyclic? " << cyclic << " nrpts " << points.size() << " 1st pt "
	     << points.front() << ",back " << points.back() << "\n";
}

void coastsegment::compute_successor_for_cl(unsigned cln)
{
	segcl& scl0 = segcls[cln];

	// only if not already set (already set for islands contained in segments)
	if (scl0.next == -1) {
		//sys().myassert(scl0.beginpos >= 0, "paranoia bp1");
		//sys().myassert(scl0.endpos >= 0, "paranoia ep1");

		// now loop over all segcls in the segment, find minimal beginpos that
		// is greater than scl0.endpos
		int minbeginpos = 8*SEGSCALE; // +inf
		for (unsigned i = 0; i < segcls.size(); ++i) {
			// i's successor can be i itself, when i enters and leaves the seg without any
			// other cl in between
			const segcl& scl1 = segcls[i];
			int beginpos = scl1.beginpos;
			if (beginpos < scl0.endpos) beginpos += 4*SEGSCALE;
			if (beginpos < minbeginpos) {
				scl0.next = i;
				minbeginpos = beginpos;
			}
		}

		sys().myassert(scl0.next != -1, "no successor found!");
	}
}



void coastsegment::push_back_segcl(const segcl& scl)
{
	if (scl.beginpos < 0) {
		scl.print();
		sys().myassert(scl.cyclic, "begin < -1 but not cyclic");
		sys().myassert(scl.endpos < 0, "begin < -1, but not end");
	}
	if (scl.endpos < 0) {
		scl.print();
		sys().myassert(scl.cyclic, "end < -1 but not cyclic");
		sys().myassert(scl.beginpos < 0, "end < -1, but not begin");
	}
	if (scl.points.size() >= 2)
		segcls.push_back(scl);
}



//
// coastmap functions
//

Uint8& coastmap::mapf(int cx, int cy)
{
	cx = clamp_zero(cx);
	cy = clamp_zero(cy);
	cx = mapw - 1 - clamp_zero(mapw - 1 - cx);
	cy = maph - 1 - clamp_zero(maph - 1 - cy);
	return themap[cy*mapw+cx];
}



int coastmap::find_begin_of_coastline(int& x, int& y)
{
	int sx = x, sy = y;
	int lastborder_x = -1, lastborder_y = -1;
	cout <<" find beg of cl " << sx << "," << sy << "\n";
	// loop until we step on a map border, with land left of it.
	int olddir = -1, turncount = 0;
	while (true) {
		// compute next x,y
		int dir = -1;

		Uint8 pattern = 0;
		for (int j = 0; j < 4; ++j) {
			pattern |= (mapf(x+dmx[j], y+dmy[j]) & 0x7f) << j;
		}

		if (olddir == -1)
			sys().myassert(pattern != 5 && pattern != 10, "illegal start pattern!");

		if (patternprocessok[pattern] && (x % pixels_per_seg == 0 || y % pixels_per_seg == 0)) {
			lastborder_x = x;
			lastborder_y = y;
		}

		// mirrored direction to find_coastline
		if (pattern == 10) {
			// check pattern:
			// #.
			// .#
			// direction: only 1,3 valid. 0->1, 2->3
			sys().myassert(olddir == 0 || olddir == 2, "olddir illegal 1 (%i)",olddir);
			dir = olddir + 1;
		} else if (pattern == 5) {
			// check pattern:
			// .#
			// #.
			// direction: only 0,2 valid. 3->0, 1->2
			sys().myassert(olddir == 3 || olddir == 1, "olddir illegal 2 (%i)",olddir);
			dir = (olddir + 1) % 4;
		} else {
			// check other patterns:
			dir = runlandright[pattern];
			sys().myassert(dir != -1, "dir illegal 1");
		}

		// turncount
		if (olddir != -1) {
			int t = (dir - olddir + 4) % 4;
			sys().myassert(t != 2, "no 180 degree turns allowed!");
			if (t == 3) t = -1;
			// positive values are ccw turns.
			turncount += t;
		}
		olddir = dir;

		int nx = x + dx[dir];
		int ny = y + dy[dir];
		// if we left the border, stop search.
		if (nx < 0 || ny < 0 || nx > int(mapw) || ny > int(maph)) {
			sys().myassert(pattern != 5 && pattern != 10, "illegal start pattern!3");
			break;
		}
		x = nx;
		y = ny;
		//cout << "x/y now " << x << "," << y << "\n";
		if (sx == x && sy == y) {
			if (lastborder_x != -1) {
			//fixme: das kann dazu f�hren, da� begin auf x. .x pattern gesetzt wird,
			//ab da ist startrichtung undefiniert!!!
			//generell bricht das hier ab, wenn auf einem solchen pattern begonnen wird und ne
			//ein teil des patterns quasi eine insel ist!
			//also verbieten, da� findbegcl auf diesem pattern begonnen wird.
			//wenn wird auf so einem pattern sind, und das als startpos gespeichert wird,
			//dann sage auch die richtung, in die man loslaufen mu� (gegenrichtung der richtung,
			//mit der findbegcl das pattern erreicht hat!)
				//jetzt nicht mehr, fixme
				x = lastborder_x;
				y = lastborder_y;
			}
			// kann +- 3 sein, wenn knick auf startpunkt
			//sys().myassert(turncount == 4 || turncount == -4, "island but turn count != +-4? %i", turncount);
			return (turncount < 0) ? -1 : 1;	// island found
		}
	}
	sys().myassert(turncount >= -3 && turncount <= 3, "loop without island?? %i", turncount);
	return 0;
}



// returns true if cl is valid
bool coastmap::find_coastline(int x, int y, vector<vector2i>& points, bool& cyclic)
{
	// run backward at the coastline until we reach the border or round an island.
	// start there creating the coastline. this avoids coastlines that can never be seen.
	// In reality: north pole, ice, America to the west, Asia/africa to the east.
	// generate points in ccw order, that means land is left, sea is right.

	sys().myassert((mapf(x, y) & 0x80) == 0);
	
	int beginok = find_begin_of_coastline(x, y);
	if (beginok < 0) return false;
	cyclic = (beginok > 0);

	int sx = x, sy = y;
	int olddir = -1, turncount = 0;
	while (true) {
		// store x,y
		points.push_back(vector2i(x, y));

		sys().myassert(x>=0&&y>=0&&x<=int(mapw)&&y<=int(maph));//fixme testweise von < auf <= gestellt
		mapf(x, y) |= 0x80;

		// compute next x,y
		int dir = -1;

		Uint8 pattern = 0;
		for (int j = 0; j < 4; ++j) {
			pattern |= (mapf(x+dmx[j], y+dmy[j]) & 0x7f) << j;
		}

		if (olddir == -1)
			sys().myassert(pattern != 5 && pattern != 10, "illegal start pattern!2");

		if (pattern == 10) {
			// check pattern:
			// #.
			// .#
			// direction: only 0,2 valid. 1->0, 3->2
			sys().myassert(olddir == 1 || olddir == 3, "olddir illegal 3 (%i)",olddir);
			dir = olddir - 1;
		} else if (pattern == 5) {
			// check pattern:
			// .#
			// #.
			// direction: only 1,3 valid. 2->1, 0->3
			sys().myassert(olddir == 2 || olddir == 0, "olddir illegal 4 (%i)",olddir);
			dir = (olddir + 3) % 4;
		} else {
			// check other patterns:
			dir = runlandleft[pattern];
			sys().myassert(dir != -1, "dir illegal 2");
		}

		// turncount
		if (olddir != -1) {
			int t = (dir - olddir + 4) % 4;
			sys().myassert(t != 2, "no 180 degree turns allowed!");
			if (t == 3) t = -1;
			// positive values are ccw turns.
			turncount += t;
		}
		olddir = dir;

		int nx = x + dx[dir];
		int ny = y + dy[dir];
//		printf("x %i y %i nx %i ny %i\n",x,y,nx,ny);
//		printf("pattern %02x%02x\npattern %02x%02x\n",mv[3],mv[2],mv[0],mv[1]);
		if (nx < 0 || ny < 0 || nx > int(mapw) || ny > int(maph)) // border reached
			break;
		x = nx;
		y = ny;
		if (sx == x && sy == y) {
			break;	// island found
		}
	}

cout<<"TURNCOUNT is "<<turncount<<"\n";	
	return (!cyclic) || (turncount <= 0);
}



void coastmap::divide_and_distribute_cl(const vector<vector2i>& cl, bool clcyclic)
{
	coastsegment::segcl scl;

	// divide coastline at segment borders
	// find segment that first point is into
	vector2i p0(cl[0].x, cl[0].y);
	vector2i segc(p0.x / SEGSCALE, p0.y / SEGSCALE);
	segc = segc.min(vector2i(segsx-1, segsy-1));		// segment coordinate
	int segcn = segc.y * segsx + segc.x;
	vector2i segoff = segc * SEGSCALE;			// segment offset
	vector2i segend = segoff + vector2i(SEGSCALE, SEGSCALE); // last coordinates IN segment
	vector2i rel = p0 - segoff;
	coastsegment::segpos ps0((unsigned short)rel.x, (unsigned short)rel.y);
	scl.push_back_point(ps0);
	scl.beginpos = borderpos(ps0);

	const vector2i segdelta[4] = { vector2i(0,-1), vector2i(1,0), vector2i(0,1), vector2i(-1,0) };

	bool sameseg = true;
	// loop over coastline
	cout << "new distri!\n";
	cout << "p0: " << p0 << " ps0 " << ps0.x << "," << ps0.y << " beginpos " << scl.beginpos << "\n";
	for (unsigned i = 1; i < cl.size(); ) {
		cout << "i is " << i << "/" << cl.size() << " p0 " << p0 << " cli " << cl[i] << " cli-1 "
		     << cl[i-1] << "\n";
		sys().myassert(!(cl[i] == p0), "paranoia double bspli");
		//fixme: add special case here: p0 and cl[i] are on the same border, then the line
		//p0 to cl[i] should fall in the segment which is on the left (=land) of that line.
		//this avoids triangulation faults.
		// check if point is in same segment
		if (cl[i].x >= segoff.x && cl[i].x <= segend.x &&
		    cl[i].y >= segoff.y && cl[i].y <= segend.y) {
			cout << "segc : " << segc << " segc2 : " << segc << "\n";
			cout << " p0 : " << p0 << " cli " << cl[i] << "\n";
			vector2i rel = cl[i] - segoff;
			coastsegment::segpos psi((unsigned short)rel.x, (unsigned short)rel.y);
			sys().myassert(!(scl.points.back() == psi), "trying to add double point1!");
			scl.push_back_point(psi);
			ps0 = psi;
			p0 = cl[i];
			++i;
		} else {
			// no, not in same segment. so compute intersection of cp0->cpi with segment borders
			// compute which segment cpi is in.
			sameseg = false;
			// two cases: either line from p0->cl[i] cuts segment border (or corners)
			// or p0 lies on the border (or corner).
			if ((p0.x % SEGSCALE) == 0 || (p0.y % SEGSCALE) == 0) {
				// p0 is on a segment border. choose new segment
				// p0 has already been added to the segcl.
				// determine border that p0 is on, set endpos accordingly.
				//-----------fixme: choose seg so that land is left of line!
				scl.endpos = borderpos(ps0);
				//if (scl.endpos == -1) return;//test hack fixme
				cout << "cli is " << cl[i] << "\n";
				cout << "p0: " << p0 << " ps0 " << ps0.x << "," << ps0.y << " endpos " << scl.endpos << " segoff " << segoff << "\n";
				sys().myassert(scl.endpos != -1, "borderpos check1");
				// avoid segcl's with < 2 points.
				// just avoid adding double points. segcl's crossing corners then
				// only have one point and get discarded.
				coastsegments[segcn].push_back_segcl(scl);
				scl = coastsegment::segcl();
				// compute segcn with seg of cl[i]
				segc = vector2i(cl[i].x / SEGSCALE, cl[i].y / SEGSCALE);
				segc = segc.min(vector2i(segsx-1, segsy-1));		// segment coordinate
				segcn = segc.y * segsx + segc.x;
				segoff = segc * SEGSCALE;			// segment offset
				segend = segoff + vector2i(SEGSCALE, SEGSCALE); // last coordinates IN segment
				rel = p0 - segoff;
				ps0 = coastsegment::segpos((unsigned short)rel.x, (unsigned short)rel.y);
				scl.push_back_point(ps0);
				scl.beginpos = borderpos(ps0);
				//if (scl.beginpos == -1) return;//test hack fixme
				cout << "p0: " << p0 << " ps0 " << ps0.x << "," << ps0.y << " beginpos " << scl.beginpos << " segoff " << segoff << "\n";
				//fixme: abbruch hier, weil es eine linie gibt, die auf dem rechten rand beginnt und der zielpunkt
				//ein GANZES segment weiter links ist (rand 152/153 in x-richtung, cl[i] liegt in 151)
				//daher macht der cast von rel.x nach unsigned short aus 2*SEGSCALE = 2*(2^16-1) % 2^16 =
				//2^17-2 % 2^16 = -2 = 65534, und das ist nicht auf dem border!
				//so lange linien sollten eigentlich gar nicht erlaubt sein. ist das der anfang einer insel???
				//nein! wird da p0 auf was falsches gesetzt, durch eine berechnung vorher?
				//eher nein, schon cl[i] und cl[i-1] sind in x-richtung 80000 und in y-r. 60-70.000 entfernt
				//unglaublich, sowas darf gar net passieren!
				sys().myassert(scl.beginpos != -1, "borderpos check2");
				// repeat with same cl[i]
			} else {
				// intersection: it depends on the direction of that line.
				vector2i delta = cl[i] - p0;
				// border coordinates are segoff.x/y and segend.x/y
				double mint = 1e30;
				// find nearest intersection with border along line
				int border = -1;
				if (delta.x > 0) {
					// check right border
					double t = double(segend.x - p0.x) / delta.x;
					if (t < mint) { mint = t; border = 1; }
				} else if (delta.x < 0) {
					// check left border
					double t = double(segoff.x - p0.x) / delta.x;
					if (t < mint) { mint = t; border = 3; }
				}
				if (delta.y > 0) {
					// check top border
					double t = double(segend.y - p0.y) / delta.y;
					if (t < mint) { mint = t; border = 2; }
				} else if (delta.y < 0) {
					// check bottom border
					double t = double(segoff.y - p0.y) / delta.y;
					if (t < mint) { mint = t; border = 0; }
				}
				cout << "mint "<< mint<<" border "<<border <<"\n";
				cout << "p0: " << p0 << " cli " << cl[i] << " dekta " << delta << "\n";
				sys().myassert(border != -1, "paranoia mint");
				vector2i p1 = vector2i(int(round(p0.x + mint * delta.x)), int(round(p0.y + mint * delta.y)));
				cout << "p1 real " << double(p0.x) + mint * delta.x << "," << double(p0.y) + mint * delta.y << "\n";
				vector2i rel = p1 - segoff;
				coastsegment::segpos psi((unsigned short)rel.x, (unsigned short)rel.y);
				sys().myassert(psi.x == 0 || psi.x == SEGSCALE || psi.y == 0 || psi.y == SEGSCALE, "paranoia border");
				// if cl[i] is on a border/corner this should be handled correctly
				// by avoiding segcls with < 2 points.
				// if the segcl comes from seg x and leaves to y, but touches also segs
				// w and z, we will get segcls with one point in w and z and the right
				// segcl in y. w/z's segcls will get discarded later. So everything is right
				scl.push_back_point(psi);
				scl.endpos = borderpos(psi);
				sys().myassert(scl.endpos != -1, "borderpos check3");
				coastsegments[segcn].push_back_segcl(scl);
				// recompute segment values
				cout << "segc : " << segc << " segc2 : " << segc << "\n";
				cout << " p0 : " << p0 << " cli " << cl[i] << "\n";
				segc += segdelta[border];
				segc = segc.min(vector2i(segsx-1, segsy-1));	// segment coordinate
				segcn = segc.y * segsx + segc.x;
				cout << "i " << i << " cl.siz " << cl.size() << "\n";
				cout << "segc : " << segc << " segc2 : " << segc << "\n";
				segoff = segc * SEGSCALE;			// segment offset
				segend = segoff + vector2i(SEGSCALE, SEGSCALE); // last coordinates IN segment
				rel = p1 - segoff;
				cout << "segoff " << segoff << " rel " << rel << "\n";
				psi = coastsegment::segpos((unsigned short)rel.x, (unsigned short)rel.y);
				cout << "psi: " << psi << " segoff " << segoff << " p1 " << p1 << "\n";
				sys().myassert(psi.x == 0 || psi.x == SEGSCALE || psi.y == 0 || psi.y == SEGSCALE, "paranoia border2");
				ps0 = psi;
				p0 = p1;

				scl = coastsegment::segcl();
				scl.push_back_point(ps0);
				scl.beginpos = borderpos(ps0);
				sys().myassert(scl.beginpos != -1, "borderpos check4");
				// repeat with same cl[i]
			}
		}
	}
			
	// store remaining segment
	coastsegment::segpos cpb((unsigned short)(cl.back().x - segoff.x),
				 (unsigned short)(cl.back().y - segoff.y));
	scl.endpos = borderpos(cpb);

	scl.cyclic = (clcyclic && sameseg);
	if (scl.cyclic) scl.next = coastsegments[segcn].segcls.size(); // points to itself.
	cout << "remaining segcl has " << scl.points.size() << " points, cyclic? " << (scl.cyclic) << "\n";
	coastsegments[segcn].push_back_segcl(scl);
}



void coastmap::process_coastline(int x, int y)
{
	sys().myassert ((mapf(x, y) & 0x80) == 0);
	
	// find coastline, avoid "lakes", (inverse of islands), because the triangulation will fault there
	vector<vector2i> points;
	bool cyclic;
	bool valid = find_coastline(x, y, points, cyclic);

//	printf("coastline found from %i %i # pts %u cyclic? %u, bb %i eb %i valid %u\n",
//	       x,y,points.size(),cyclic,beginborder,endborder,valid);
	
	if (!valid) return;	// skip

	// create bspline curve
	vector<vector2> tmp;
	tmp.reserve(points.size());
	for (unsigned i = 0; i < points.size(); ++i) {
		tmp.push_back(vector2(points[i].x, points[i].y));
		cout << i << "/" << points.size() << " is " << points[i] << " / " << vector2(points[i].x, points[i].y) << "\n";
	}
	points.clear();

//	cout << "points.size()="<<points.size()<<" cyclic?"<<cyclic<<"\n";
	if (cyclic) {
		// close polygon and make sure the first and last line are linear dependent
		//fixme: problematic, cl will not begin on border any longer
		sys().myassert(tmp.size()>2);
		vector2 p0 = tmp[0];
		vector2 p1 = tmp[1];
		vector2 p01 = (p0 + p1) * 0.5;

		printf("closing coastline with trick!\n");

		// close coastline.
		tmp.push_back(tmp.front());
/* fixme.
		tmp[0] = p01;
		tmp.push_back(p0);
		tmp.push_back(p01);
*/
	}

	unsigned n = tmp.size() - 1;
	// fixme: a high n on small islands leads to a non-uniform spatial distribution of
	// bspline generated points. This looks ugly and is a serious drawback to the
	// old technique.
	// Maybe limiting n to tmp.size()/2 or tmp.size()/4 would be a solution. test it!
	if (n > BSPLINE_SMOOTH_FACTOR) n = BSPLINE_SMOOTH_FACTOR;
	if (n > tmp.size()/4) n = tmp.size()/4;

	// create smooth version of the coastline.
	bsplinet<vector2> curve(n, tmp);	// points in map pixel coordinates

	// smooth points will be scaled so that each segment is (2^16)-1 units long.
	unsigned nrpts = unsigned(tmp.size() * BSPLINE_DETAIL);
	sys().myassert(nrpts >= 2);
	tmp.clear();
	vector<vector2i> spoints;
	spoints.reserve(nrpts);
	double sscal = double(SEGSCALE) / pixels_per_seg;
	vector2 oldcv;
	cout << "generating " << nrpts << " pts\n";
	for (unsigned i = 0; i < nrpts; ++i) {
		vector2 cv = curve.value(float(i)/(nrpts-1));
		//offenbar spuckt der bspline-algo l�ngere strecken am anfang/ende der kurve aus, sogar so lange,
		//da� sie l�nger als ein segment sind.
		//die werte sind aber u.u. krass falsch, der letze liegt ganz woanders.
		//der liegt nicht mal nahe dem endpunkt, der als control-point weiter oben angegeben wird!!!
		//folglich ist der bspline-algo irgendwie falsch!		
		//der lineare-interpo.-ersatz-algo macht aber das gleiche. die letzen 3-4 st�tzpunkte
		//driften drastisch ab!
		//das ist weil die cl als cyclic behandelt wird und ein neuer endpunkt eingef�gt wird!
		//sie ist zwar nicht zyklisch, aber find_beg_of_cl behauptet das so!
		//problem ist ein See. so ca. 399/1529 bzw. real 399/471.
		//der hat die form:
		// x.
		// .x
		// find beg of cl generiert eine cyclic cl, die aber ein see ist.
		// find_cl l�uft dann ab da los,aber eben nicht um den see, sondern au�en!
		// das sollte nicht passieren. findcl/findbegcl weichen also ab. kacke! fixme!
		//das problem ist, da� die mitte der geometrischen form auf ner segmentgrenze liegt!
		//d.h. find_beg f�ngt innen an, speichert als startpunkt aber au�en...
		//loslaufrichtung bei dieser formation ist unklar! l�sung vielleicht: dort GAR NICHT anfangen.
		//problem tritt auch bei inseln auf.
		if (i>0 && oldcv.distance(cv) > 16) { cout << "WARNING!!!!!!!!!! "<<i<<"/"<<nrpts<<" oldcv "<<oldcv<<" cv "<<cv<<"\n"; }
		oldcv = cv;
		vector2i cvi = vector2i(int(round(cv.x * sscal)), int(round(cv.y * sscal)));
		// avoid double points here.
		if (spoints.empty() || !(spoints.back() == cvi))
			spoints.push_back(cvi);
	}

	divide_and_distribute_cl(spoints, cyclic);
}



void coastmap::process_segment(int sx, int sy)
{
	coastsegment& cs = coastsegments[sy*segsx+sx];
	if (cs.segcls.size() == 0) {	// no coastlines in segment.
		// segment is fully land or sea. determine what it is
		// check pixel value of bottom left corner (land/sea)?
		if (mapf(sx*pixels_per_seg, sy*pixels_per_seg) & 0x7f) {
			cs.type = 1;
		} else {
			cs.type = 0;
		}
	} else {		// there are coastlines in segment
		cs.type = 2;
		
		vector2 segoff = vector2(sx * segw_real + realoffset.x, sy * segw_real + realoffset.y);

		// compute cl.next info
		for (unsigned i = 0; i < cs.segcls.size(); ++i) {
			cs.compute_successor_for_cl(i);
		}
	}
}



vector2 coastmap::segcoord_to_real(int segx, int segy, const coastsegment::segpos& sp) const
{
	vector2 tmp(double(segx) + double(sp.x)/SEGSCALE, double(segy) + double(sp.y)/SEGSCALE);
	return (tmp * segw_real) + realoffset;
}



vector2f coastmap::segcoord_to_texc(int segx, int segy, const coastsegment::segpos& sp) const
{
	// float get to its limit when segsx,segsy > 256, bot that doesn't really matter.
	return vector2f(float(segx * SEGSCALE + sp.x)/(segsx*SEGSCALE), 1.0f - float(segy * SEGSCALE + sp.y)/(segsy*SEGSCALE));
}



// load from xml description file
coastmap::coastmap(const string& filename)
{
	TiXmlDocument doc(filename);
	doc.LoadFile();
	TiXmlElement* root = doc.FirstChildElement("dftd-map");
	sys().myassert(root != 0, string("coastmap: no root element found in ") + filename);
	TiXmlElement* etopology = root->FirstChildElement("topology");
	sys().myassert(etopology != 0, string("coastmap: no topology node found in ") + filename);
	const char* img = etopology->Attribute("image");
	sys().myassert(img != 0, string("coastmap: no image attribute found in ") + filename);
	realwidth = 0;
	etopology->Attribute("realwidth", &realwidth);
	sys().myassert(realwidth != 0, string("coastmap: realwidth not given or zero in ") + filename);
	etopology->Attribute("realoffsetx", &realoffset.x);
	etopology->Attribute("realoffsety", &realoffset.y);
	TiXmlElement* ecities = root->FirstChildElement("cities");
	if (ecities) {
		TiXmlElement* ecity = ecities->FirstChildElement("city");
		for ( ; ecity != 0; ecity = ecity->NextSiblingElement()) {
			// parse name, posx, posy  fixme
		}
	}

	SDL_Surface* surf = IMG_Load((get_map_dir() + img).c_str());
	add_loading_screen("map image loaded");
	sys().myassert(surf != 0, string("coastmap: error loading image ") + img + string(" referenced in file ") + filename);

	mapw = surf->w;
	maph = surf->h;
	pixelw_real = realwidth/mapw;
	realheight = maph*realwidth/mapw;
	pixels_per_seg = 1 << unsigned(ceil(log2(60000/pixelw_real)));
	segsx = mapw/pixels_per_seg;
	segsy = maph/pixels_per_seg;
	segw_real = pixelw_real * pixels_per_seg;
	sys().myassert((segsx*pixels_per_seg == mapw) && (segsy*pixels_per_seg == maph), string("coastmap: map size must be integer multiple of segment size, in") + filename);

	themap.resize(mapw*maph);

	SDL_LockSurface(surf);
	sys().myassert(surf->format->BytesPerPixel == 1 && surf->format->palette != 0 && surf->format->palette->ncolors == 2, string("coastmap: image is no black/white 1bpp paletted image, in ") + filename);

	Uint8* offset = (Uint8*)(surf->pixels);
	int mapoffy = maph*mapw;
	for (int yy = 0; yy < int(maph); yy++) {
		mapoffy -= mapw;
		for (int xx = 0; xx < int(mapw); ++xx) {
			Uint8 c = (*offset++);
			themap[mapoffy+xx] = (c > 0) ? 1 : 0;
		}
		offset += surf->pitch - mapw;
	}

	SDL_UnlockSurface(surf);
	SDL_FreeSurface(surf);

	add_loading_screen("image transformed");

	// they are filled in by process_coastline
	coastsegments.resize(segsx*segsy);

	// find coastlines
	// when to start processing: all patterns, except: 0,5,10,15
	for (int yy = 0; yy < int(maph); ++yy) {
		for (int xx = 0; xx < int(mapw); ++xx) {
			if (mapf(xx, yy) & 0x80) continue;
			Uint8 pattern = 0;
			for (int j = 0; j < 4; ++j) {
				pattern |= (mapf(xx+dmx[j], yy+dmy[j]) & 0x7f) << j;
			}
			if (patternprocessok[pattern]) {
				process_coastline(xx, yy);
			}
		}
	}

	// find coastsegment type and successors of cls.
	for (unsigned yy = 0; yy < segsy; ++yy) {
		for (unsigned xx = 0; xx < segsx; ++xx) {
			process_segment(xx, yy);
		}
	}

/*
	// read cities, fixme move to coastmap
	parser cityfile(get_map_dir() + "cities.txt");
	while (!cityfile.is_empty()) {
		bool xneg = cityfile.type() == TKN_MINUS;
		cityfile.consume();
		int xd = cityfile.parse_number();
		cityfile.parse(TKN_COMMA);
		int xm = cityfile.parse_number();
		cityfile.parse(TKN_COMMA);
		bool yneg = cityfile.type() == TKN_MINUS;
		cityfile.consume();
		int yd = cityfile.parse_number();
		cityfile.parse(TKN_COMMA);
		int ym = cityfile.parse_number();
		cityfile.parse(TKN_COMMA);
		string n = cityfile.parse_string();
		double x, y;
		sea_object::degrees2meters(xneg, xd, xm, yneg, yd, ym, x, y);
		cities.push_back(make_pair(vector2(x, y), n));
	}
*/

	// fixme: clear "themap" so save space.
	// information wether a position on the map is land or sea can be computed from
	// segment data. This will save 6MB of space at least.

	add_loading_screen("coastmap created");
}



void coastmap::draw_as_map(const vector2& droff, double mapzoom, int detail) const
{
	int x, y, w, h;
//cout << "mapzoom pix/m = " << mapzoom << " segwreal " << segw_real << "\n";
	w = int(ceil((1024/mapzoom)/segw_real)) +2;
	h = int(ceil((768/mapzoom)/segw_real)) +2;	// fixme: use 640 and translate map y - 64
//cout << " w " << w << " h " << h << " waren's mal.\n";
	x = int(floor((droff.x - realoffset.x)/segw_real)) - w/2;
	y = int(floor((droff.y - realoffset.y)/segw_real)) - h/2;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > int(segsx)) {
		w = int(segsx) - x;
	}
	if (y + h > int(segsy)) {
		h = int(segsy) - y;
	}
//cout<<"draw map   segsx " << segsx << " segsy " << segsy << " x " << x << " y " << y << " w " << w << " h " << h << "\n";

	atlanticmap->set_gl_texture();
	for (int yy = y; yy < y + h; ++yy) {
		for (int xx = x; xx < x + w; ++xx) {
			coastsegments[yy*segsx+xx].draw_as_map(*this, xx, yy, detail);
		}
	}

/*
	// draw cities, fixme move to coastmap
	for (list<pair<vector2, string> >::const_iterator it = cities.begin(); it != cities.end(); ++it) {
		glBindTexture(GL_TEXTURE_2D, 0);
		draw_square_mark(gm, it->first, -offset, color(255, 0, 0));
		vector2 pos = (it->first - offset) * mapzoom;
		font_arial->print(int(512 + pos.x), int(384 - pos.y), it->second);
	}
*/
}



void coastmap::render(const vector2& p, int detail, bool withterraintop) const
{
	vector2 pnew = p - realoffset;

	// determine which coast segment can be seen (at most 4)
	// fixme
	double rsegw = pixelw_real * pixels_per_seg;
	int moffx = int(pnew.x/rsegw);
	int moffy = int(pnew.y/rsegw);

	glPushMatrix();
	glTranslated(-p.x, -p.y, 0.0);

	if (moffx >= 0 && moffy >= 0 && moffx < int(segsx) && moffy < int(segsy))	
		coastsegments[moffy*segsx+moffx].render(*this, moffx, moffy, pnew - vector2(moffx*rsegw, moffy*rsegw), detail +2);

	glPopMatrix();
}
