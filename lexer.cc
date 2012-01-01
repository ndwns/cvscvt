#include <stdexcept>

#include "lexer.h"
#include "set.h"


Lexer::Lexer(FILE* const f) : f_(f), line_(1), col_(0)
{
	next();
}

static Set<Blob*> texts;

static Blob* hash_find(Blob* const b)
{
	return texts.insert(b);
}

static inline bool is_num_char(int const c)
{
	return ('0' <= c && c <= '9') || c == '.';
}

static inline bool is_visible_char(int const c)
{
	return
		(0x21 <= c && c <= 0x7E && c != '$' && c != ',' && c != '.' && c != ':' && c != ';' && c != '@') ||
		(0xA0 <= c && c <= 0xFF);
}

int Lexer::read_char()
{
	int const c = fgetc(f_);
	if (c == EOF && !feof(f_))
		throw std::runtime_error("read failed");
	++col_;
	return c;
}

void Lexer::unget_char(int const c)
{
	--col_;
	ungetc(c, f_);
}

void Lexer::next()
{
	blob_ = 0;

	for (;;) {
		int c = read_char();
		colstart_ = col_;
		switch (c) {
			case EOF: kind_ = T_EOF; return;

			case '\n': // LF line feed
				++line_;
				col_ = 0;
				/* FALLTHROUGH */
			case '\b': // BS backspace
			case '\t': // HT horizontal tabulator
			case '\v': // VT vertical tabulator
			case '\f': // FF form feed
			case '\r': // CR carriage return
			case ' ': // SP space
				continue; // Skip whitespace.

#if 0
			case '$': return T_DOLLAR;
			case ',': return T_COMMA;
#endif
			case ':': kind_ = T_COLON;     return;
			case ';': kind_ = T_SEMICOLON; return;

			case '@': {
				BlobBuilder b;
				for (;;) {
					c = read_char();
					switch (c) {
						case EOF:
							throw std::runtime_error("unterminated string");

						case '\n':
							++line_;
							col_ = 0;
							break;

						case '@':
							c = read_char();
							if (c != '@') {
								unget_char(c);
								blob_ = hash_find(b.get());
								kind_ = T_STRING;
								return;
							}
							break;
					}
					b.add_byte(c);
				}
			}

			default: {
				BlobBuilder b;
				if (is_num_char(c)) {
					do {
						b.add_byte(c);
						c = read_char();
					} while (is_num_char(c));
					if (is_visible_char(c))
						goto read_ident;
					kind_ = T_NUM;
				} else if (is_visible_char(c)) {
read_ident:
					do {
						b.add_byte(c);
						c = read_char();
					} while (is_visible_char(c));
					kind_ = T_ID;
				} else {
					throw std::runtime_error("invalid char in input");
				}
				unget_char(c);
				blob_ = hash_find(b.get());
				return;
			}
		}
	}
}

Symbol Lexer::add_keyword(char const* const s)
{
	return hash_find(Blob::alloc(s));
}

Symbol Lexer::expect(Symbol const b)
{
	if (kind_ == T_ID && blob_ == b) {
		next();
		return b;
	} else {
		throw std::runtime_error("unexpected token");
	}
}

Symbol Lexer::expect(TokenKind const t)
{
	if (kind_ == t) {
		Symbol const b = blob_;
		next();
		return b;
	} else {
		throw std::runtime_error("unexpected token");
	}
}

Symbol Lexer::accept(Symbol const b)
{
	if (kind_ == T_ID && blob_ == b) {
		next();
		return b;
	} else {
		return 0;
	}
}

Symbol Lexer::accept(TokenKind const t)
{
	if (kind_ == t) {
		Symbol const b = blob_;
		next();
		return b;
	} else {
		return 0;
	}
}
