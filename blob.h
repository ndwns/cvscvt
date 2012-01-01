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

	bool empty() const { return size == 0; }

	u1 const* begin() const { return data; }

	u1 const* end() const { return data + size; }

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

class BlobBuilder
{
public:
	BlobBuilder() : capacity_(16), blob_(Blob::alloc(capacity_)) {}

	~BlobBuilder() { delete blob_; }

	void add_byte(u1 const c)
	{
		if (blob_->size == capacity_) {
			size_t const cap = capacity_;
			Blob*  const b   = Blob::alloc(*blob_, cap * 2);
			delete blob_;
			capacity_ = cap * 2;
			blob_     = b;
		}
		blob_->append(c);
	}

	bool empty() const { return blob_->empty(); }

	Blob* get()
	{
		Blob* const b = blob_;
		blob_ = 0;
		return b;
	}

private:
	size_t capacity_;
	Blob*  blob_;
};

#endif
