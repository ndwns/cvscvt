#include "indent.h"

std::ostream& operator <<(std::ostream& o, Indent const& i)
{
	char const* const indent = i.indent;
	for (size_t n = i.level; n != 0; --n) {
		o << indent;
	}
	return o;
}
