#ifndef CALENDAR
#define CALENDAR
#include <map>
#include <vector>
#include <cstddef>
#include "datetime.h"
#include "day.h"
#include "intake.h"
#include "photo.h"
#include "user.h"

// 여러 날을 날짜별로 담아 두는 저장소.
//
// 날짜 하나에 Day 하나가 붙고, 그 Day 안에 그날의 식사와 사진이 들어간다.
// 몇 달치 사진을 한 번에 밀어 넣으면 찍힌 시각을 보고 알맞은 날에 알아서 걸린다.
//
//   Calendar
//   └ 2026-06-14 ─ Day ─ 식사 3개 / 사진 5장
//     2026-06-15 ─ Day ─ 식사 2개 / 사진 1장
//     ...
namespace domains {

    class Calendar {
    public:
        // 새 날을 만들 때 쓸 기본 목표.
        // 과거 날짜를 나중에 만들면 그때의 몸 상태가 아니라 이 목표가 붙으므로,
        // 정확히 하려면 setGoalFor() 로 그날의 목표를 따로 넣어 준다.
        explicit Calendar(NutritionGoal defaultGoal);

        static Calendar forUser(const User& user,
                                ActivityLevel level = ActivityLevel::Light,
                                MacroRatio ratio = MacroRatio{});

        // ---- 하루 꺼내기 ----
        Day& day(const Date& date);                  // 없으면 새로 만든다
        Day* find(const Date& date);                 // 없으면 nullptr
        const Day* find(const Date& date) const;
        bool has(const Date& date) const;
        bool remove(const Date& date);               // 지웠으면 true

        // ---- 넣기 ----
        // 달력상 날짜와 시각을 주면 하루 경계를 적용해 알맞은 날에 넣는다.
        // 넣은 곳의 날짜를 돌려준다.
        Date addMeal(const Date& calendarDate, Meal meal);
        Date addPhoto(Photo photo);                  // 사진은 자기 시각을 알고 있다

        // 몇 달치를 한 번에. 넣은 장수를 돌려준다.
        std::size_t addPhotos(const std::vector<Photo>& photos);

        // ---- 둘러보기 ----
        const std::map<Date, Day>& days() const { return days_; }
        std::size_t size() const { return days_.size(); }
        bool empty() const { return days_.empty(); }
        void clear();

        std::vector<Date> dates() const;                          // 오름차순
        std::vector<Date> datesInRange(const Date& from, const Date& to) const;
        std::vector<Date> datesInMonth(int year, int month) const;

        Date firstDate() const;      // 비어 있으면 예외
        Date lastDate() const;

        // ---- 합계 ----
        std::size_t mealCount() const;
        std::size_t photoCount() const;

        Macros totalInRange(const Date& from, const Date& to) const;

        // 기록이 있는 날로만 나눈 평균.
        // 기록이 없는 날은 "안 먹은 날" 이 아니라 "안 적은 날" 이므로 세지 않는다.
        Macros averageInRange(const Date& from, const Date& to) const;
        std::size_t recordedDaysInRange(const Date& from, const Date& to) const;

        // ---- 설정 ----
        const DayBoundary& boundary() const { return boundary_; }
        void setBoundary(DayBoundary boundary) { boundary_ = boundary; }

        const NutritionGoal& defaultGoal() const { return defaultGoal_; }
        void setDefaultGoal(NutritionGoal goal) { defaultGoal_ = goal; }
        void setGoalFor(const Date& date, NutritionGoal goal);

    private:
        std::map<Date, Day> days_;
        NutritionGoal defaultGoal_;
        DayBoundary boundary_;
    };

}

#endif
