#include <stdexcept>

#include "piecetable.h"

void PieceTable::set(Blob const& b)
{
	size_ = b.size;

	u1 const* data = b.data;
	size_t    size = 0;
	for (u1 const* i = data, * const end = data + b.size; i != end; ++i) {
		++size;
		if (*i == '\n') {
			pieces_.push_back(Piece(data, size));
			data = i + 1;
			size = 0;
		}
	}
	if (size != 0) {
		pieces_.push_back(Piece(data, size));
	}
}

void PieceTable::modify(PieceTable const& src, Blob const& b)
{
	Vector<Piece> p;
	size_t line  = 0; // Copied till this line
	size_t total = 0; // Total size of new piece table in bytes
	for (u1 const* i = b.data, * const end = i + b.size; i != end;) {
		u1 const cmd = *i++;

		size_t l = 0;
		for (;;) {
			if (i == end) goto invalid;
			if ('0' <= *i && *i <= '9') {
				l = l * 10 + (*i++ - '0');
			} else {
				break;
			}
		}

		if (cmd == 'd') --l;

		if (l < line)               goto invalid;
		if (src.pieces_.size() < l) goto invalid;

		if (i == end || *i++ != ' ') goto invalid;

		size_t n = 0;
		for (;;) {
			if (i == end) goto invalid;
			if ('0' <= *i && *i <= '9') {
				n = n * 10 + (*i++ - '0');
			} else {
				break;
			}
		}

		if (n == 0) goto invalid;

		if (i == end || *i++ != '\n') goto invalid;

		while (line != l) {
			Piece const& piece = src.pieces_[line++];
			total += piece.size;
			p.push_back(piece);
		}

		if (cmd == 'a') {
			u1 const* data = i;
			size_t    size = 0;
			for (;;) {
				if (i == end) {
					if (n != 1 || size == 0) goto invalid;
					total += size;
					p.push_back(Piece(data, size));
					break;
				}

				++size;
				if (*i++ == '\n') {
					total += size;
					p.push_back(Piece(data, size));
					if (--n == 0) break;
					data = i;
					size = 0;
				}
			}
		} else if (cmd == 'd') {
			if (src.pieces_.size() - line < n) goto invalid;
			line += n;
		} else {
			goto invalid;
		}
	}

	while (line != src.pieces_.size()) {
		Piece const& piece = src.pieces_[line++];
		total += piece.size;
		p.push_back(piece);
	}

	std::swap(pieces_, p);
	size_ = total;
	return;

invalid:
	throw std::runtime_error("invalid delta");
}

std::ostream& operator <<(std::ostream& o, PieceTable const& p)
{
	for (Vector<PieceTable::Piece>::const_iterator i = p.pieces_.begin(), end = p.pieces_.end(); i != end; ++i) {
		o.write(reinterpret_cast<char const*>(i->data), i->size);
	}
	return o;
}
