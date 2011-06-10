#ifndef BLOB_H
#define BLOB_H

#include <ostream>
#include <cstring>

#include "types.h"

struct Blob
{
	size_t size;
	u1     data[];

	static Blob* alloc(size_t const capacity)
	{
		return new(capacity) Blob();
	}

	static Blob* alloc(Blob const& o, size_t const capacity)
	{
		return new(capacity) Blob(o.data, o.size);
	}

	static Blob* alloc(char const* const str)
	{
		size_t const size = std::strlen(str);
		return new(size) Blob((u1 const*)str, size);
	}

	void append(u1 const c)
	{
		data[size++] = c;
	}

	u4 hash() const
	{
		u4 hash = 2166136261U;

		for (u1 const* p = data; p != data + size; ++p) {
			hash *= 16777619U;
			hash ^= *p;
		}

		return hash;
	}

private:
	Blob() : size(0) {}

	Blob(u1 const* const data, size_t const size) : size(size)
	{
		memcpy(this->data, data, size);
	}

	void* operator new(size_t const size, size_t const data_size)
	{
		return ::operator new(size + data_size);
	}
};

static inline bool operator ==(Blob const& a, Blob const& b)
{
	return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
}

static inline std::ostream& operator <<(std::ostream& o, Blob const& b)
{
	return o.write(reinterpret_cast<char const*>(b.data), b.size);
}

#endif
