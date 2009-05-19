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

//
//  A bounding volume tree (spheres) (C)+(W) 2009 Thorsten Jordan
//

#include "bv_tree.h"

//#define PRINT(x) std::cout << x
#define PRINT(x) do { } while (0)

std::auto_ptr<bv_tree> bv_tree::create(std::list<leaf_data>& nodes)
{
	std::auto_ptr<bv_tree> result;
	// if list has zero entries, return empty pointer
	if (nodes.empty())
		return result;
	// compute bounding box for leaves
	vector3f bbox_min = nodes.front().v[0];
	vector3f bbox_max = bbox_min;
	for (std::list<leaf_data>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
		for (unsigned i = 0; i < 3; ++i) {
			bbox_min = bbox_min.min(it->v[i]);
			bbox_max = bbox_max.max(it->v[i]);
		}
	}
	// new sphere center is center of bbox
	spheref bound_sphere((bbox_min + bbox_max) * 0.5f, 0.0f);
	// compute sphere radius by vertex distances to center (more accurate than
	// approximating by bbox size)
	for (std::list<leaf_data>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
		for (unsigned i = 0; i < 3; ++i) {
			float r = it->v[i].distance(bound_sphere.center);
			bound_sphere.radius = std::max(r, bound_sphere.radius);
		}
	}
	// if list has one entry, return that
	if (nodes.size() == 1) {
		result.reset(new bv_tree(bound_sphere, nodes.front().triangle));
		return result;
	}
	//
	// split leaf node list in two parts
	//
	vector3f deltav = bbox_max - bbox_min;
	// chose axis with longest value range, sort along that axis,
	// split in center of bound_sphere.
	PRINT("nodes " << nodes.size() << " boundsph=" << bound_sphere.center << "|" << bound_sphere.radius << "\n");
	unsigned split_axis = 0; // x - default
	if (deltav.y > deltav.x) {
		if (deltav.z > deltav.y) {
			split_axis = 2; // z
		} else {
			split_axis = 1; // y
		}
	} else if (deltav.z > deltav.x) {
		split_axis = 2; // z
	}
	PRINT("deltav " << deltav << " split axis " << split_axis << "\n");
	std::list<leaf_data> left_nodes, right_nodes;
	float vcenter[3];
	bound_sphere.center.to_mem(vcenter);
	while (!nodes.empty()) {
		float vc[3];
		nodes.front().get_center().to_mem(vc);
		if (vc[split_axis] < vcenter[split_axis])
			left_nodes.splice(left_nodes.end(), nodes, nodes.begin());
		else
			right_nodes.splice(right_nodes.end(), nodes, nodes.begin());
	}
	if (left_nodes.empty() || right_nodes.empty()) {
		PRINT("special case\n");
		// special case: force division
		std::list<leaf_data>& empty_list = left_nodes.empty() ? left_nodes : right_nodes;
		std::list<leaf_data>& full_list = left_nodes.empty() ? right_nodes : left_nodes;
		std::list<leaf_data>::iterator it = full_list.begin();
		for (unsigned i = 0; i < full_list.size() / 2; ++i)
			++it;
		empty_list.splice(empty_list.end(), full_list, full_list.begin(), it);
	}
	PRINT("left " << left_nodes.size() << " right " << right_nodes.size() << "\n");
	result.reset(new bv_tree(bound_sphere, create(left_nodes), create(right_nodes)));
	PRINT("final volume " << result->volume.center << "|" << result->volume.radius << "\n");
	return result;
}



bv_tree::bv_tree(const spheref& sph, std::auto_ptr<bv_tree> left_tree, std::auto_ptr<bv_tree> right_tree)
	: volume(sph), triangle(unsigned(-1))
{
	children[0] = left_tree;
	children[1] = right_tree;
}



bool bv_tree::is_inside(const vector3f& v) const
{
	if (!volume.is_inside(v))
		return false;
	for (int i = 0; i < 2; ++i)
		if (children[i].get())
			if (children[i]->is_inside(v))
				return true;
	return false;
}



bool bv_tree::collides(const bv_tree& other, std::list<vector3f>& contact_points) const
{
	// if bounding volumes do not intersect, there can't be any collision of leaf elements
	if (!volume.intersects(other.volume))
		return false;

	// handel case that this is a leaf node
	if (is_leaf()) {
		// we have a leaf node
		if (other.is_leaf()) {
			// direct face to face collision test
			// fixme ...
			return true;
		} else {
			// other node is no leaf, recurse there
			bool col1 = other.children[0]->collides(*this, contact_points);
			bool col2 = other.children[1]->collides(*this, contact_points);
			return col1 || col2;
		}
	}

	// split larger volume of this and other, go recursivly down all children
	if (volume.radius > other.volume.radius || other.is_leaf()) {
		// recurse this node
		bool col1 = children[0]->collides(other, contact_points);
		bool col2 = children[1]->collides(other, contact_points);
		return col1 || col2;
	} else {
		// recurse other node - other is no leaf here
		bool col1 = other.children[0]->collides(*this, contact_points);
		bool col2 = other.children[1]->collides(*this, contact_points);
		return col1 || col2;
	}
}



bool bv_tree::collides(const bv_tree& other, std::list<vector3f>& contact_points, const matrix4f& other_transform, bool use_transform) const
{
	// split larger volume of this and other, go recursivly down larger, both paths
	// call other.collides with !use_transform
	// use transform for other always if use_transform==true
	return false; // fixme
}



void bv_tree::transform(const matrix4f& mat)
{
	volume.center = mat.mul4vec3xlat(volume.center);
	for (int i = 0; i < 2; ++i)
		if (children[i].get())
			children[i]->transform(mat);
}



void bv_tree::compute_min_max(vector3f& minv, vector3f& maxv) const
{
	volume.compute_min_max(minv, maxv);
	for (int i = 0; i < 2; ++i)
		if (children[i].get())
			children[i]->compute_min_max(minv, maxv);
}



void bv_tree::debug_dump(unsigned level) const
{
	for (unsigned i = 0; i < level; ++i)
		std::cout << "\t";
	std::cout << "Level " << level << " Sphere " << volume.center << " | " << volume.radius << "\n";
	for (int i = 0; i < 2; ++i)
		if (children[i].get())
			children[i]->debug_dump(level + 1);
}
