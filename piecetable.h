#ifndef PIECETABLE_H
#define PIECETABLE_H

#include <ostream>

#include "blob.h"
#include "vector.h"

class PieceTable
{
public:
	PieceTable(Blob const&);

	void modify(Blob const& b);

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
