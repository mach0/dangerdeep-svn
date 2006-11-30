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

// Danger from the Deep, standard error/exception
// (C)+(W) by Thorsten Jordan. See LICENSE

#ifndef ERROR_H
#define ERROR_H

#include <string>
#include <stdexcept>

// 2006-11-30 doc1972 On WIN32 we need to undef ERROR because its already defined as
// #define ERROR 0 in wingdi.h
#ifdef WIN32
#undef ERROR
#endif /* WIN32 */

///\brief Base exception class for any runtime error.
class error : public std::runtime_error
{
 public:
#if defined(DEBUG) && defined(__GNUC__)
	static std::string str(const char* file, unsigned line);
#define ERROR(x) error(x + error::str(__FILE__, __LINE__))
#else
#define ERROR(x) error(x)
#endif
	error(const std::string& s) : std::runtime_error(std::string("DftD error: ") + s) {}
};

#endif
