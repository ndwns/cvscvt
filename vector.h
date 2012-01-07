#ifndef VECTOR_H
#define VECTOR_H

#include <algorithm>
#include <cassert>

#include "types.h"

template<typename T> class Vector
{
public:
	typedef T*       iterator;
	typedef T const* const_iterator;

	Vector() : capacity_(0), size_(0), data_(0) {}

	Vector(size_t const n);

	~Vector();

	T&       operator [](size_t const i)       { assert(i < size_); return data_[i]; }
	T const& operator [](size_t const i) const { assert(i < size_); return data_[i]; }

	T&       front()       { assert(size_ != 0); return data_[0]; }
	T const& front() const { assert(size_ != 0); return data_[0]; }

	T&       back()       { assert(size_ != 0); return data_[size_ - 1]; }
	T const& back() const { assert(size_ != 0); return data_[size_ - 1]; }

	void push_back(T const& v)
	{
		if (size_ == capacity_) {
			expand();
		}

		data_[size_++] = v;
	}

	void pop_back()
	{
		assert(size_ != 0);
		data_[--size_].~T();
	}

	bool empty() const { return size_ == 0; }

	size_t size() const { return size_; }

	iterator       begin()       { return data_; }
	const_iterator begin() const { return data_; }

	iterator       end()       { return data_ + size_; }
	const_iterator end() const { return data_ + size_; }

private:
	void expand();

	size_t capacity_;
	size_t size_;
	T*     data_;

	Vector(Vector const&);          // No copy
	void operator =(Vector const&); // No assignment

	template<typename U> friend void std::swap(Vector<U>&, Vector<U>&);
};

template<typename T> Vector<T>::Vector(size_t const n) :
	capacity_(n), size_(n), data_(new T[n]())
{
	for (T* i = data_, * const end = data_ + n; i != end; ++i) {
		new(i) T();
	}
}

template<typename T> Vector<T>::~Vector()
{
	for (T* i = data_ + size_, * const end = data_; i != end;) {
		(--i)->~T();
	}
	::operator delete(data_);
}

template<typename T> void Vector<T>::expand()
{
	size_t const ncap = capacity_ != 0 ? capacity_ * 2 : 4;
	T*     const ndat = static_cast<T*>(::operator new(ncap * sizeof(T)));
	T*     const data = data_;
	size_t const size = size_;
	for (size_t i = 0; i != size; ++i) {
		new(&ndat[i]) T(data[i]);
	}
	for (T* i = data + size, * const end = data; i != end;) {
		(--i)->~T();
	}
	capacity_ = ncap;
	data_     = ndat;
	::operator delete(data);
}

namespace std
{
	template<typename T> inline void swap(Vector<T>& a, Vector<T>& b)
	{
		swap(a.capacity_, b.capacity_);
		swap(a.size_,     b.size_);
		swap(a.data_,     b.data_);
	}
}

#endif
