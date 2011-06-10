#ifndef HEAP_H
#define HEAP_H

#include "vector.h"

template<typename T, typename U = bool(T const&, T const&)> class Heap
{
	private:
		static bool default_compare(T const& a, T  const& b) { return a < b; }

	public:
		Heap(U& compare = default_compare) : compare_(compare) {}

		void push(T const& v);
		void pop();

		bool empty() const { return heap_.empty(); }

		size_t size() const { return heap_.size(); }

		T&       front()       { return heap_.front(); }
		T const& front() const { return heap_.front(); }

	private:
		Vector<T> heap_;
		U&        compare_;
};

template<typename T, typename U> void Heap<T, U>::push(T const& v)
{
	size_t dst = heap_.size();
	heap_.push_back(v);
	for (; dst != 0;) {
		size_t parent = (dst - 1) / 2;
		if (compare_(v, heap_[parent]))
			break;

		heap_[dst] = heap_[parent];
		dst = parent;
	}
	heap_[dst] = v;
}

template<typename T, typename U> void Heap<T, U>::pop()
{
	T v = heap_.back();
	heap_.pop_back();
	size_t src  = 0;
	size_t size = heap_.size();
	if (size != 0) {
		for (;;) {
			size_t const left = src * 2 + 1;
			if (size <= left)
				break;
			size_t const right = src * 2 + 2;
			if (compare_(v, heap_[left])) {
				if (right < size && compare_(heap_[left], heap_[right]))
					goto right;
				heap_[src] = heap_[left];
				src = left;
			} else if (right < size && compare_(v, heap_[right])) {
right:
				heap_[src] = heap_[right];
				src = right;
			} else {
				break;
			}
		}
		heap_[src] = v;
	}
}

#endif
