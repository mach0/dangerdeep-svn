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

// global defined directories for data.
// subsim (C)+(W) Thorsten Jordan. SEE LICENSE

#ifndef DIRECTORIES_H
#define DIRECTORIES_H

#include <string>
#include <map>
#include <list>

std::string get_data_dir();
inline std::string get_texture_dir() { return get_data_dir() + "textures/"; }
inline std::string get_font_dir() { return get_data_dir() + "fonts/"; }
inline std::string get_model_dir() { return get_data_dir() + "models/"; }
inline std::string get_sound_dir() { return get_data_dir() + "sounds/"; }
inline std::string get_image_dir() { return get_data_dir() + "images/"; }
inline std::string get_mission_dir() { return get_data_dir() + "missions/"; }
inline std::string get_map_dir() { return get_data_dir() + "maps/"; }
inline std::string get_shader_dir() { return get_data_dir() + "shaders/"; }

//deprecated!!! fixme, remove them asap
inline std::string get_ship_dir() { return get_data_dir() + "ships/"; }
inline std::string get_submarine_dir() { return get_data_dir() + "submarines/"; }
inline std::string get_airplane_dir() { return get_data_dir() + "airplanes/"; }
inline std::string get_torpedo_dir() { return get_data_dir() + "torpedoes/"; }

class data_file_handler
{
 private:
	data_file_handler();
	void parse_for_data_files(std::string dir, std::list<std::string>& idlist);

	static data_file_handler* my_instance;
	std::map<std::string, std::string> data_files;
	std::list<std::string> airplane_ids;
	std::list<std::string> ship_ids;
	std::list<std::string> submarine_ids;
	std::list<std::string> torpedo_ids;
 public:
	static const data_file_handler& instance();
	const std::string& get_path(const std::string& objectid) const;
	std::string get_filename(const std::string& objectid) const;
	const std::list<std::string>& get_airplane_list() const { return airplane_ids; }
	const std::list<std::string>& get_ship_list() const { return ship_ids; }
	const std::list<std::string>& get_submarine_list() const { return submarine_ids; }
	const std::list<std::string>& get_torpedo_list() const { return torpedo_ids; }
};

inline const data_file_handler& data_file() { return data_file_handler::instance(); }

#endif
