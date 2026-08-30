#include "inventory.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace domains {

    // 재고를 안 세는 메뉴의 "남은 양".
    // 하루에 이만큼 나가는 메뉴는 없으므로 사실상 무한대이고,
    // 그러면서도 곱하거나 더해도 넘치지 않는 크기다.
    const double kUnlimited = 1e9;

    namespace {

        const double kEps = 1e-9;

        // x 이하로 팔 수 있는 가장 큰 양. 없으면 0.
        //
        // Menu::normalize() 는 위로 올린다 - 100g 단위로 파는 메뉴에 150g 을 주면
        // 200g 을 돌려준다. 재고에서는 그걸 쓰면 남은 것보다 많이 팔게 되므로
        // 반대 방향이 필요하다.
        double floorToSellable(const Menu& menu, double x) {
            if (x <= 0.0) return 0.0;

            if (const DiscreteMenu* d = dynamic_cast<const DiscreteMenu*>(&menu)) {
                long long n = static_cast<long long>(std::floor(x + kEps));
                if (n < d->minCount()) return 0.0;
                n -= (n - d->minCount()) % d->step();      // 묶음 단위로 내림
                return static_cast<double>(n);
            }

            if (const ContinuousMenu* c = dynamic_cast<const ContinuousMenu*>(&menu)) {
                double a = x;
                if (c->maxAmount() > 0.0 && a > c->maxAmount()) a = c->maxAmount();
                if (c->step() > 0.0) a = std::floor(a / c->step() + kEps) * c->step();
                if (a < c->minAmount() - kEps) return 0.0;   // 최소 판매량에 못 미친다
                return a;
            }

            // 우리가 모르는 Menu 파생 클래스. 판매 단위를 물어볼 길이 없다.
            // normalize() 가 단조증가라는 것만 믿고 x 이하에 걸리는 가장 큰 값을 찾는다.
            // 새 파생 클래스가 생겨도 재고가 조용히 틀리지 않게 하려는 것이다.
            if (menu.isValidAmount(x)) return x;

            double best = menu.normalize(0.0);
            if (best > x + kEps) return 0.0;      // 최소 판매량조차 넘는다

            double lo = 0.0, hi = x;
            for (int i = 0; i < 60; ++i) {
                double mid = 0.5 * (lo + hi);
                double v = menu.normalize(mid);
                if (v <= x + kEps) { best = v; lo = mid; }
                else               { hi = mid; }
            }
            return best;
        }

        void eraseAt(std::vector<Hold>& v, std::size_t i) {
            v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
        }

    }

    // ---------- 재고 상태 ----------

    std::string describe(StockState state) {
        switch (state) {
            case StockState::Plenty:  return "넉넉함";
            case StockState::Low:     return "얼마 안 남음";
            case StockState::SoldOut: return "매진";
        }
        return "";
    }

    // ---------- StockItem ----------

    StockItem::StockItem(std::string menuName, std::string unit, double prepared)
        : menuName(std::move(menuName)), unit(std::move(unit)), prepared(prepared) {
        if (this->prepared < 0.0)
            throw std::invalid_argument("prepared must be >= 0");
    }

    double StockItem::available() const {
        double left = prepared - held - sold;
        return left > 0.0 ? left : 0.0;
    }

    double StockItem::remaining() const {
        double left = prepared - sold;
        return left > 0.0 ? left : 0.0;
    }

    bool StockItem::isSoldOut() const {
        return available() <= kEps;
    }

    StockState StockItem::state() const {
        if (isSoldOut()) return StockState::SoldOut;
        if (lowMark > 0.0 && available() <= lowMark + kEps) return StockState::Low;
        return StockState::Plenty;
    }

    // ---------- Hold ----------

    Hold::Hold(HoldId id, std::string menuName, double amount, TimeOfDay until)
        : id(id), menuName(std::move(menuName)), amount(amount), until(until) {}

    // ---------- DailyStock ----------

    DailyStock::DailyStock()
        : date_(Date::today()) {}

    DailyStock::DailyStock(const Date& date)
        : date_(date) {}

    StockItem* DailyStock::locate(const std::string& menuName) {
        std::map<std::string, StockItem>::iterator it = items_.find(menuName);
        return it == items_.end() ? 0 : &it->second;
    }

    const StockItem* DailyStock::locate(const std::string& menuName) const {
        std::map<std::string, StockItem>::const_iterator it = items_.find(menuName);
        return it == items_.end() ? 0 : &it->second;
    }

    // ---- 아침에 채우기 ----

    void DailyStock::setPrepared(const std::string& menuName,
                                 const std::string& unit,
                                 double amount) {
        if (menuName.empty())
            throw std::invalid_argument("menuName must not be empty");
        if (amount < 0.0)
            throw std::invalid_argument("prepared must be >= 0");

        StockItem* item = locate(menuName);
        if (!item) {
            items_.insert(std::make_pair(menuName, StockItem(menuName, unit, amount)));
            return;
        }
        // 이미 판 것을 없던 일로 만들 수는 없다. 그런 숫자를 조용히 받으면
        // 남은 양이 음수가 되고, 그 뒤로는 아무 계산도 믿을 수 없게 된다.
        if (amount < item->sold - kEps)
            throw std::invalid_argument("이미 나간 양보다 적게 잡을 수 없습니다");

        item->unit     = unit;
        item->prepared = amount;
    }

    void DailyStock::setPrepared(const Menu& menu, double amount) {
        setPrepared(menu.name(), menu.unit(), amount);
    }

    void DailyStock::addPrepared(const std::string& menuName, double amount) {
        if (amount < 0.0)
            throw std::invalid_argument("amount must be >= 0");

        StockItem* item = locate(menuName);
        if (!item) {
            // 안 세던 메뉴를 이제부터 센다. 단위는 나중에 setPrepared(Menu&) 가 채운다.
            setPrepared(menuName, std::string(), amount);
            return;
        }
        item->prepared += amount;
    }

    void DailyStock::setLowMark(const std::string& menuName, double amount) {
        if (amount < 0.0)
            throw std::invalid_argument("lowMark must be >= 0");

        StockItem* item = locate(menuName);
        if (!item)
            throw std::invalid_argument("재고를 세고 있지 않은 메뉴입니다: " + menuName);
        item->lowMark = amount;
    }

    bool DailyStock::remove(const std::string& menuName) {
        if (items_.erase(menuName) == 0) return false;

        for (std::size_t i = 0; i < holds_.size(); ) {
            if (holds_[i].menuName == menuName) eraseAt(holds_, i);
            else ++i;
        }
        return true;
    }

    void DailyStock::clear() {
        items_.clear();
        holds_.clear();
    }

    // ---- 들여다보기 ----

    bool DailyStock::tracks(const std::string& menuName) const {
        return locate(menuName) != 0;
    }

    const StockItem* DailyStock::find(const std::string& menuName) const {
        return locate(menuName);
    }

    std::vector<StockItem> DailyStock::items() const {
        std::vector<StockItem> out;
        out.reserve(items_.size());
        for (std::map<std::string, StockItem>::const_iterator it = items_.begin();
             it != items_.end(); ++it)
            out.push_back(it->second);
        return out;   // map 이 이름 순으로 들고 있으므로 이미 오름차순이다
    }

    std::vector<std::string> DailyStock::names() const {
        std::vector<std::string> out;
        out.reserve(items_.size());
        for (std::map<std::string, StockItem>::const_iterator it = items_.begin();
             it != items_.end(); ++it)
            out.push_back(it->first);
        return out;
    }

    double DailyStock::available(const std::string& menuName) const {
        const StockItem* item = locate(menuName);
        return item ? item->available() : kUnlimited;
    }

    bool DailyStock::has(const std::string& menuName, double amount) const {
        const StockItem* item = locate(menuName);
        if (!item) return true;                       // 안 세는 메뉴는 늘 있다
        return amount <= item->available() + kEps;
    }

    StockState DailyStock::state(const std::string& menuName) const {
        const StockItem* item = locate(menuName);
        return item ? item->state() : StockState::Plenty;
    }

    double DailyStock::capFor(const Menu& menu) const {
        const StockItem* item = locate(menu.name());
        if (!item) return kUnlimited;
        return floorToSellable(menu, item->available());
    }

    // ---- 잡기 / 확정 / 풀기 ----

    HoldId DailyStock::hold(const std::string& menuName, double amount, TimeOfDay until) {
        if (amount <= 0.0)
            throw std::invalid_argument("amount must be > 0");

        StockItem* item = locate(menuName);
        if (item && amount > item->available() + kEps)
            return 0;                                 // 매진. 오류가 아니라 상태다

        HoldId id = nextHoldId_++;
        holds_.push_back(Hold(id, menuName, amount, until));
        if (item) item->held += amount;
        return id;
    }

    bool DailyStock::release(HoldId id) {
        for (std::size_t i = 0; i < holds_.size(); ++i) {
            if (holds_[i].id != id) continue;

            StockItem* item = locate(holds_[i].menuName);
            if (item) {
                item->held -= holds_[i].amount;
                if (item->held < 0.0) item->held = 0.0;
            }
            eraseAt(holds_, i);
            return true;
        }
        return false;
    }

    bool DailyStock::commit(HoldId id) {
        for (std::size_t i = 0; i < holds_.size(); ++i) {
            if (holds_[i].id != id) continue;

            StockItem* item = locate(holds_[i].menuName);
            if (item) {
                // 잡을 때는 안 세던 메뉴가 그 사이에 세는 메뉴가 됐을 수 있다.
                // 그때 held 에는 이 표의 몫이 안 들어 있으므로 있는 만큼만 뺀다.
                double back = std::min(holds_[i].amount, item->held);
                item->held -= back;
                item->sold += holds_[i].amount;
            }
            eraseAt(holds_, i);
            return true;
        }
        return false;
    }

    std::size_t DailyStock::expire(TimeOfDay now) {
        std::size_t freed = 0;
        for (std::size_t i = 0; i < holds_.size(); ) {
            if (holds_[i].until < now) {
                StockItem* item = locate(holds_[i].menuName);
                if (item) {
                    item->held -= holds_[i].amount;
                    if (item->held < 0.0) item->held = 0.0;
                }
                eraseAt(holds_, i);
                ++freed;
            } else {
                ++i;
            }
        }
        return freed;
    }

    const Hold* DailyStock::findHold(HoldId id) const {
        for (std::size_t i = 0; i < holds_.size(); ++i)
            if (holds_[i].id == id) return &holds_[i];
        return 0;
    }

    std::vector<Hold> DailyStock::holds() const {
        return holds_;
    }

    double DailyStock::heldFor(const std::string& menuName) const {
        const StockItem* item = locate(menuName);
        if (item) return item->held;

        // 안 세는 메뉴도 표는 발급되므로 표를 세어 답한다
        double total = 0.0;
        for (std::size_t i = 0; i < holds_.size(); ++i)
            if (holds_[i].menuName == menuName) total += holds_[i].amount;
        return total;
    }

    // ---- 표 없이 바로 ----

    bool DailyStock::sell(const std::string& menuName, double amount) {
        if (amount <= 0.0)
            throw std::invalid_argument("amount must be > 0");

        StockItem* item = locate(menuName);
        if (!item) return true;                       // 안 세는 메뉴는 그냥 통과
        if (amount > item->available() + kEps) return false;

        item->sold += amount;
        return true;
    }

    bool DailyStock::refund(const std::string& menuName, double amount) {
        if (amount <= 0.0)
            throw std::invalid_argument("amount must be > 0");

        StockItem* item = locate(menuName);
        if (!item) return true;
        if (amount > item->sold + kEps) return false;

        item->sold -= amount;
        if (item->sold < 0.0) item->sold = 0.0;
        return true;
    }

    // ---- 마감과 점검 ----

    std::vector<std::string> DailyStock::soldOutNames() const {
        std::vector<std::string> out;
        for (std::map<std::string, StockItem>::const_iterator it = items_.begin();
             it != items_.end(); ++it)
            if (it->second.state() == StockState::SoldOut) out.push_back(it->first);
        return out;
    }

    std::vector<std::string> DailyStock::lowNames() const {
        std::vector<std::string> out;
        for (std::map<std::string, StockItem>::const_iterator it = items_.begin();
             it != items_.end(); ++it)
            if (it->second.state() == StockState::Low) out.push_back(it->first);
        return out;
    }

    std::vector<std::string> DailyStock::untracked(const WeeklyMenu& menu) const {
        std::vector<std::string> out;
        const std::vector<MenuPtr>& list = menu.menus();
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (!list[i]) continue;
            if (!tracks(list[i]->name())) out.push_back(list[i]->name());
        }
        return out;
    }

    double DailyStock::totalRemaining() const {
        double total = 0.0;
        for (std::map<std::string, StockItem>::const_iterator it = items_.begin();
             it != items_.end(); ++it)
            total += it->second.remaining();
        return total;
    }

    // ---------- StockBook ----------

    void StockBook::set(const DailyStock& day) {
        days_.erase(day.date());
        days_.insert(std::make_pair(day.date(), day));
    }

    bool StockBook::remove(const Date& date) {
        return days_.erase(date) > 0;
    }

    void StockBook::clear() {
        days_.clear();
    }

    const DailyStock* StockBook::forDate(const Date& date) const {
        std::map<Date, DailyStock>::const_iterator it = days_.find(date);
        return it == days_.end() ? 0 : &it->second;
    }

    DailyStock* StockBook::forDate(const Date& date) {
        std::map<Date, DailyStock>::iterator it = days_.find(date);
        return it == days_.end() ? 0 : &it->second;
    }

    DailyStock& StockBook::dayOf(const Date& date) {
        std::map<Date, DailyStock>::iterator it = days_.find(date);
        if (it == days_.end())
            it = days_.insert(std::make_pair(date, DailyStock(date))).first;
        return it->second;
    }

    bool StockBook::has(const Date& date) const {
        return forDate(date) != 0;
    }

    std::vector<Date> StockBook::dates() const {
        std::vector<Date> out;
        out.reserve(days_.size());
        for (std::map<Date, DailyStock>::const_iterator it = days_.begin();
             it != days_.end(); ++it)
            out.push_back(it->first);
        return out;   // map 이 Date 순으로 들고 있으므로 이미 오름차순이다
    }

    // ---------- 아침에 재고 세우기 ----------

    DailyStock openingStock(const Date& date, const WeeklyMenu& menu,
                            double gramsEach, double piecesEach) {
        if (gramsEach < 0.0 || piecesEach < 0.0)
            throw std::invalid_argument("amount must be >= 0");

        DailyStock stock(date);
        const std::vector<MenuPtr>& list = menu.menus();
        for (std::size_t i = 0; i < list.size(); ++i) {
            const MenuPtr& m = list[i];
            if (!m) continue;

            double amount = (m->divisibility() == Divisibility::Discrete)
                          ? piecesEach : gramsEach;
            stock.setPrepared(*m, amount);
        }
        return stock;
    }

}
