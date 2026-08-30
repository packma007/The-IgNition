#ifndef DATETIME
#define DATETIME

// 날짜와 시각. 영양/식사에 대해서는 아무것도 모르는 순수 달력 타입들.
// intake.h(끼니 시각)와 day.h(하루) 양쪽에서 쓰이므로 따로 떼어 두었다.
namespace domains {

    // ---------- 날짜 ----------

    struct Date {
        int year  = 1970;
        int month = 1;    // 1..12
        int day   = 1;    // 1..31

        Date() = default;
        Date(int year, int month, int day);   // 없는 날짜면 예외

        static Date today();                  // 시스템 시계 기준 오늘
        static bool isLeapYear(int year);
        static int daysInMonth(int year, int month);

        Date next() const;                    // 다음 날
        Date prev() const;                    // 전 날
        Date plusDays(int n) const;

        int dayOfWeek() const;                // 0=일 1=월 ... 6=토
        bool isWeekend() const;

        // 1970-01-01 을 0 으로 하는 일련번호. 비교/차이 계산의 기준이 된다.
        long long dayNumber() const;
        static Date fromDayNumber(long long n);
    };

    // b - a (며칠 뒤인가). b 가 앞서면 음수.
    long long daysBetween(const Date& a, const Date& b);

    bool operator==(const Date& a, const Date& b);
    bool operator!=(const Date& a, const Date& b);
    bool operator< (const Date& a, const Date& b);
    bool operator> (const Date& a, const Date& b);
    bool operator<=(const Date& a, const Date& b);
    bool operator>=(const Date& a, const Date& b);

    // ---------- 시각 ----------

    struct TimeOfDay {
        int hour   = 0;    // 0..23
        int minute = 0;    // 0..59

        TimeOfDay() = default;
        TimeOfDay(int hour, int minute);

        static TimeOfDay now();
        int minutesOfDay() const { return hour * 60 + minute; }
    };

    bool operator==(const TimeOfDay& a, const TimeOfDay& b);
    bool operator!=(const TimeOfDay& a, const TimeOfDay& b);
    bool operator< (const TimeOfDay& a, const TimeOfDay& b);
    bool operator> (const TimeOfDay& a, const TimeOfDay& b);
    bool operator<=(const TimeOfDay& a, const TimeOfDay& b);
    bool operator>=(const TimeOfDay& a, const TimeOfDay& b);

    // ---------- 하루의 경계 ----------

    // 하루가 몇 시에 시작하는지. 기본 04:00.
    // 새벽 1시에 먹은 것은 달력상 오늘이지만 "어제" 의 식사로 친다.
    // 이렇게 두면 야식을 다음날로 넘기지 않고도 끼니가 엉뚱한 날에 붙는 일이 없다.
    struct DayBoundary {
        TimeOfDay start = TimeOfDay(4, 0);

        DayBoundary() = default;
        explicit DayBoundary(TimeOfDay start) : start(start) {}

        // 달력 날짜 + 시각 -> 그 식사가 속하는 날
        Date dateFor(const Date& calendarDate, const TimeOfDay& time) const;
    };

}

#endif
