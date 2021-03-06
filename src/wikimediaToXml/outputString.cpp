/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/// \brief Helper functions for output
/// \file outputString.hpp
#include "outputString.hpp"
#include <stdexcept>

using namespace strus;

std::string strus::outputString( const char* si, const char* se, int maxlen)
{
	if (!si || !se)
	{
		throw std::runtime_error( "outputString called with NULL argument");
	}
	else if (maxlen > 0 && se - si > maxlen)
	{
		char const* sn = si + maxlen/2;
		enum {B10000000 = 128, B11000000 = 128 + 64};
		while (sn < se && (*sn & B11000000) == B10000000) ++sn;
		std::string head( si, sn-si);
		sn = se - maxlen/2;
		while (sn < se && (*sn & B11000000) == B10000000) ++sn;
		std::string tail( sn, se-sn);
		return head + "..." + tail;
	}
	else
	{
		return std::string( si, se-si);
	}
}

std::string strus::outputLineString( const char* si, const char* se, int maxlen)
{
	std::string rt = outputString( si, se, maxlen);
	std::string::iterator ri = rt.begin(), re = rt.end();
	for (; ri != re; ++ri)
	{
		if ((unsigned char)*ri < 32)
		{
			*ri = ' ';
		}
	}
	return rt;
}

std::string strus::outputLineString( const std::string& str, int maxlen)
{
	return outputLineString( str.c_str(), str.c_str()+str.size(), maxlen);
}



