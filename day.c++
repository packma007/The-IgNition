#include "day.h"
#include <utility>

namespace domains {

    Day::Day(Date date, NutritionGoal goal)
        : date_(date), nutrition_(goal) {}

    Day Day::forUser(Date date,
                     const User& user,
                     ActivityLevel level,
                     MacroRatio ratio) {
        return Day(date, NutritionGoal::forUser(user, level, ratio));
    }

    bool Day::owns(const Date& calendarDate,
                   const TimeOfDay& time,
                   const DayBoundary& boundary) const {
        return boundary.dateFor(calendarDate, time) == date_;
    }

    void Day::addMeal(Meal meal) {
        nutrition_.addMeal(std::move(meal));
    }

    void Day::reset() {
        nutrition_.reset();
    }

    void Day::setGoal(NutritionGoal goal) {
        nutrition_.setGoal(goal);
    }

    Macros Day::consumedUntil(const TimeOfDay& cutoff) const {
        return nutrition_.intake().totalUntil(cutoff);
    }

    Macros Day::remainingUntil(const TimeOfDay& cutoff) const {
        return nutrition_.goal().remaining(consumedUntil(cutoff));
    }

    // ---------- 사진 ----------

    void Day::addPhoto(Photo photo) {
        photos_.push_back(std::move(photo));
    }

    void Day::clearPhotos() {
        photos_.clear();
    }

    std::vector<const Photo*> Day::photosNear(const TimeOfDay& time,
                                              int windowMinutes) const {
        std::vector<const Photo*> found;
        for (const Photo& p : photos_)
            if (p.isNear(time, windowMinutes)) found.push_back(&p);
        return found;
    }

    std::vector<const Photo*> Day::photosFor(const Meal& meal,
                                             int windowMinutes) const {
        return photosNear(meal.clock(), windowMinutes);
    }

}
