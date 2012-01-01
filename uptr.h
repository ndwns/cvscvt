#ifndef UPTR_H
#define UPTR_H

template<typename T> class uptr
{
public:
	uptr() : ptr_() {}

	uptr(T* const p) : ptr_(p) {}

	~uptr() { delete ptr_; }

	uptr& operator =(T* const p)
	{
		delete ptr_;
		ptr_ = p;
		return *this;
	}

	T& operator *() const { return *ptr_; }

	T* operator ->() const { return ptr_; }

private:
	uptr(uptr const&);

	T* ptr_;
};

#endif
