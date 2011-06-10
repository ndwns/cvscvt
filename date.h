#ifndef DATE_H
#define DATE_H

#include <ostream>

#include "blob.h"
#include "types.h"

struct Date
{
	Date() : year(), month(), day(), hour(), minute(), second() {}

	Date(u2 const year, u1 const month, u1 const day, u1 const hour, u1 const minute, u1 const second) :
		year(year),
		month(month),
		day(day),
		hour(hour),
		minute(minute),
		second(second)
	{}

	u4 seconds() const;

	static Date parse(Blob const*);

	u2 year;
	u1 month;
	u1 day;
	u1 hour;
	u1 minute;
	u1 second;
};

static inline bool operator <(Date const& a, Date const& b)
{
	return
		(a.year   < b.year   || (a.year   == b.year   &&
		(a.month  < b.month  || (a.month  == b.month  &&
		(a.day    < b.day    || (a.day    == b.day    &&
		(a.hour   < b.hour   || (a.hour   == b.hour   &&
		(a.minute < b.minute || (a.minute == b.minute &&
		(a.second < b.second)))))))))));
}

static inline bool operator ==(Date const& a, Date const& b)
{
	return
		a.year   == b.year   &&
		a.month  == b.month  &&
		a.day    == b.day    &&
		a.hour   == b.hour   &&
		a.minute == b.minute &&
		a.second == b.second;
}

static inline bool operator !=(Date const& a, Date const& b)
{
	return !(a == b);
}

std::ostream& operator <<(std::ostream&, Date const&);

#endif
