#include "datetime.h"
#include <ctime>
#include <stdexcept>

namespace domains {

    namespace {
        // 지금 시각을 지역 시간으로 가져온다 (플랫폼마다 안전한 함수가 다르다)
        std::tm localNow() {
            std::time_t t = std::time(nullptr);
            std::tm out{};
        #if defined(_WIN32)
            localtime_s(&out, &t);
        #else
            localtime_r(&t, &out);
        #endif
            return out;
        }
    }

    // ---------- Date ----------

    Date::Date(int year, int month, int day)
        : year(year), month(month), day(day) {
        if (month < 1 || month > 12)
            throw std::invalid_argument("month must be 1..12");
        if (day < 1 || day > daysInMonth(year, month))
            throw std::invalid_argument("day is out of range for the month");
    }

    bool Date::isLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    int Date::daysInMonth(int year, int month) {
        if (month < 1 || month > 12)
            throw std::invalid_argument("month must be 1..12");
        static const int table[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (month == 2 && isLeapYear(year)) return 29;
        return table[month - 1];
    }

    Date Date::today() {
        std::tm t = localNow();
        return Date(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    }

    // 1970-01-01 을 0 으로 하는 일련번호로 바꾼다.
    // 월/일 경계와 윤년을 매번 따지지 않고 정수 하나로 다루기 위한 것.
    long long Date::dayNumber() const {
        int y = year - (month <= 2 ? 1 : 0);
        long long era = (y >= 0 ? y : y - 399) / 400;
        long long yoe = y - era * 400;                                  // 0..399
        long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // 0..146096
        return era * 146097LL + doe - 719468LL;
    }

    Date Date::fromDayNumber(long long n) {
        n += 719468LL;
        long long era = (n >= 0 ? n : n - 146096) / 146097;
        long long doe = n - era * 146097;                               // 0..146096
        long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long long y   = yoe + era * 400;
        long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);        // 0..365
        long long mp  = (5 * doy + 2) / 153;                            // 0..11
        long long d   = doy - (153 * mp + 2) / 5 + 1;
        long long m   = mp + (mp < 10 ? 3 : -9);
        return Date(static_cast<int>(y + (m <= 2 ? 1 : 0)),
                    static_cast<int>(m),
                    static_cast<int>(d));
    }

    Date Date::plusDays(int n) const { return fromDayNumber(dayNumber() + n); }
    Date Date::next() const          { return plusDays(1); }
    Date Date::prev() const          { return plusDays(-1); }

    int Date::dayOfWeek() const {
        // 1970-01-01(일련번호 0)은 목요일이므로 +4 하면 일요일이 0이 된다
        long long w = (dayNumber() + 4) % 7;
        if (w < 0) w += 7;
        return static_cast<int>(w);
    }

    bool Date::isWeekend() const {
        int w = dayOfWeek();
        return w == 0 || w == 6;
    }

    long long daysBetween(const Date& a, const Date& b) {
        return b.dayNumber() - a.dayNumber();
    }

    bool operator==(const Date& a, const Date& b) {
        return a.year == b.year && a.month == b.month && a.day == b.day;
    }
    bool operator!=(const Date& a, const Date& b) { return !(a == b); }
    bool operator< (const Date& a, const Date& b) { return a.dayNumber() < b.dayNumber(); }
    bool operator> (const Date& a, const Date& b) { return b < a; }
    bool operator<=(const Date& a, const Date& b) { return !(b < a); }
    bool operator>=(const Date& a, const Date& b) { return !(a < b); }

    // ---------- TimeOfDay ----------

    TimeOfDay::TimeOfDay(int hour, int minute) : hour(hour), minute(minute) {
        if (hour < 0 || hour > 23)
            throw std::invalid_argument("hour must be 0..23");
        if (minute < 0 || minute > 59)
            throw std::invalid_argument("minute must be 0..59");
    }

    TimeOfDay TimeOfDay::now() {
        std::tm t = localNow();
        return TimeOfDay(t.tm_hour, t.tm_min);
    }

    bool operator==(const TimeOfDay& a, const TimeOfDay& b) {
        return a.minutesOfDay() == b.minutesOfDay();
    }
    bool operator!=(const TimeOfDay& a, const TimeOfDay& b) { return !(a == b); }
    bool operator< (const TimeOfDay& a, const TimeOfDay& b) {
        return a.minutesOfDay() < b.minutesOfDay();
    }
    bool operator> (const TimeOfDay& a, const TimeOfDay& b) { return b < a; }
    bool operator<=(const TimeOfDay& a, const TimeOfDay& b) { return !(b < a); }
    bool operator>=(const TimeOfDay& a, const TimeOfDay& b) { return !(a < b); }

    // ---------- DayBoundary ----------

    Date DayBoundary::dateFor(const Date& calendarDate, const TimeOfDay& time) const {
        // 하루 시작 시각보다 이르면 아직 전날이다 (04:00 기준이면 새벽 1시는 어제)
        return (time < start) ? calendarDate.prev() : calendarDate;
    }

}
