#ifndef STRUTIL_H
#define STRUTIL_H

#include <cstring>

static inline bool between(char const min, char const c, char const max)
{
	return min <= c && c <= max;
}

static inline bool streq(char const* const a, char const* const b)
{
	return std::strcmp(a, b) == 0;
}

static inline bool strless(char const* const a, char const* const b)
{
	return std::strcmp(a, b) < 0;
}

#endif
