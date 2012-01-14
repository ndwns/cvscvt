#ifndef LEXER_H
#define LEDER_H

#include <stdio.h>

#include "blob.h"
#include "types.h"

typedef Blob const* Symbol;

enum TokenKind
{
	T_COLON,
#if 0
	T_COMMA,
	T_DOLLAR,
#endif
	T_EOF,
	T_ID,
	T_NUM,
	T_SEMICOLON,
	T_STRING,
};

class Lexer
{
public:
	Lexer(FILE*);

	static Symbol add_keyword(char const*);
	static Symbol add_symbol(Blob*);

	void next();

	Symbol expect(Symbol);
	Symbol expect(TokenKind);

	Symbol accept(Symbol);
	Symbol accept(TokenKind);

	u4 line() const { return line_; }
	u4 col()  const { return col_; }

private:
	int read_char();

	void unget_char(int);

	FILE*     f_;
	TokenKind kind_;
	u4        line_;
	u4        col_;
	u4        colstart_;
	Symbol    blob_;
};

#endif
