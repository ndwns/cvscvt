#ifndef STRUTIL_H
#define STRUTIL_H

#include <cstring>

static inline bool streq(char const* const a, char const* const b)
{
	return std::strcmp(a, b) == 0;
}

static inline bool strless(char const* const a, char const* const b)
{
	return std::strcmp(a, b) < 0;
}

#endif
