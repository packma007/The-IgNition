#ifndef DAY
#define DAY
#include <cstddef>
#include <vector>
#include "datetime.h"
#include "intake.h"
#include "photo.h"
#include "user.h"

// "하루" 를 나타내는 곳.
// 날짜(Date) + 그날의 영양 장부(DailyNutrition) 를 한 덩어리로 묶는다.
//
// 계산은 intake.h 가, 달력은 datetime.h 가 담당하고 여기서는 붙이기만 한다.
// 그날 찍은 사진(photo.h)도 여기에 함께 매달린다.
// 여러 날을 모으는 것은 calendar.h 가 한다.
namespace domains {

    class Day {
    public:
        Day(Date date, NutritionGoal goal);

        // 사용자 정보에서 목표를 뽑아 하루를 만드는 간편 함수.
        // 활동량은 user.activityLevel() 에서 온다.
        static Day forUser(Date date, const User& user);
        static Day forUser(Date date, const User& user, ActivityLevel level);

        const Date& date() const { return date_; }
        void setDate(Date date) { date_ = date; }

        // 달력상 날짜와 시각이 주어졌을 때, 그 식사가 이 하루에 속하는가.
        // 하루 경계(기본 04:00)를 적용하므로 새벽에 먹은 것은 전날로 간다.
        bool owns(const Date& calendarDate,
                  const TimeOfDay& time,
                  const DayBoundary& boundary = DayBoundary{}) const;

        // 장부 본체. 세세한 조회는 여기서 직접 꺼내 쓴다.
        DailyNutrition& nutrition() { return nutrition_; }
        const DailyNutrition& nutrition() const { return nutrition_; }

        // ---- 자주 쓰는 것들 바로가기 ----
        void addMeal(Meal meal);
        void reset();                                  // 기록만 비움, 목표는 유지
        void setGoal(NutritionGoal goal);

        const NutritionGoal& goal() const { return nutrition_.goal(); }
        const Macros& consumed() const { return nutrition_.consumed(); }
        const Macros& remaining() const { return nutrition_.remaining(); }

        Macros consumedUpTo(MealTime t) const { return nutrition_.consumedUpTo(t); }
        Macros remainingUpTo(MealTime t) const { return nutrition_.remainingUpTo(t); }

        // 시각 기준. "지금까지" 를 보려면 TimeOfDay::now() 를 넘긴다.
        Macros consumedUntil(const TimeOfDay& cutoff) const;
        Macros remainingUntil(const TimeOfDay& cutoff) const;

        double consumedCalories() const { return nutrition_.consumedCalories(); }
        double remainingCalories() const { return nutrition_.remainingCalories(); }
        double achievedRatio() const { return nutrition_.achievedRatio(); }
        bool isOverCalories() const { return nutrition_.isOverCalories(); }

        const std::vector<Meal>& meals() const { return nutrition_.intake().meals(); }
        std::size_t mealCount() const { return nutrition_.intake().size(); }

        // ---- 그날 찍은 사진 ----
        // 사진은 끼니와 별개로 날짜에 붙는다. 나중에 시각으로 끼니와 짝지을 수 있다.
        void addPhoto(Photo photo);
        void clearPhotos();

        const std::vector<Photo>& photos() const { return photos_; }
        std::size_t photoCount() const { return photos_.size(); }

        // 아래 두 함수가 돌려주는 포인터는 photos_ 안을 가리킨다.
        // addPhoto() 로 사진을 더 넣으면 무효가 되므로 그때그때 다시 받아 쓴다.

        // 그 시각 앞뒤 windowMinutes 안에 찍힌 사진들
        std::vector<const Photo*> photosNear(const TimeOfDay& time,
                                             int windowMinutes = 60) const;

        // 그 끼니를 먹은 시각 근처에 찍힌 사진들
        std::vector<const Photo*> photosFor(const Meal& meal,
                                            int windowMinutes = 60) const;

        bool isEmpty() const { return mealCount() == 0 && photos_.empty(); }

    private:
        Date date_;
        DailyNutrition nutrition_;
        std::vector<Photo> photos_;
    };

}

#endif
