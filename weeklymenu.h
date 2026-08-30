#ifndef WEEKLYMENU
#define WEEKLYMENU
#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include "datetime.h"
#include "domains.h"

// 한 주 동안 파는 메뉴판.
//
// 우리는 아무 때나 아무거나 파는 곳이 아니다. 한 주에 15가지를 정해 놓고
// 일요일부터 토요일까지 그대로 판다. 다음 주가 되면 15가지가 통째로 바뀐다.
//
//   MenuBook
//   ├ 2026-08-30(일) ─ WeeklyMenu ─ 메뉴 15개   <- 이번 주
//   ├ 2026-09-06(일) ─ WeeklyMenu ─ 메뉴 15개   <- 다음 주
//   └ ...
//
// 왜 주 단위로 묶는가:
//   - 유저가 화요일에 고른 메뉴는 금요일에도 있어야 한다. 주 안에서는 목록이 고정이다.
//   - 지난 주 기록을 다시 열었을 때 그때의 메뉴판이 그대로 보여야 한다.
//     메뉴 목록 하나만 들고 있으면 다음 주로 넘어가는 순간 지난 기록이 뭘 먹은 건지 알 수 없게 된다.
//
// 주의 시작은 일요일로 고정한다 (Date::dayOfWeek() 이 0=일 이므로 계산이 한 줄이다).
namespace domains {

    class WeeklyMenu {
    public:
        // 한 주에 파는 가짓수
        static std::size_t capacity() { return 15; }

        // 그 날짜가 속한 주의 일요일
        static Date weekStartOf(const Date& date);
        static bool isSameWeek(const Date& a, const Date& b);

        WeeklyMenu();                            // 오늘이 속한 주
        explicit WeeklyMenu(const Date& anyDay); // 그 날이 속한 주로 맞춘다

        // ---- 주차 ----
        const Date& weekStart() const { return weekStart_; }   // 일요일
        Date weekEnd() const;                                  // 토요일
        bool covers(const Date& date) const;

        // ---- 메뉴 채우기 ----
        // 널이면 예외. 이름이 겹치면 그 자리를 덮어쓴다 (가짓수가 늘지 않는다).
        // 15개가 찬 상태에서 새 이름을 넣으면 예외 - 조용히 버리면 메뉴판에
        // 없는 메뉴가 화면에만 있는 상태가 된다.
        void add(MenuPtr menu);
        bool remove(const std::string& name);    // 지웠으면 true
        void clear();

        // ---- 들여다보기 ----
        std::size_t size() const { return menus_.size(); }
        bool empty() const { return menus_.empty(); }
        bool isFull() const { return menus_.size() >= capacity(); }
        std::size_t slotsLeft() const { return capacity() - menus_.size(); }

        const std::vector<MenuPtr>& menus() const { return menus_; }
        const MenuPtr& at(std::size_t i) const;              // 범위 밖이면 예외
        MenuPtr find(const std::string& name) const;         // 없으면 널
        bool contains(const std::string& name) const;
        std::vector<std::string> names() const;

        // 다음 주 빈 메뉴판. 주차만 옮기고 내용은 비운다.
        WeeklyMenu nextWeek() const;

    private:
        Date weekStart_;
        std::vector<MenuPtr> menus_;
    };

    // ---------- 주차별 메뉴판 모음 ----------

    // 어느 날짜의 메뉴판이든 여기서 꺼낸다. 날짜를 주면 그 주의 것을 찾아 준다.
    class MenuBook {
    public:
        // 같은 주의 메뉴판이 이미 있으면 통째로 바꾼다
        void set(const WeeklyMenu& week);
        bool remove(const Date& anyDay);         // 지웠으면 true
        void clear();

        // 그 날짜가 속한 주의 메뉴판. 없으면 널.
        const WeeklyMenu* forDate(const Date& date) const;
        WeeklyMenu* forDate(const Date& date);

        // 없으면 그 주의 빈 메뉴판을 만들어 돌려준다
        WeeklyMenu& weekOf(const Date& date);

        bool has(const Date& date) const;

        const std::map<Date, WeeklyMenu>& weeks() const { return weeks_; }
        std::size_t size() const { return weeks_.size(); }
        bool empty() const { return weeks_.empty(); }
        std::vector<Date> weekStarts() const;    // 오름차순

    private:
        std::map<Date, WeeklyMenu> weeks_;       // 열쇠는 그 주의 일요일
    };

}

#endif
