#include "calendar.h"
#include <stdexcept>
#include <utility>

namespace domains {

    Calendar::Calendar(NutritionGoal defaultGoal)
        : defaultGoal_(defaultGoal) {}

    Calendar Calendar::forUser(const User& user,
                               ActivityLevel level,
                               MacroRatio ratio) {
        return Calendar(NutritionGoal::forUser(user, level, ratio));
    }

    // ---------- 하루 꺼내기 ----------

    Day& Calendar::day(const Date& date) {
        std::map<Date, Day>::iterator it = days_.find(date);
        if (it != days_.end()) return it->second;
        // Day 에는 기본 생성자가 없으므로 operator[] 대신 insert 로 넣는다
        return days_.insert(std::make_pair(date, Day(date, defaultGoal_)))
                    .first->second;
    }

    Day* Calendar::find(const Date& date) {
        std::map<Date, Day>::iterator it = days_.find(date);
        return it == days_.end() ? nullptr : &it->second;
    }

    const Day* Calendar::find(const Date& date) const {
        std::map<Date, Day>::const_iterator it = days_.find(date);
        return it == days_.end() ? nullptr : &it->second;
    }

    bool Calendar::has(const Date& date) const {
        return days_.find(date) != days_.end();
    }

    bool Calendar::remove(const Date& date) {
        return days_.erase(date) > 0;
    }

    void Calendar::clear() {
        days_.clear();
    }

    // ---------- 넣기 ----------

    Date Calendar::addMeal(const Date& calendarDate, Meal meal) {
        Date target = boundary_.dateFor(calendarDate, meal.clock());
        day(target).addMeal(std::move(meal));
        return target;
    }

    Date Calendar::addPhoto(Photo photo) {
        Date target = photo.belongsTo(boundary_);
        day(target).addPhoto(std::move(photo));
        return target;
    }

    std::size_t Calendar::addPhotos(const std::vector<Photo>& photos) {
        for (std::size_t i = 0; i < photos.size(); ++i)
            addPhoto(photos[i]);
        return photos.size();
    }

    // ---------- 둘러보기 ----------

    std::vector<Date> Calendar::dates() const {
        std::vector<Date> out;
        out.reserve(days_.size());
        for (std::map<Date, Day>::const_iterator it = days_.begin();
             it != days_.end(); ++it)
            out.push_back(it->first);
        return out;   // map 이 Date 순으로 정렬해 두므로 이미 오름차순이다
    }

    std::vector<Date> Calendar::datesInRange(const Date& from, const Date& to) const {
        if (to < from) throw std::invalid_argument("to must not be before from");
        std::vector<Date> out;
        for (std::map<Date, Day>::const_iterator it = days_.lower_bound(from);
             it != days_.end() && it->first <= to; ++it)
            out.push_back(it->first);
        return out;
    }

    std::vector<Date> Calendar::datesInMonth(int year, int month) const {
        Date first(year, month, 1);
        Date last(year, month, Date::daysInMonth(year, month));
        return datesInRange(first, last);
    }

    Date Calendar::firstDate() const {
        if (days_.empty()) throw std::runtime_error("calendar is empty");
        return days_.begin()->first;
    }

    Date Calendar::lastDate() const {
        if (days_.empty()) throw std::runtime_error("calendar is empty");
        return days_.rbegin()->first;
    }

    // ---------- 합계 ----------

    std::size_t Calendar::mealCount() const {
        std::size_t n = 0;
        for (std::map<Date, Day>::const_iterator it = days_.begin();
             it != days_.end(); ++it)
            n += it->second.mealCount();
        return n;
    }

    std::size_t Calendar::photoCount() const {
        std::size_t n = 0;
        for (std::map<Date, Day>::const_iterator it = days_.begin();
             it != days_.end(); ++it)
            n += it->second.photoCount();
        return n;
    }

    Macros Calendar::totalInRange(const Date& from, const Date& to) const {
        if (to < from) throw std::invalid_argument("to must not be before from");
        Macros sum;
        for (std::map<Date, Day>::const_iterator it = days_.lower_bound(from);
             it != days_.end() && it->first <= to; ++it)
            sum += it->second.consumed();
        return sum;
    }

    std::size_t Calendar::recordedDaysInRange(const Date& from, const Date& to) const {
        if (to < from) throw std::invalid_argument("to must not be before from");
        std::size_t n = 0;
        for (std::map<Date, Day>::const_iterator it = days_.lower_bound(from);
             it != days_.end() && it->first <= to; ++it)
            if (it->second.mealCount() > 0) ++n;
        return n;
    }

    Macros Calendar::averageInRange(const Date& from, const Date& to) const {
        std::size_t n = recordedDaysInRange(from, to);
        if (n == 0) return Macros();
        return totalInRange(from, to) * (1.0 / static_cast<double>(n));
    }

    // ---------- 설정 ----------

    void Calendar::setGoalFor(const Date& date, NutritionGoal goal) {
        day(date).setGoal(goal);
    }

}
