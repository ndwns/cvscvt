#ifndef PIECETABLE_H
#define PIECETABLE_H

#include <ostream>

#include "blob.h"
#include "vector.h"

class PieceTable
{
public:
	PieceTable() : size_(0) {}

	PieceTable(Blob const& b) { set(b); }

	void set(Blob const&);

	void modify(PieceTable const&, Blob const&);

	size_t size() const { return size_; }

private:
	struct Piece
	{
		Piece(u1 const* data, size_t const size) : data(data), size(size) {}

		u1 const* data;
		size_t    size;
	};

	Vector<Piece> pieces_;
	size_t        size_;

	friend std::ostream& operator <<(std::ostream&, PieceTable const&);
};

#endif
