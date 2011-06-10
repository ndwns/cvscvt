#ifndef INDENT_H
#define INDENT_H

#include <ostream>

class Indent
{
public:
	Indent(char const* const indent = "  ") : level(), indent(indent) {}

	Indent& operator ++() { ++level; return *this; }
	Indent& operator --() { --level; return *this; }

private:
	size_t            level;
	char const* const indent;

	friend std::ostream& operator <<(std::ostream&, Indent const&);
};

#endif
