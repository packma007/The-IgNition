#include "weeklymenu.h"
#include <sstream>
#include <stdexcept>

namespace domains {

    // ---------- 주차 계산 ----------

    Date WeeklyMenu::weekStartOf(const Date& date) {
        // dayOfWeek() 은 0=일 이므로 그만큼 뒤로 물리면 그 주의 일요일이다
        return date.plusDays(-date.dayOfWeek());
    }

    bool WeeklyMenu::isSameWeek(const Date& a, const Date& b) {
        return weekStartOf(a) == weekStartOf(b);
    }

    WeeklyMenu::WeeklyMenu()
        : weekStart_(weekStartOf(Date::today())) {}

    WeeklyMenu::WeeklyMenu(const Date& anyDay)
        : weekStart_(weekStartOf(anyDay)) {}

    Date WeeklyMenu::weekEnd() const {
        return weekStart_.plusDays(6);
    }

    bool WeeklyMenu::covers(const Date& date) const {
        return weekStartOf(date) == weekStart_;
    }

    // ---------- 메뉴 채우기 ----------

    void WeeklyMenu::add(MenuPtr menu) {
        if (!menu) throw std::invalid_argument("menu must not be null");

        for (std::size_t i = 0; i < menus_.size(); ++i) {
            if (menus_[i]->name() == menu->name()) {   // 같은 이름은 덮어쓴다
                menus_[i] = menu;
                return;
            }
        }
        if (isFull()) {
            std::ostringstream o;
            o << "한 주에 담을 수 있는 메뉴는 " << capacity() << "가지입니다";
            throw std::length_error(o.str());
        }
        menus_.push_back(menu);
    }

    bool WeeklyMenu::remove(const std::string& name) {
        for (std::size_t i = 0; i < menus_.size(); ++i) {
            if (menus_[i]->name() == name) {
                menus_.erase(menus_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    void WeeklyMenu::clear() {
        menus_.clear();
    }

    const MenuPtr& WeeklyMenu::at(std::size_t i) const {
        if (i >= menus_.size())
            throw std::out_of_range("menu index is out of range");
        return menus_[i];
    }

    MenuPtr WeeklyMenu::find(const std::string& name) const {
        for (std::size_t i = 0; i < menus_.size(); ++i)
            if (menus_[i]->name() == name) return menus_[i];
        return MenuPtr();
    }

    bool WeeklyMenu::contains(const std::string& name) const {
        return static_cast<bool>(find(name));
    }

    std::vector<std::string> WeeklyMenu::names() const {
        std::vector<std::string> out;
        out.reserve(menus_.size());
        for (std::size_t i = 0; i < menus_.size(); ++i)
            out.push_back(menus_[i]->name());
        return out;
    }

    WeeklyMenu WeeklyMenu::nextWeek() const {
        return WeeklyMenu(weekStart_.plusDays(7));
    }

    // ---------- MenuBook ----------

    void MenuBook::set(const WeeklyMenu& week) {
        weeks_.erase(week.weekStart());
        weeks_.insert(std::make_pair(week.weekStart(), week));
    }

    bool MenuBook::remove(const Date& anyDay) {
        return weeks_.erase(WeeklyMenu::weekStartOf(anyDay)) > 0;
    }

    void MenuBook::clear() {
        weeks_.clear();
    }

    const WeeklyMenu* MenuBook::forDate(const Date& date) const {
        std::map<Date, WeeklyMenu>::const_iterator it =
            weeks_.find(WeeklyMenu::weekStartOf(date));
        return it == weeks_.end() ? 0 : &it->second;
    }

    WeeklyMenu* MenuBook::forDate(const Date& date) {
        std::map<Date, WeeklyMenu>::iterator it =
            weeks_.find(WeeklyMenu::weekStartOf(date));
        return it == weeks_.end() ? 0 : &it->second;
    }

    WeeklyMenu& MenuBook::weekOf(const Date& date) {
        Date start = WeeklyMenu::weekStartOf(date);
        std::map<Date, WeeklyMenu>::iterator it = weeks_.find(start);
        if (it == weeks_.end())
            it = weeks_.insert(std::make_pair(start, WeeklyMenu(start))).first;
        return it->second;
    }

    bool MenuBook::has(const Date& date) const {
        return forDate(date) != 0;
    }

    std::vector<Date> MenuBook::weekStarts() const {
        std::vector<Date> out;
        out.reserve(weeks_.size());
        for (std::map<Date, WeeklyMenu>::const_iterator it = weeks_.begin();
             it != weeks_.end(); ++it)
            out.push_back(it->first);
        return out;   // map 이 Date 순으로 들고 있으므로 이미 오름차순이다
    }

}
