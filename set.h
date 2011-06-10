#ifndef SET_H
#define SET_H

#include "types.h"

template<typename T> class Set
{
private:
	struct Entry
	{
		Entry() : hash(), data() {}

		u4 hash;
		T  data;
	};

public:
	class iterator
	{
	public:
		iterator(Entry* const e, Entry const* const end) : e(e), end(end) {}

		T& operator *() { return e->data; }

		iterator& operator ++()
		{
			while (++e != end && !e->data) {}
			return *this;
		}

		bool operator ==(iterator const& o) const { return e == o.e; }
		bool operator !=(iterator const& o) const { return !(*this == o); }

	private:
		Entry*             e;
		Entry const* const end;
	};

	Set() : capacity_(256), size_(0), table_(new Entry[capacity_]()) {}

	~Set() { delete [] table_; }

	void clear()
	{
		Entry* const t = new Entry[capacity_]();
		delete [] table_;
		table_ = t;
		size_  = 0;
	}

	size_t size() const { return size_; }

	T& insert(T const&);

	T* find(T const&);

	iterator begin()
	{
		iterator i(table_, table_ + capacity_);
		if (!table_->data) ++i;
		return i;
	}

	iterator end()
	{
		Entry* const end = table_ + capacity_;
		return iterator(end, end);
	}

private:
	size_t capacity_;
	size_t size_;
	Entry* table_;
};

template<typename T> T& Set<T>::insert(T const& v)
{
	if (size_ == capacity_ / 2) {
		size_t const cap = capacity_ * 2;
		Entry* const t   = new Entry[cap]();
		for (Entry const* i = table_, * const end = table_ + capacity_; i != end; ++i) {
			Entry const& src = *i;
			if (!src.data) continue;

			for (u4 idx = src.hash, step = 0;; idx += ++step) {
				Entry& dst = t[idx & (cap - 1)];
				if (dst.data) continue;
				dst = src;
				break;
			}
		}

		delete [] table_;
		capacity_ = cap;
		table_    = t;
	}

	u4 const hash = v->hash();
	for (u4 idx = hash, step = 0;; idx += ++step) {
		Entry& e = table_[idx & (capacity_ - 1)];
		if (!e.data) {
			++size_;
			e.hash = hash;
			e.data = v;
			return e.data;
		} else if (e.hash == hash && *e.data == *v) {
			delete v;
			return e.data;
		}
	}
}

template<typename T> T* Set<T>::find(T const& v)
{
	u4 const hash = v->hash();
	for (u4 idx = hash, step = 0;; idx += ++step) {
		Entry& e = table_[idx & (capacity_ - 1)];
		if (!e.data) {
			return 0;
		} else if (e.hash == hash && *e.data == *v) {
			return &e.data;
		}
	}
}

#endif
