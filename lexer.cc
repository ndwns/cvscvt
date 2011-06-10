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

static size_t blob_capacity;
static Blob*  blob;

static void init_blob()
{
	blob_capacity = 16;
	blob          = Blob::alloc(blob_capacity);
}

static void add_byte(u1 const c)
{
	if (blob->size == blob_capacity)
	{
		size_t const cap = blob_capacity;
		Blob*  const s   = Blob::alloc(*blob, cap * 2);
		delete [] blob;
		blob_capacity = cap * 2;
		blob          = s;
	}
	blob->append(c);
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
	blob_capacity = 0;
	blob          = 0;

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

			case '@':
				init_blob();
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
								blob = hash_find(blob);
								kind_ = T_STRING;
								return;
							}
							break;
					}
					add_byte(c);
				}

			case '.':
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				init_blob();
				do {
					add_byte(c);
					c = read_char();
				} while (is_num_char(c));
				if (is_visible_char(c))
					goto parse_ident;
				unget_char(c);
				blob = hash_find(blob);
				kind_ = T_NUM;
				return;

			default:
				if (is_visible_char(c)) {
					init_blob();
parse_ident:
					do {
						add_byte(c);
						c = read_char();
					} while (is_visible_char(c));
					unget_char(c);
					blob = hash_find(blob);
					kind_ = T_ID;
					return;
				} else {
					throw std::runtime_error("invalid char in input");
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
	if (kind_ == T_ID && blob == b) {
		next();
		return b;
	} else {
		throw std::runtime_error("unexpected token");
	}
}

Symbol Lexer::expect(TokenKind const t)
{
	if (kind_ == t) {
		Symbol const b = blob;
		next();
		return b;
	} else {
		throw std::runtime_error("unexpected token");
	}
}

Symbol Lexer::accept(Symbol const b)
{
	if (kind_ == T_ID && blob == b) {
		next();
		return b;
	} else {
		return 0;
	}
}

Symbol Lexer::accept(TokenKind const t)
{
	if (kind_ == t) {
		Symbol const b = blob;
		next();
		return b;
	} else {
		return 0;
	}
}
