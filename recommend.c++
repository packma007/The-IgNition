#include "recommend.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace domains {

    // 재고를 안 꽂았을 때 모든 메뉴가 갖는 상한.
    const double kNoStockCap = 1e9;

    namespace {

        // 영양소 이름을 문자열로 다시 적지 않고 domains.h 의 클래스에서 가져온다
        const std::string kCarbName    = Carbohydrate(0.0).name();
        const std::string kProteinName = Protein(0.0).name();
        const std::string kFatName     = Fat(0.0).name();

        const double kEps = 1e-9;

        double clampLow(double v) { return v > 0.0 ? v : 0.0; }

        // 0 으로 나누지 않고 비율을 낸다. 목표가 0이면 "이미 채웠다"고 본다.
        double ratio(double got, double target) {
            if (target <= kEps) return got <= kEps ? 1.0 : 2.0;
            return got / target;
        }

    }

    // ---------- Menu -> Macros ----------

    Macros macrosOf(const Menu& menu, double amount) {
        Macros m;
        if (const Nutrient* n = menu.findNutrient(kCarbName))
            m.carbG = n->amountFor(amount);
        if (const Nutrient* n = menu.findNutrient(kProteinName))
            m.proteinG = n->amountFor(amount);
        if (const Nutrient* n = menu.findNutrient(kFatName))
            m.fatG = n->amountFor(amount);
        return m;
    }

    double kcalPerUnitOf(const Menu& menu) {
        return macrosOf(menu, 1.0).calories();
    }

    // ---------- Tolerance ----------

    Tolerance::Tolerance(double kcalBandPct, double proteinFloorPct, double fatFloorPct)
        : kcalBandPct(kcalBandPct),
          proteinFloorPct(proteinFloorPct),
          fatFloorPct(fatFloorPct) {
        if (kcalBandPct < 0.0)
            throw std::invalid_argument("kcalBandPct must be >= 0");
        if (proteinFloorPct < 0.0)
            throw std::invalid_argument("proteinFloorPct must be >= 0");
        if (fatFloorPct < 0.0)
            throw std::invalid_argument("fatFloorPct must be >= 0");
    }

    // ---------- Budget ----------

    Budget::Budget(long long limit) : limit(limit) {
        if (limit < 0) throw std::invalid_argument("budget limit must be >= 0");
    }

    Budget::Budget(long long limit, double weight) : limit(limit), weight(weight) {
        if (limit < 0)   throw std::invalid_argument("budget limit must be >= 0");
        if (weight < 0.0) throw std::invalid_argument("budget weight must be >= 0");
    }

    // ---------- Issue ----------

    std::string describe(Issue issue) {
        switch (issue) {
            case Issue::CaloriesOver:  return "열량 초과";
            case Issue::CaloriesUnder: return "열량 부족";
            case Issue::ProteinShort:  return "단백질 부족";
            case Issue::FatShort:      return "지방 부족";
            case Issue::PriceOver:     return "예산 초과";
            case Issue::AmountLimited: return "양으로는 더 맞출 수 없음";
            case Issue::StockShort:    return "재료 부족";
        }
        return "";
    }

    // ---------- Pick ----------

    Pick::Pick(MenuPtr menu) : menu(menu) {
        if (!menu) throw std::invalid_argument("menu must not be null");
    }

    Pick::Pick(MenuPtr menu, double minAmount, double maxAmount)
        : menu(menu), minAmount(minAmount), maxAmount(maxAmount) {
        if (!menu) throw std::invalid_argument("menu must not be null");
        if (minAmount < 0.0) throw std::invalid_argument("minAmount must be >= 0");
        if (maxAmount < 0.0) throw std::invalid_argument("maxAmount must be >= 0");
        if (maxAmount > 0.0 && maxAmount < minAmount)
            throw std::invalid_argument("maxAmount must be >= minAmount");
    }

    Pick Pick::fixed(MenuPtr menu, double amount) {
        Pick p(menu);
        p.locked = true;
        p.amount = menu->normalize(amount);   // 팔 수 없는 양으로 고정되는 일은 없다
        return p;
    }

    std::vector<Pick> picksFrom(const WeeklyMenu& week,
                                const std::vector<std::string>& names,
                                std::vector<std::string>* missing) {
        std::vector<Pick> out;
        for (std::size_t i = 0; i < names.size(); ++i) {
            MenuPtr m = week.find(names[i]);
            if (m) out.push_back(Pick(m));
            else if (missing) missing->push_back(names[i]);
        }
        return out;
    }

    std::vector<Pick> availablePicks(const std::vector<Pick>& picks,
                                     const StockLimits& stock,
                                     std::vector<std::string>* soldOut) {
        std::vector<Pick> out;
        for (std::size_t i = 0; i < picks.size(); ++i) {
            if (!picks[i].menu) throw std::invalid_argument("menu must not be null");

            // 0 은 "오늘은 못 판다" 다. 남은 것이 최소 판매량에도 못 미치는 경우까지
            // 여기 들어온다 - 30g 남은 것을 50g 부터 파는 메뉴로 담을 수는 없다.
            if (stock.capFor(*picks[i].menu) <= kEps) {
                if (soldOut) soldOut->push_back(picks[i].menu->name());
                continue;
            }
            out.push_back(picks[i]);
        }
        return out;
    }

    std::vector<Pick> picksFrom(const WeeklyMenu& week,
                                const std::vector<std::string>& names,
                                const StockLimits& stock,
                                std::vector<std::string>* missing,
                                std::vector<std::string>* soldOut) {
        return availablePicks(picksFrom(week, names, missing), stock, soldOut);
    }

    // ---------- PlanItem / Plan ----------

    bool PlanItem::countsByUnit() const {
        return menu && menu->divisibility() == Divisibility::Discrete;
    }

    const std::string& PlanItem::unit() const {
        static const std::string none;
        return menu ? menu->unit() : none;
    }

    bool Plan::has(Issue issue) const {
        for (std::size_t i = 0; i < issues.size(); ++i)
            if (issues[i] == issue) return true;
        return false;
    }

    std::string Plan::warning() const {
        std::string out;
        for (std::size_t i = 0; i < issues.size(); ++i) {
            if (!out.empty()) out += " · ";
            out += describe(issues[i]);
        }
        return out;
    }

    std::vector<std::string> Plan::limitedMenus() const {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].atMin || items[i].atMax)
                out.push_back(items[i].menu->name());
        return out;
    }

    std::vector<std::string> Plan::stockLimitedMenus() const {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].isStockCapped())
                out.push_back(items[i].menu->name());
        return out;
    }

    const NutrientTotal* Plan::nutrient(const std::string& name) const {
        for (std::size_t i = 0; i < nutrients.size(); ++i)
            if (nutrients[i].name == name) return &nutrients[i];
        return 0;
    }

    double Plan::nutrientAmount(const std::string& name) const {
        const NutrientTotal* n = nutrient(name);
        return n ? n->amount : 0.0;
    }

    std::size_t Plan::indexOf(const std::string& menuName) const {
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].menu && items[i].menu->name() == menuName) return i;
        return npos();
    }

    bool Plan::contains(const std::string& menuName) const {
        return indexOf(menuName) != npos();
    }

    std::vector<std::string> Plan::menuNames() const {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].menu) out.push_back(items[i].menu->name());
        return out;
    }

    bool Plan::isValidComposition() const {
        return MealPlanner::isValidPickCount(items.size());
    }

    bool Plan::canAddMenu() const {
        return items.size() < MealPlanner::maxPicks();
    }

    bool Plan::canRemoveMenu() const {
        return items.size() > MealPlanner::minPicks();
    }

    std::string Plan::compositionWarning() const {
        if (isValidComposition()) return std::string();
        std::ostringstream o;
        if (items.size() < MealPlanner::minPicks())
            o << "메뉴를 " << (MealPlanner::minPicks() - items.size()) << "가지 더 골라 주세요"
              << " (한 끼에 " << MealPlanner::minPicks() << "~" << MealPlanner::maxPicks() << "가지)";
        else
            o << "한 끼에 담을 수 있는 것은 " << MealPlanner::maxPicks() << "가지까지입니다"
              << " (지금 " << items.size() << "가지)";
        return o.str();
    }

    // ---------- 양을 푸는 알고리즘 ----------

    namespace {

        // 판매 가능한 양의 격자.
        // lo 에서 시작해 step 씩. step 이 0 이면 lo..hi 사이 아무 값이나 된다.
        struct Grid {
            double lo = 0.0;
            double hi = 0.0;
            double step = 0.0;

            // 오늘 재고가 허락하는 최대와, 그것이 실제로 상한을 내렸는가.
            // 재고를 안 꽂았으면 kNoStockCap 이고 언제나 거짓이다.
            double stockCap = kNoStockCap;
            bool stockLimited = false;

            double clampTo(double x) const {
                if (x < lo) return lo;
                if (x > hi) return hi;
                return x;
            }

            // 격자 위로 내린다 / 올린다. 항상 [lo, hi] 안에 든다.
            double down(double x) const {
                double v = clampTo(x);
                if (step <= 0.0) return v;
                double k = std::floor((v - lo) / step + 1e-9);
                if (k < 0.0) k = 0.0;
                return clampTo(lo + k * step);
            }
            double up(double x) const {
                double v = clampTo(x);
                if (step <= 0.0) return v;
                double k = std::ceil((v - lo) / step - 1e-9);
                if (k < 0.0) k = 0.0;
                return clampTo(lo + k * step);
            }
            double nearest(double x) const {
                double d = down(x), u = up(x);
                return (x - d) <= (u - x) ? d : u;
            }
            bool isPoint() const { return hi - lo < 1e-9; }
        };

        // 아주 작은 값을 얹어 normalize 가 어디로 올리는지 보고 계량 단위를 알아낸다.
        // Menu 인터페이스만 쓰므로 새 Menu 파생 클래스가 생겨도 그대로 돈다.
        const double kProbe = 1e-4;

        double gridStepOf(const Menu& m, double lo) {
            if (m.divisibility() == Divisibility::Discrete) {
                for (int k = 1; k <= 64; ++k)
                    if (m.isValidAmount(lo + k)) return static_cast<double>(k);
                return 1.0;
            }
            double up = m.normalize(lo + kProbe);
            double d = up - lo;
            // 이보다 잘게 나뉘면 계량 단위가 없는 것으로 본다
            // (g/ml 로 파는 음식을 0.001 단위로 다는 저울은 없다)
            return d > 1e-3 ? d : 0.0;
        }

        // 메뉴 자신의 상한. 없으면 0.
        // ContinuousMenu::normalize 는 상한이 있으면 거기서 잘라 주므로,
        // 아주 큰 값을 넣어 보고 잘리는지로 알아낸다.
        double menuCapOf(const Menu& m) {
            const double big = 1e9;
            double v = m.normalize(big);
            return v < big - 1.0 ? v : 0.0;
        }

        Grid buildGrid(const Menu& m, const Pick& p, double budgetKcal,
                       double stockCap) {
            Grid g;
            g.lo   = m.normalize(p.minAmount > 0.0 ? p.minAmount : 0.0);
            g.step = gridStepOf(m, g.lo);

            double hi;
            if (p.maxAmount > 0.0) {
                hi = p.maxAmount;
            } else {
                // 남은 열량을 혼자 다 채우고도 남을 만큼까지만 열어 둔다.
                // 그 위로는 어차피 열량 초과라 답이 될 수 없다.
                double perUnit = kcalPerUnitOf(m);
                if (perUnit > kEps && budgetKcal > kEps)
                    hi = g.lo + (budgetKcal / perUnit) * 2.0 + g.step;
                else
                    hi = g.lo;   // 열량이 없는 메뉴거나 예산이 없다 -> 최소량 고정
            }
            // 유저가 이미 정해 둔 양은 언제나 격자 안에 들어와야 한다.
            // 그래야 나중에 그 항목도 손으로 조절할 수 있다.
            if (p.locked && p.amount > hi) hi = p.amount;

            double cap = menuCapOf(m);
            if (cap > 0.0 && hi > cap) hi = cap;

            // 재고가 마지막 상한이다. 영양도, 예산도, 유저가 건 울타리도 여기서 멈춘다 -
            // 없는 재료로 목표를 맞추는 답은 답이 아니다.
            // stockCap 은 이미 그 메뉴가 팔 수 있는 양으로 내림 보정된 값이다.
            g.stockCap = stockCap;
            if (stockCap < hi - kEps) {
                hi = stockCap;
                g.stockLimited = true;
            }

            // 유저가 건 하한보다도 재고가 적으면 하한을 내린다.
            // "밥은 200g 넘게" 는 취향이고 "150g 밖에 없다" 는 사실이다.
            // 사실이 이긴다. 대신 화면에는 재료 부족으로 뜬다.
            if (stockCap < g.lo - kEps) {
                g.lo = stockCap;
                g.stockLimited = true;
            }

            if (hi < g.lo) hi = g.lo;
            g.hi = hi;

            // 격자 위로 내린 뒤, 정말 팔 수 있는 양인지 확인한다.
            // (상한이 계량 단위에 딱 맞지 않는 메뉴가 있다)
            g.hi = g.down(g.hi);
            for (int guard = 0; guard < 8 && g.hi > g.lo + kEps; ++guard) {
                if (m.isValidAmount(g.hi)) break;
                g.hi = g.step > 0.0 ? g.hi - g.step : g.lo;
            }
            if (!m.isValidAmount(g.hi)) g.hi = g.lo;
            if (g.hi < g.lo) g.hi = g.lo;
            return g;
        }

        // +/- 버튼 한 번에 움직일 양.
        // 계량 단위가 있으면 그것이 곧 한 칸이고, 없으면 폭의 1/20 을 쓴다.
        double nudgeStepOf(const Grid& g) {
            if (g.step > 0.0) return g.step;
            double span = g.hi - g.lo;
            double s = std::floor(span / 20.0);
            return s >= 1.0 ? s : 1.0;
        }

        // ---- 목표와 벌점 ----

        struct Ctx {
            double K       = 0.0;   // 목표 열량
            double Ptar    = 0.0;   // 목표 단백질 g
            double Ffloor  = 0.0;   // 지방 하한 g
            double kscale  = 500.0; // 열량 오차를 나눌 기준
            double Blimit  = 0.0;   // 예산. 0 이면 안 걸었다
            double Bweight = 0.0;   // 값의 저울눈
        };

        Ctx contextOf(const Macros& target, const Tolerance& tol, const Budget& b) {
            Ctx c;
            c.K      = target.calories();
            c.Ptar   = target.proteinG;
            // 지방 하한은 "그 끼니 열량의 몇 %" 로 본다 (지방 1g = 9kcal)
            c.Ffloor = c.K > kEps ? c.K * tol.fatFloorPct / 9.0 : 0.0;
            c.kscale = c.K > kEps ? c.K : 500.0;
            c.Blimit = b.isSet() ? static_cast<double>(b.limit) : 0.0;
            c.Bweight = b.weight > 0.0 ? b.weight : 0.0;
            return c;
        }

        bool weighsPrice(const Ctx& c) {
            return c.Blimit > kEps && c.Bweight > 0.0;
        }

        // 낮을수록 좋다.
        // 열량은 어긋난 만큼, 단백질과 지방은 "모자란 만큼"만 벌점을 준다.
        // 넘치는 단백질과 탄수화물에는 벌점이 없다 - 그게 이 설계의 요점이다.
        //
        // 값도 같은 모양으로 붙인다: "예산을 넘은 만큼"만 벌점이다.
        // 예산보다 싸게 나왔다고 상을 주지는 않는다 - 예산은 상한이지 목표가 아니다.
        // 싸게 만드는 일은 동점일 때 싼 쪽을 고르는 것으로 이미 하고 있다.
        // (상을 주면 영양을 버리고 값을 깎는 쪽으로 답이 흘러간다)
        //
        // 모든 항이 "몇 % 어긋났나" 라는 같은 단위여서 서로 견줄 수 있다.
        double costOf(const Ctx& c, const Macros& m, long long price) {
            double v = std::fabs(m.calories() - c.K) / c.kscale
                     + 2.0 * clampLow(1.0 - ratio(m.proteinG, c.Ptar))
                     + 1.0 * clampLow(1.0 - ratio(m.fatG, c.Ffloor));
            if (weighsPrice(c))
                v += c.Bweight * clampLow(static_cast<double>(price) / c.Blimit - 1.0);
            return v;
        }

        // 한 가지 메뉴에 대한 계산 재료
        struct Var {
            const Menu* menu = 0;
            Macros unit;              // 1단위당 탄단지
            double kcalPerUnit = 0.0;
            Grid grid;
        };

        Macros scaled(const Macros& u, double x) {
            return Macros(u.carbG * x, u.proteinG * x, u.fatG * x);
        }

        // 지금 손대지 않는 것들. 탄단지와 값을 함께 들고 다닌다 -
        // 값이 벌점에 들어온 뒤로는 "나머지가 얼마어치인가"를 알아야 내 몫을 정할 수 있다.
        struct Rest {
            Macros macros;
            long long price = 0;
        };

        // skipA / skipB 를 뺀 나머지의 합
        Rest restOf(const std::vector<Var>& v, const std::vector<double>& x,
                    const Macros& base, long long basePrice,
                    std::size_t skipA, std::size_t skipB) {
            Rest r;
            r.macros = base;
            r.price  = basePrice;
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i == skipA || i == skipB) continue;
                r.macros += scaled(v[i].unit, x[i]);
                r.price  += v[i].menu->priceFor(x[i]);
            }
            return r;
        }

        // 나머지에 한 자리를 얹은 것
        Rest plus(const Rest& r, const Var& v, double x) {
            Rest o;
            o.macros = r.macros + scaled(v.unit, x);
            o.price  = r.price + v.menu->priceFor(x);
            return o;
        }

        long long priceOf(const std::vector<Var>& v, const std::vector<double>& x) {
            long long p = 0;
            for (std::size_t i = 0; i < v.size(); ++i)
                p += v[i].menu->priceFor(x[i]);
            return p;
        }

        // 나머지가 rest 로 고정됐을 때, i 번 메뉴의 양으로 시도해 볼 값들.
        //
        // 벌점 함수는 양 하나에 대해 꺾은선이다 - 열량이 목표에 닿는 지점,
        // 단백질이 목표에 닿는 지점, 지방이 하한에 닿는 지점, 그리고 값이 예산에 닿는
        // 지점에서만 기울기가 바뀐다 (격자 위에서 값은 양에 정비례하므로 이것도 직선이다).
        // 그러니 최소값은 반드시 그 네 꼭짓점이나 양 끝에 있다. 그 여섯 곳만 보면 된다.
        void candidatesFor(const Ctx& c, const Var& v, const Rest& rest,
                           double current, std::vector<double>& out) {
            out.clear();
            const Grid& g = v.grid;

            double raw[7];
            std::size_t n = 0;
            raw[n++] = g.lo;
            raw[n++] = g.hi;
            raw[n++] = current;
            if (std::fabs(v.kcalPerUnit) > kEps)
                raw[n++] = (c.K - rest.macros.calories()) / v.kcalPerUnit;
            if (v.unit.proteinG > kEps)
                raw[n++] = (c.Ptar - rest.macros.proteinG) / v.unit.proteinG;
            if (v.unit.fatG > kEps)
                raw[n++] = (c.Ffloor - rest.macros.fatG) / v.unit.fatG;
            if (weighsPrice(c) && v.menu->unitPrice() > 0)
                raw[n++] = (c.Blimit - static_cast<double>(rest.price))
                         / static_cast<double>(v.menu->unitPrice());

            for (std::size_t i = 0; i < n; ++i) {
                out.push_back(g.down(raw[i]));
                out.push_back(g.up(raw[i]));
            }

            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        }

        // rest 가 고정됐을 때 i 번의 최선의 양. 벌점이 같으면 싼 쪽을 고른다.
        double bestAmountFor(const Ctx& c, const Var& v, const Rest& rest,
                             double current, std::vector<double>& scratch,
                             double& bestCost) {
            candidatesFor(c, v, rest, current, scratch);

            double bestX = current;
            bestCost = costOf(c, rest.macros + scaled(v.unit, current),
                              rest.price + v.menu->priceFor(current));
            long long bestPrice = v.menu->priceFor(current);

            for (std::size_t k = 0; k < scratch.size(); ++k) {
                double x = scratch[k];
                long long price = v.menu->priceFor(x);
                double cost = costOf(c, rest.macros + scaled(v.unit, x),
                                     rest.price + price);
                if (cost < bestCost - 1e-12 ||
                    (cost < bestCost + 1e-12 && price < bestPrice)) {
                    bestCost = cost;
                    bestPrice = price;
                    bestX = x;
                }
            }
            return bestX;
        }

        // 한 자리씩 정확히 최소로 옮기고, 그다음 두 자리를 함께 옮긴다.
        //
        // 한 자리씩만 옮기면 "밥을 줄이면서 동시에 닭가슴살을 늘려야 나아지는" 자리에서
        // 멈춰 버린다 - 혼자 움직여서는 어느 쪽도 나아지지 않기 때문이다.
        // 그래서 i 를 후보값에 놓아 보고 j 를 다시 맞추는 짝 이동을 함께 돌린다.
        double refine(const Ctx& c, const std::vector<Var>& v,
                      const Macros& base, long long basePrice,
                      std::vector<double>& x, int maxSweeps) {
            std::vector<double> scratch, candI;
            Macros total = base;
            long long totalPrice = basePrice;
            for (std::size_t i = 0; i < v.size(); ++i) {
                total += scaled(v[i].unit, x[i]);
                totalPrice += v[i].menu->priceFor(x[i]);
            }
            double cur = costOf(c, total, totalPrice);

            for (int sweep = 0; sweep < maxSweeps; ++sweep) {
                bool improved = false;

                // ---- 한 자리씩 ----
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (v[i].grid.isPoint()) continue;
                    Rest rest = restOf(v, x, base, basePrice, i, i);
                    double cost = 0.0;
                    double nx = bestAmountFor(c, v[i], rest, x[i], scratch, cost);
                    if (cost < cur - 1e-12) { x[i] = nx; cur = cost; improved = true; }
                    else if (cost < cur + 1e-12) { x[i] = nx; }   // 같은 값이면 싼 쪽으로
                }

                // ---- 두 자리씩 ----
                for (std::size_t i = 0; i + 1 < v.size(); ++i) {
                    for (std::size_t j = i + 1; j < v.size(); ++j) {
                        if (v[i].grid.isPoint() && v[j].grid.isPoint()) continue;
                        Rest rest = restOf(v, x, base, basePrice, i, j);

                        // j 를 양 끝과 지금 자리에 놓아 보고 i 의 후보를 뽑는다.
                        // 그래야 "j 를 끝까지 줄였을 때 비로소 맞는 i" 가 후보에 든다.
                        double probes[3] = { v[j].grid.lo, v[j].grid.hi, x[j] };
                        candI.clear();
                        for (std::size_t t = 0; t < 3; ++t) {
                            candidatesFor(c, v[i], plus(rest, v[j], probes[t]),
                                          x[i], scratch);
                            candI.insert(candI.end(), scratch.begin(), scratch.end());
                        }
                        std::sort(candI.begin(), candI.end());
                        candI.erase(std::unique(candI.begin(), candI.end()), candI.end());

                        double bestXi = x[i], bestXj = x[j], bestCost = cur;
                        long long bestPrice = v[i].menu->priceFor(x[i])
                                            + v[j].menu->priceFor(x[j]);

                        for (std::size_t k = 0; k < candI.size(); ++k) {
                            Rest withI = plus(rest, v[i], candI[k]);
                            double cost = 0.0;
                            double xj = bestAmountFor(c, v[j], withI, x[j], scratch, cost);
                            long long price = v[i].menu->priceFor(candI[k])
                                            + v[j].menu->priceFor(xj);
                            if (cost < bestCost - 1e-12 ||
                                (cost < bestCost + 1e-12 && price < bestPrice)) {
                                bestCost = cost; bestPrice = price;
                                bestXi = candI[k]; bestXj = xj;
                            }
                        }
                        if (bestCost < cur - 1e-12) improved = true;
                        x[i] = bestXi; x[j] = bestXj; cur = bestCost;
                    }
                }

                if (!improved) break;
            }
            return cur;
        }

        // ---- 합계와 경고를 다시 맞춘다 ----
        //
        // 처음 계산한 직후에도, 유저가 양을 바꾼 뒤에도 똑같이 이걸 부른다.
        // 두 길이 갈리면 "추천일 때는 초록불인데 손으로 같은 양을 넣으면 빨간불" 같은
        // 일이 생긴다.
        // 조합에 실린 영양소를 하나도 버리지 않고 이름별로 더한다.
        // 탄/단/지를 먼저 놓는 것은 화면에서 늘 같은 자리에 있어야 하기 때문이다.
        // 그 뒤로는 메뉴에 나온 순서를 지킨다 (메뉴를 뺐다 넣었다 해도 줄이 안 튄다).
        void sumNutrients(Plan& plan) {
            plan.nutrients.clear();
            plan.nutrientCalories = 0.0;

            const std::string* order[3] = { &kCarbName, &kProteinName, &kFatName };
            for (std::size_t k = 0; k < 3; ++k) {
                NutrientTotal t;
                t.name = *order[k];
                t.unit = "g";
                plan.nutrients.push_back(t);
            }

            for (std::size_t i = 0; i < plan.items.size(); ++i) {
                const PlanItem& it = plan.items[i];
                if (!it.menu) continue;
                const std::vector<NutrientPtr>& ns = it.menu->nutrients();
                for (std::size_t j = 0; j < ns.size(); ++j) {
                    if (!ns[j]) continue;
                    const std::string name = ns[j]->name();

                    std::size_t at = plan.nutrients.size();
                    for (std::size_t k = 0; k < plan.nutrients.size(); ++k)
                        if (plan.nutrients[k].name == name) { at = k; break; }
                    if (at == plan.nutrients.size()) {
                        NutrientTotal t;
                        t.name = name;
                        t.unit = ns[j]->unit();
                        plan.nutrients.push_back(t);
                    }

                    plan.nutrients[at].amount   += ns[j]->amountFor(it.amount);
                    plan.nutrients[at].calories += ns[j]->caloriesFor(it.amount);
                }
            }

            // 아무 메뉴에도 없는 탄/단/지 줄은 0 인 채로 남긴다.
            // 화면에서 "단백질 0g" 이 사라지는 것보다 0 이라고 보이는 편이 낫다.
            for (std::size_t k = 0; k < plan.nutrients.size(); ++k)
                plan.nutrientCalories += plan.nutrients[k].calories;
        }

        void evaluatePlan(Plan& plan, const Tolerance& tol) {
            Ctx c = contextOf(plan.target, tol, plan.budget);

            plan.macros = Macros();
            plan.price  = 0;
            for (std::size_t i = 0; i < plan.items.size(); ++i) {
                PlanItem& it = plan.items[i];
                it.macros = macrosOf(*it.menu, it.amount);
                it.price  = it.menu->priceFor(it.amount);
                it.atMin  = it.amount <= it.minAmount + 1e-9;
                it.atMax  = it.amount >= it.maxAmount - 1e-9;
                plan.macros += it.macros;
                plan.price  += it.price;
            }
            sumNutrients(plan);

            plan.targetCalories = c.K;
            plan.calories       = plan.macros.calories();
            plan.calorieDelta   = plan.calories - c.K;
            plan.caloriePct     = ratio(plan.calories, c.K);
            plan.proteinPct     = ratio(plan.macros.proteinG, c.Ptar);
            plan.fatPct         = ratio(plan.macros.fatG, c.Ffloor);
            plan.score          = costOf(c, plan.macros, plan.price);

            if (plan.budget.isSet()) {
                plan.priceDelta = plan.price - plan.budget.limit;
                plan.pricePct   = static_cast<double>(plan.price)
                                / static_cast<double>(plan.budget.limit);
            } else {
                plan.priceDelta = 0;
                plan.pricePct   = 0.0;
            }

            plan.issues.clear();
            if (c.K > kEps) {
                double band = c.K * tol.kcalBandPct;
                if (plan.calorieDelta >  band + kEps)
                    plan.issues.push_back(Issue::CaloriesOver);
                if (plan.calorieDelta < -band - kEps)
                    plan.issues.push_back(Issue::CaloriesUnder);
            } else {
                // 예산이 이미 없다. 뭘 먹든 초과다.
                if (plan.calories > kEps) plan.issues.push_back(Issue::CaloriesOver);
            }
            if (c.Ptar > kEps &&
                plan.macros.proteinG < c.Ptar * tol.proteinFloorPct - kEps)
                plan.issues.push_back(Issue::ProteinShort);
            if (c.Ffloor > kEps && plan.macros.fatG < c.Ffloor - kEps)
                plan.issues.push_back(Issue::FatShort);

            // 예산은 딱 떨어지는 선이다. 열량처럼 밴드를 주지 않는다 -
            // 9,000원 예산에 9,300원짜리를 "거의 맞았다" 고 넘기면 결제할 때 딴소리가 된다.
            if (plan.budget.isSet() && plan.price > plan.budget.limit)
                plan.issues.push_back(Issue::PriceOver);

            // 범위를 벗어난 채로 어떤 항목이 한계에 붙어 있다면, 양으로는 더 못 맞춘다.
            // UI 는 이걸 보고 "메뉴를 바꿔 보세요" 라고 말할 수 있다.
            // (한계에 붙은 것이 없는데도 어긋난다면 양이 아니라 고른 조합 자체의 문제다)
            if (!plan.issues.empty() && !plan.limitedMenus().empty()) {
                plan.issues.push_back(Issue::AmountLimited);

                // 그 한계가 재고라면 이유를 하나 더 붙인다.
                // "더 담을 수 없다" 와 "오늘 재료가 여기까지다" 는 유저에게 다른 말이다 -
                // 앞의 것은 메뉴를 바꾸라는 뜻이고, 뒤의 것은 내일 오라는 뜻이다.
                if (!plan.stockLimitedMenus().empty())
                    plan.issues.push_back(Issue::StockShort);
            }
        }

        // 항목에 걸린 한계와 계량 단위에 맞춰 양을 보정한다
        double snapToItem(const PlanItem& it, double amount) {
            Grid g;
            g.lo = it.minAmount;
            g.hi = it.maxAmount;
            g.step = it.step;
            double v = g.nearest(amount);
            if (it.menu->isValidAmount(v)) return v;

            // 격자 밖으로 벗어난 값이면 한 칸 내려 본다. 그래도 아니면 최소량으로.
            if (g.step > 0.0) {
                double d = g.down(v);
                if (it.menu->isValidAmount(d)) return d;
            }
            return g.lo;
        }

    }

    // ---------- MealPlanner ----------

    bool MealPlanner::isValidPickCount(std::size_t n) {
        return n >= minPicks() && n <= maxPicks();
    }

    void MealPlanner::setMaxSweeps(int n) {
        if (n < 1) throw std::invalid_argument("maxSweeps must be >= 1");
        maxSweeps_ = n;
    }

    // ---------- 오늘 재고 ----------

    double MealPlanner::stockCapFor(const Menu& menu) const {
        if (!stock_) return kNoStockCap;

        double cap = stock_->capFor(menu);

        // 꽂은 쪽이 음수를 주더라도 "제한 없음" 으로 뒤집히지 않게 여기서 막는다.
        // 0 은 매진이고 음수는 있을 수 없는 값이므로 둘 다 0 으로 본다.
        return cap > 0.0 ? cap : 0.0;
    }

    bool MealPlanner::canServe(const Menu& menu) const {
        return stockCapFor(menu) > kEps;
    }

    bool MealPlanner::canAddMenu(const Plan& plan, const Menu& menu) const {
        return addBlockReason(plan, menu).empty();
    }

    std::string MealPlanner::addBlockReason(const Plan& plan, const Menu& menu) const {
        // 이미 담겨 있으면 addPick() 은 그 자리를 돌려줄 뿐 아무것도 안 한다.
        // 막힌 것이 아니므로 이유도 없다.
        if (plan.contains(menu.name())) return std::string();

        if (!canServe(menu))
            return "재료가 부족합니다 (" + menu.name() + ")";

        if (plan.items.size() >= maxPicks()) {
            std::ostringstream o;
            o << "한 끼에 담을 수 있는 것은 " << maxPicks() << "가지까지입니다";
            return o.str();
        }
        return std::string();
    }

    void MealPlanner::checkStock(const std::vector<Pick>& picks) const {
        if (!stock_) return;
        for (std::size_t i = 0; i < picks.size(); ++i) {
            if (!picks[i].menu || canServe(*picks[i].menu)) continue;
            throw std::invalid_argument("오늘 재료가 떨어진 메뉴가 있습니다: "
                                        + picks[i].menu->name());
        }
    }

    std::string StockChange::message() const {
        if (!changed()) return std::string();

        std::ostringstream o;
        if (!dropped.empty()) {
            o << "재료가 떨어져 뺐습니다: ";
            for (std::size_t i = 0; i < dropped.size(); ++i) {
                if (i) o << ", ";
                o << dropped[i];
            }
        }
        if (!shrunk.empty()) {
            if (!dropped.empty()) o << " · ";
            o << "재료가 모자라 양을 줄였습니다: ";
            for (std::size_t i = 0; i < shrunk.size(); ++i) {
                if (i) o << ", ";
                o << shrunk[i];
            }
        }
        return o.str();
    }

    StockChange MealPlanner::refreshStock(Plan& plan) const {
        StockChange out;
        if (!stock_) return out;

        // 뒤에서부터 지운다. 앞에서 지우면 뒤 번호가 밀린다.
        for (std::size_t k = plan.items.size(); k > 0; --k) {
            std::size_t i = k - 1;
            PlanItem& it = plan.items[i];
            if (!it.menu) continue;

            double cap = stockCapFor(*it.menu);

            // 다 나갔다. 0 짜리 줄로 남겨 두면 안 된다 - Menu::priceFor(0) 은
            // 최소 판매량으로 올려 계산하므로, 안 담은 것에 값이 붙어 버린다.
            if (cap <= kEps) {
                out.dropped.push_back(it.menu->name());
                plan.items.erase(plan.items.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }

            it.stockCap = cap;

            // 상한은 내리기만 한다. 그 사이에 더 만들었더라도 여기서 올리지는 않는다 -
            // 유저가 보고 있는 조절 범위가 저절로 넓어지면 그것도 화면을 못 믿게 만든다.
            // 넓히는 것은 rebalance() 나 다시 풀기로 유저가 부를 때 일어난다.
            if (cap < it.maxAmount - kEps) {
                it.maxAmount = cap;
                it.stockLimited = true;
            }
            if (cap < it.minAmount - kEps) it.minAmount = cap;
            if (it.maxAmount < it.minAmount) it.maxAmount = it.minAmount;

            if (it.amount > it.maxAmount + kEps) {
                it.amount = it.maxAmount;
                out.shrunk.push_back(it.menu->name());
            }
        }

        // 뒤에서부터 돌았으므로 이름이 거꾸로 담겼다. 화면 순서대로 돌려준다.
        std::reverse(out.shrunk.begin(), out.shrunk.end());
        std::reverse(out.dropped.begin(), out.dropped.end());

        evaluatePlan(plan, tolerance_);
        return out;
    }

    Plan MealPlanner::solve(const std::vector<Pick>& picks, const Macros& target) const {
        return solve(picks, target, Budget());
    }

    Plan MealPlanner::solve(const std::vector<Pick>& picks, const Macros& target,
                            const Budget& budget) const {
        if (!isValidPickCount(picks.size())) {
            std::ostringstream o;
            o << "한 끼에 고르는 메뉴는 " << minPicks() << "~" << maxPicks()
              << "가지여야 합니다 (지금 " << picks.size() << "가지)";
            throw std::invalid_argument(o.str());
        }
        for (std::size_t i = 0; i < picks.size(); ++i)
            if (!picks[i].menu) throw std::invalid_argument("picked menu must not be null");

        // 오늘 재료가 떨어진 것이 섞여 있으면 여기서 걸린다.
        // 0g 짜리 줄을 세워 놓고 "주문할 수 없습니다" 라고 하는 것보다,
        // 목록을 만드는 쪽에서 아예 빼는 편이 맞다 (picksFrom / availablePicks).
        checkStock(picks);

        Ctx c = contextOf(target, tolerance_, budget);

        // ---- 격자는 모든 항목에 대해 만든다 ----
        // 고정된 항목도 나중에 유저가 손으로 조절할 수 있어야 하므로 한계를 알아야 한다.
        std::vector<Grid> grids(picks.size());
        for (std::size_t i = 0; i < picks.size(); ++i)
            grids[i] = buildGrid(*picks[i].menu, picks[i], c.K,
                                 stockCapFor(*picks[i].menu));

        // ---- 고정된 항목은 상수로 빼 둔다 ----
        // 값도 함께 빼 둔다. 고정된 항목이 이미 예산을 얼마나 먹었는지 알아야
        // 나머지에 남은 돈이 얼마인지가 나온다.
        Macros base;
        long long basePrice = 0;
        std::vector<Var> vars;
        std::vector<std::size_t> varOf(picks.size(), static_cast<std::size_t>(-1));

        for (std::size_t i = 0; i < picks.size(); ++i) {
            if (picks[i].locked) {
                base      += macrosOf(*picks[i].menu, picks[i].amount);
                basePrice += picks[i].menu->priceFor(picks[i].amount);
                continue;
            }
            Var v;
            v.menu        = picks[i].menu.get();
            v.unit        = macrosOf(*picks[i].menu, 1.0);
            v.kcalPerUnit = v.unit.calories();
            v.grid        = grids[i];
            varOf[i] = vars.size();
            vars.push_back(v);
        }

        // ---- 두 자리에서 시작해 더 나은 쪽을 쓴다 ----
        // 최소량에서 출발하면 "적게 담는" 답에, 예산을 나눠 갖고 출발하면
        // "고루 담는" 답에 가까이 떨어진다. 둘 다 풀어 보고 나은 쪽을 고른다.
        std::vector<double> x(vars.size(), 0.0), best;

        for (std::size_t i = 0; i < vars.size(); ++i) x[i] = vars[i].grid.lo;
        double bestCost = refine(c, vars, base, basePrice, x, maxSweeps_);
        best = x;

        if (!vars.empty()) {
            double share = c.K > kEps ? c.K / static_cast<double>(vars.size()) : 0.0;
            for (std::size_t i = 0; i < vars.size(); ++i) {
                double want = vars[i].kcalPerUnit > kEps ? share / vars[i].kcalPerUnit : 0.0;
                x[i] = vars[i].grid.down(vars[i].grid.clampTo(want));
            }
            double cost = refine(c, vars, base, basePrice, x, maxSweeps_);
            if (cost < bestCost - 1e-12 ||
                (cost < bestCost + 1e-12 && priceOf(vars, x) < priceOf(vars, best))) {
                bestCost = cost;
                best = x;
            }
        }

        // ---- 결과 조립 ----
        Plan plan;
        plan.target = target;
        plan.budget = budget;
        for (std::size_t i = 0; i < picks.size(); ++i) {
            PlanItem item;
            item.menu      = picks[i].menu;
            item.locked    = picks[i].locked;
            item.minAmount = grids[i].lo;
            item.maxAmount = grids[i].hi;
            item.step      = grids[i].step;
            item.nudgeStep = nudgeStepOf(grids[i]);
            item.stockCap     = grids[i].stockCap;
            item.stockLimited = grids[i].stockLimited;

            // 고정해 둔 양이라도 재고를 넘길 수는 없다.
            // "아메리카노는 무조건 2잔" 이어도 한 잔밖에 없으면 한 잔이다.
            item.amount = picks[i].locked ? picks[i].amount : best[varOf[i]];
            if (item.amount > grids[i].hi) item.amount = grids[i].hi;

            plan.items.push_back(item);
        }
        evaluatePlan(plan, tolerance_);
        return plan;
    }

    Plan MealPlanner::solveFor(const std::vector<Pick>& picks, const Day& day) const {
        return solve(picks, day.remaining(), Budget());
    }

    Plan MealPlanner::solveFor(const std::vector<Pick>& picks, const Day& day,
                               const Budget& budget) const {
        return solve(picks, day.remaining(), budget);
    }

    // ---------- 유저가 양을 바꾼다 ----------

    void MealPlanner::recompute(Plan& plan) const {
        evaluatePlan(plan, tolerance_);
    }

    double MealPlanner::setAmount(Plan& plan, std::size_t index, double amount) const {
        if (index >= plan.items.size())
            throw std::out_of_range("plan item index is out of range");

        PlanItem& it = plan.items[index];
        it.amount = snapToItem(it, amount);
        evaluatePlan(plan, tolerance_);
        return it.amount;
    }

    double MealPlanner::nudge(Plan& plan, std::size_t index, int steps) const {
        if (index >= plan.items.size())
            throw std::out_of_range("plan item index is out of range");

        const PlanItem& it = plan.items[index];
        return setAmount(plan, index,
                         it.amount + it.nudgeStep * static_cast<double>(steps));
    }

    // ---------- 유저가 메뉴 구성을 바꾼다 ----------

    namespace {

        // index 번을 뺀 나머지 항목의 탄단지와 값.
        // plan.macros / plan.price 를 그냥 쓰지 않는 것은, 유저가 items[i].amount 를
        // 직접 만지고 아직 recompute() 를 안 불렀을 수도 있기 때문이다.
        // 여기서는 항상 지금 값을 본다.
        Rest restOfPlan(const Plan& plan, std::size_t skip) {
            Rest r;
            for (std::size_t i = 0; i < plan.items.size(); ++i) {
                if (i == skip || !plan.items[i].menu) continue;
                r.macros += macrosOf(*plan.items[i].menu, plan.items[i].amount);
                r.price  += plan.items[i].menu->priceFor(plan.items[i].amount);
            }
            return r;
        }

    }

    PlanItem MealPlanner::makeItem(const Pick& pick, const Plan& plan,
                                   std::size_t skip) const {
        if (!pick.menu) throw std::invalid_argument("picked menu must not be null");

        Ctx c = contextOf(plan.target, tolerance_, plan.budget);
        Rest rest = restOfPlan(plan, skip);

        // 한계는 목표와 메뉴만 보고 정한다. 지금 담긴 양은 보지 않는다 -
        // 넣는 순서에 따라 "더 담을 수 있는 최대"가 달라지면 유저가 화면을 믿을 수 없다.
        Grid g = buildGrid(*pick.menu, pick, c.K, stockCapFor(*pick.menu));

        PlanItem item;
        item.menu      = pick.menu;
        item.locked    = pick.locked;
        item.minAmount = g.lo;
        item.maxAmount = g.hi;
        item.step      = g.step;
        item.nudgeStep = nudgeStepOf(g);
        item.stockCap     = g.stockCap;
        item.stockLimited = g.stockLimited;

        if (pick.locked) {
            item.amount = pick.amount > g.hi ? g.hi : pick.amount;
        } else {
            // 나머지가 지금 담긴 그대로일 때, 목표에 가장 가까워지는 양.
            // 나머지를 흔들지 않고 새 항목만 맞추므로 유저가 맞춰 둔 값이 살아 있다.
            // 예산을 걸어 두었다면 이미 쓴 돈(rest.price)이 함께 계산에 든다 -
            // 8,000원짜리 위에 얹는 메뉴는 처음부터 조금만 담긴다.
            Var v;
            v.menu        = pick.menu.get();
            v.unit        = macrosOf(*pick.menu, 1.0);
            v.kcalPerUnit = v.unit.calories();
            v.grid        = g;

            std::vector<double> scratch;
            double cost = 0.0;
            item.amount = bestAmountFor(c, v, rest, g.lo, scratch, cost);
        }

        item.macros = macrosOf(*item.menu, item.amount);
        item.price  = item.menu->priceFor(item.amount);
        return item;
    }

    std::size_t MealPlanner::addPick(Plan& plan, const Pick& pick) const {
        if (!pick.menu) throw std::invalid_argument("picked menu must not be null");

        // 같은 메뉴를 두 줄로 만들지 않는다. 두 줄이 되면 유저는 어느 쪽을 줄여야
        // 열량이 내려가는지 알 수 없다. 양으로 조절할 일이지 줄로 조절할 일이 아니다.
        std::size_t at = plan.indexOf(pick.menu->name());
        if (at != Plan::npos()) return at;

        if (plan.items.size() >= maxPicks()) {
            std::ostringstream o;
            o << "한 끼에 담을 수 있는 것은 " << maxPicks() << "가지까지입니다";
            throw std::length_error(o.str());
        }

        // 오늘 재료가 없으면 넣지 않는다. 자리 부족과 달리 이건 되돌릴 수 없다 -
        // 뭘 빼도 없는 재료가 생기지는 않는다.
        // UI 는 canAddMenu() 로 미리 물어보고 버튼을 잠그면 된다.
        if (!canServe(*pick.menu))
            throw std::invalid_argument("재료가 부족해 담을 수 없습니다: " + pick.menu->name());

        plan.items.push_back(makeItem(pick, plan, Plan::npos()));
        evaluatePlan(plan, tolerance_);
        return plan.items.size() - 1;
    }

    std::size_t MealPlanner::addMenu(Plan& plan, MenuPtr menu) const {
        return addPick(plan, Pick(menu));
    }

    void MealPlanner::removeItem(Plan& plan, std::size_t index) const {
        if (index >= plan.items.size())
            throw std::out_of_range("plan item index is out of range");

        plan.items.erase(plan.items.begin() + static_cast<std::ptrdiff_t>(index));
        evaluatePlan(plan, tolerance_);
    }

    bool MealPlanner::removeMenu(Plan& plan, const std::string& menuName) const {
        std::size_t at = plan.indexOf(menuName);
        if (at == Plan::npos()) return false;
        removeItem(plan, at);
        return true;
    }

    std::size_t MealPlanner::replaceItem(Plan& plan, std::size_t index,
                                         MenuPtr menu) const {
        if (index >= plan.items.size())
            throw std::out_of_range("plan item index is out of range");
        if (!menu) throw std::invalid_argument("picked menu must not be null");

        std::size_t already = plan.indexOf(menu->name());
        if (already != Plan::npos() && already != index)
            throw std::invalid_argument("그 메뉴는 이미 담겨 있습니다: " + menu->name());

        if (!canServe(*menu))
            throw std::invalid_argument("재료가 부족해 담을 수 없습니다: " + menu->name());

        plan.items[index] = makeItem(Pick(menu), plan, index);
        evaluatePlan(plan, tolerance_);
        return index;
    }

    void MealPlanner::rebalance(Plan& plan) const {
        Ctx c = contextOf(plan.target, tolerance_, plan.budget);

        // 한계는 이미 항목에 실려 있다. 다시 만들지 않는다 -
        // 넣고 빼는 동안 조절 범위가 슬금슬금 달라지면 유저가 눈치채지 못한 채
        // 아까는 담기던 양이 안 담기게 된다.
        Macros base;
        long long basePrice = 0;
        std::vector<Var> vars;
        std::vector<std::size_t> varOf(plan.items.size(), static_cast<std::size_t>(-1));

        for (std::size_t i = 0; i < plan.items.size(); ++i) {
            const PlanItem& it = plan.items[i];
            if (!it.menu) continue;
            if (it.locked) {
                base      += macrosOf(*it.menu, it.amount);
                basePrice += it.menu->priceFor(it.amount);
                continue;
            }
            Var v;
            v.menu        = it.menu.get();
            v.unit        = macrosOf(*it.menu, 1.0);
            v.kcalPerUnit = v.unit.calories();
            v.grid.lo     = it.minAmount;
            v.grid.hi     = it.maxAmount;
            v.grid.step   = it.step;
            varOf[i] = vars.size();
            vars.push_back(v);
        }

        std::vector<double> x(vars.size(), 0.0);
        for (std::size_t i = 0; i < plan.items.size(); ++i)
            if (varOf[i] != static_cast<std::size_t>(-1))
                x[varOf[i]] = vars[varOf[i]].grid.nearest(plan.items[i].amount);

        refine(c, vars, base, basePrice, x, maxSweeps_);

        for (std::size_t i = 0; i < plan.items.size(); ++i)
            if (varOf[i] != static_cast<std::size_t>(-1))
                plan.items[i].amount = x[varOf[i]];

        evaluatePlan(plan, tolerance_);
    }

    void MealPlanner::setTarget(Plan& plan, const Macros& target) const {
        plan.target = target;
        evaluatePlan(plan, tolerance_);
    }

    void MealPlanner::setBudget(Plan& plan, const Budget& budget) const {
        plan.budget = budget;
        evaluatePlan(plan, tolerance_);
    }

    // ---------- 확인이 필요한 추정치 ----------

    std::vector<PendingConfirmation> pendingConfirmations(const Day& day,
                                                          double threshold) {
        std::vector<PendingConfirmation> out;
        double goalKcal = day.goal().targetCalories();

        const std::vector<Meal>& meals = day.meals();
        for (std::size_t i = 0; i < meals.size(); ++i) {
            if (!meals[i].needsConfirmation()) continue;

            PendingConfirmation p;
            p.mealIndex   = i;
            p.foodName    = meals[i].foodName();
            p.calories    = meals[i].calories();
            p.shareOfGoal = goalKcal > kEps ? p.calories / goalKcal : 1.0;
            p.worthAsking = p.shareOfGoal >= threshold;
            out.push_back(p);
        }

        // 큰 것부터. 작은 추정치를 먼저 묻는 것은 사용자 시간을 버리는 일이다.
        for (std::size_t i = 1; i < out.size(); ++i)
            for (std::size_t k = i; k > 0 && out[k].calories > out[k-1].calories; --k)
                std::swap(out[k], out[k-1]);
        return out;
    }

    bool shouldAskBeforePlanning(const Day& day, double threshold) {
        std::vector<PendingConfirmation> p = pendingConfirmations(day, threshold);
        for (std::size_t i = 0; i < p.size(); ++i)
            if (p[i].worthAsking) return true;
        return false;
    }

    void confirmMeal(Day& day, std::size_t mealIndex, const Macros& correctedPerServing) {
        const std::vector<Meal>& meals = day.meals();
        if (mealIndex >= meals.size())
            throw std::out_of_range("mealIndex is out of range");

        // Meal 은 값 타입이라 바꿔 넣으려면 다시 만들어야 한다.
        // Day 를 통째로 다시 쌓되 해당 항목만 교체한다.
        std::vector<Meal> kept = meals;
        const Meal& old = kept[mealIndex];

        Meal fixed = old.hasExactTime()
            ? Meal(old.foodName(), old.time(), old.clock(), correctedPerServing, old.servings())
            : Meal(old.foodName(), old.time(), correctedPerServing, old.servings());
        fixed.setSource(old.source());
        fixed.setConfirmed(true);
        kept[mealIndex] = fixed;

        day.nutrition().reset();
        for (std::size_t i = 0; i < kept.size(); ++i) day.addMeal(kept[i]);
    }

    void confirmMeal(Day& day, std::size_t mealIndex) {
        const std::vector<Meal>& meals = day.meals();
        if (mealIndex >= meals.size())
            throw std::out_of_range("mealIndex is out of range");
        confirmMeal(day, mealIndex, meals[mealIndex].perServing());
    }

    void logPlan(Day& day,
                 const Plan& plan,
                 MealTime slot,
                 TimeOfDay clock) {
        for (std::size_t i = 0; i < plan.items.size(); ++i) {
            const PlanItem& it = plan.items[i];
            // 메뉴 1단위당 값을 넣고 인분 수를 amount 로 준다.
            // 그래야 Meal 쪽에서도 "무엇을 얼마나" 가 그대로 남는다.
            Meal m(it.menu->name(), slot, clock, macrosOf(*it.menu, 1.0), it.amount);
            m.setSource(MacroSource::OurMenu);   // 우리 메뉴이므로 정확한 값이다
            m.setConfirmed(true);                // 추측이 아니므로 확인할 것이 없다
            day.addMeal(m);
        }
    }

}
