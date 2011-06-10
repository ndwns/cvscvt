#include <iomanip>
#include <stdexcept>

#include "date.h"

u4 Date::seconds() const
{
	u4 days;
	days  = year * 365;
	days += year /   4;
	days -= year / 100;
	days += year / 400;

	static u2 const days_till_month[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	days += days_till_month[month - 1];
	u4 const y = year - year % 4;
	if ((y % 100 != 0 || y % 400 == 0) && (year % 4 != 0 || month > 2)) ++days;

	days += day;

	u4 const hours   = days    * 24 + hour;
	u4 const minutes = hours   * 60 + minute;
	u4 const seconds = minutes * 60 + second;
	return seconds;
}

static u4 read_number(u1 const* const s)
{
	if ('0' <= s[0] && s[0] <= '9' &&
			'0' <= s[1] && s[1] <= '9') {
		return (u4)(s[0] - '0') * 10 + (s[1] - '0');
	} else {
		throw std::runtime_error("invalid number");
	}
}

Date Date::parse(Blob const* const b)
{
	{ u1 const* p = b->data;
		u2 year;
		if (b->size == 17) {
			year = 1900 + read_number(p);
			p += 2;
		} else if (b->size == 19) {
			year  = read_number(p) * 100;
			p += 2;
			year += read_number(p);
			p += 2;
			if (year < 2000) goto invalid;
		} else {
			goto invalid;
		}

		if (*p++ != '.') goto invalid;

		u1 const month = read_number(p);
		p += 2;
		if (month < 1 || 12 < month) goto invalid;

		if (*p++ != '.') goto invalid;

		u1 const day = read_number(p);
		p += 2;
		if (day < 1) goto invalid;
		static u1 const days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		if (days_in_month[month - 1] < day && (month != 2 || year % 4 != 0 || (year % 100 == 0 && year % 400 != 0) || 29 < day)) goto invalid;

		if (*p++ != '.') goto invalid;

		u1 const hour = read_number(p);
		p += 2;
		if (24 <= hour) goto invalid;

		if (*p++ != '.') goto invalid;

		u1 const minute = read_number(p);
		p += 2;
		if (60 <= minute) goto invalid;

		if (*p++ != '.') goto invalid;

		u1 const second = read_number(p);
		p += 2;
		if (60 <= second) goto invalid;

		return Date(year, month, day, hour, minute, second);
	}

invalid:
	throw std::runtime_error("invalid date");
}

std::ostream& operator <<(std::ostream& o, Date const& d)
{
	using std::setw;
	using std::setfill;
	return o << setfill('0')
		<<            (u4)d.year << '.' << setw(2) << (u4)d.month  << '.' << setw(2) << (u4)d.day << ' '
		<< setw(2) << (u4)d.hour << ':' << setw(2) << (u4)d.minute << ':' << setw(2) << (u4)d.second;
}
