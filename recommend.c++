#include "recommend.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace domains {

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

    // ---------- Issue ----------

    std::string describe(Issue issue) {
        switch (issue) {
            case Issue::CaloriesOver:  return "열량 초과";
            case Issue::CaloriesUnder: return "열량 부족";
            case Issue::ProteinShort:  return "단백질 부족";
            case Issue::FatShort:      return "지방 부족";
            case Issue::AmountLimited: return "양으로는 더 맞출 수 없음";
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

        Grid buildGrid(const Menu& m, const Pick& p, double budgetKcal) {
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
        };

        Ctx contextOf(const Macros& target, const Tolerance& tol) {
            Ctx c;
            c.K      = target.calories();
            c.Ptar   = target.proteinG;
            // 지방 하한은 "그 끼니 열량의 몇 %" 로 본다 (지방 1g = 9kcal)
            c.Ffloor = c.K > kEps ? c.K * tol.fatFloorPct / 9.0 : 0.0;
            c.kscale = c.K > kEps ? c.K : 500.0;
            return c;
        }

        // 낮을수록 좋다.
        // 열량은 어긋난 만큼, 단백질과 지방은 "모자란 만큼"만 벌점을 준다.
        // 넘치는 단백질과 탄수화물에는 벌점이 없다 - 그게 이 설계의 요점이다.
        double costOf(const Ctx& c, const Macros& m) {
            return std::fabs(m.calories() - c.K) / c.kscale
                 + 2.0 * clampLow(1.0 - ratio(m.proteinG, c.Ptar))
                 + 1.0 * clampLow(1.0 - ratio(m.fatG, c.Ffloor));
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

        // skipA / skipB 를 뺀 나머지의 합
        Macros restOf(const std::vector<Var>& v, const std::vector<double>& x,
                      const Macros& base, std::size_t skipA, std::size_t skipB) {
            Macros m = base;
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i == skipA || i == skipB) continue;
                m += scaled(v[i].unit, x[i]);
            }
            return m;
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
        // 단백질이 목표에 닿는 지점, 지방이 하한에 닿는 지점에서만 기울기가 바뀐다.
        // 그러니 최소값은 반드시 그 세 꼭짓점이나 양 끝에 있다. 그 다섯 곳만 보면 된다.
        void candidatesFor(const Ctx& c, const Var& v, const Macros& rest,
                           double current, std::vector<double>& out) {
            out.clear();
            const Grid& g = v.grid;

            double raw[6];
            std::size_t n = 0;
            raw[n++] = g.lo;
            raw[n++] = g.hi;
            raw[n++] = current;
            if (std::fabs(v.kcalPerUnit) > kEps)
                raw[n++] = (c.K - rest.calories()) / v.kcalPerUnit;
            if (v.unit.proteinG > kEps)
                raw[n++] = (c.Ptar - rest.proteinG) / v.unit.proteinG;
            if (v.unit.fatG > kEps)
                raw[n++] = (c.Ffloor - rest.fatG) / v.unit.fatG;

            for (std::size_t i = 0; i < n; ++i) {
                out.push_back(g.down(raw[i]));
                out.push_back(g.up(raw[i]));
            }

            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        }

        // rest 가 고정됐을 때 i 번의 최선의 양. 벌점이 같으면 싼 쪽을 고른다.
        double bestAmountFor(const Ctx& c, const Var& v, const Macros& rest,
                             double current, std::vector<double>& scratch,
                             double& bestCost) {
            candidatesFor(c, v, rest, current, scratch);

            double bestX = current;
            bestCost = costOf(c, rest + scaled(v.unit, current));
            long long bestPrice = v.menu->priceFor(current);

            for (std::size_t k = 0; k < scratch.size(); ++k) {
                double x = scratch[k];
                double cost = costOf(c, rest + scaled(v.unit, x));
                long long price = v.menu->priceFor(x);
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
                      const Macros& base, std::vector<double>& x, int maxSweeps) {
            std::vector<double> scratch, candI;
            Macros total = base;
            for (std::size_t i = 0; i < v.size(); ++i) total += scaled(v[i].unit, x[i]);
            double cur = costOf(c, total);

            for (int sweep = 0; sweep < maxSweeps; ++sweep) {
                bool improved = false;

                // ---- 한 자리씩 ----
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (v[i].grid.isPoint()) continue;
                    Macros rest = restOf(v, x, base, i, i);
                    double cost = 0.0;
                    double nx = bestAmountFor(c, v[i], rest, x[i], scratch, cost);
                    if (cost < cur - 1e-12) { x[i] = nx; cur = cost; improved = true; }
                    else if (cost < cur + 1e-12) { x[i] = nx; }   // 같은 값이면 싼 쪽으로
                }

                // ---- 두 자리씩 ----
                for (std::size_t i = 0; i + 1 < v.size(); ++i) {
                    for (std::size_t j = i + 1; j < v.size(); ++j) {
                        if (v[i].grid.isPoint() && v[j].grid.isPoint()) continue;
                        Macros rest = restOf(v, x, base, i, j);

                        // j 를 양 끝과 지금 자리에 놓아 보고 i 의 후보를 뽑는다.
                        // 그래야 "j 를 끝까지 줄였을 때 비로소 맞는 i" 가 후보에 든다.
                        double probes[3] = { v[j].grid.lo, v[j].grid.hi, x[j] };
                        candI.clear();
                        for (std::size_t t = 0; t < 3; ++t) {
                            candidatesFor(c, v[i], rest + scaled(v[j].unit, probes[t]),
                                          x[i], scratch);
                            candI.insert(candI.end(), scratch.begin(), scratch.end());
                        }
                        std::sort(candI.begin(), candI.end());
                        candI.erase(std::unique(candI.begin(), candI.end()), candI.end());

                        double bestXi = x[i], bestXj = x[j], bestCost = cur;
                        long long bestPrice = v[i].menu->priceFor(x[i])
                                            + v[j].menu->priceFor(x[j]);

                        for (std::size_t k = 0; k < candI.size(); ++k) {
                            Macros withI = rest + scaled(v[i].unit, candI[k]);
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
            Ctx c = contextOf(plan.target, tol);

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
            plan.score          = costOf(c, plan.macros);

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

            // 범위를 벗어난 채로 어떤 항목이 한계에 붙어 있다면, 양으로는 더 못 맞춘다.
            // UI 는 이걸 보고 "메뉴를 바꿔 보세요" 라고 말할 수 있다.
            // (한계에 붙은 것이 없는데도 어긋난다면 양이 아니라 고른 조합 자체의 문제다)
            if (!plan.issues.empty() && !plan.limitedMenus().empty())
                plan.issues.push_back(Issue::AmountLimited);
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

    Plan MealPlanner::solve(const std::vector<Pick>& picks, const Macros& target) const {
        if (!isValidPickCount(picks.size())) {
            std::ostringstream o;
            o << "한 끼에 고르는 메뉴는 " << minPicks() << "~" << maxPicks()
              << "가지여야 합니다 (지금 " << picks.size() << "가지)";
            throw std::invalid_argument(o.str());
        }
        for (std::size_t i = 0; i < picks.size(); ++i)
            if (!picks[i].menu) throw std::invalid_argument("picked menu must not be null");

        Ctx c = contextOf(target, tolerance_);

        // ---- 격자는 모든 항목에 대해 만든다 ----
        // 고정된 항목도 나중에 유저가 손으로 조절할 수 있어야 하므로 한계를 알아야 한다.
        std::vector<Grid> grids(picks.size());
        for (std::size_t i = 0; i < picks.size(); ++i)
            grids[i] = buildGrid(*picks[i].menu, picks[i], c.K);

        // ---- 고정된 항목은 상수로 빼 둔다 ----
        Macros base;
        std::vector<Var> vars;
        std::vector<std::size_t> varOf(picks.size(), static_cast<std::size_t>(-1));

        for (std::size_t i = 0; i < picks.size(); ++i) {
            if (picks[i].locked) {
                base += macrosOf(*picks[i].menu, picks[i].amount);
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
        double bestCost = refine(c, vars, base, x, maxSweeps_);
        best = x;

        if (!vars.empty()) {
            double share = c.K > kEps ? c.K / static_cast<double>(vars.size()) : 0.0;
            for (std::size_t i = 0; i < vars.size(); ++i) {
                double want = vars[i].kcalPerUnit > kEps ? share / vars[i].kcalPerUnit : 0.0;
                x[i] = vars[i].grid.down(vars[i].grid.clampTo(want));
            }
            double cost = refine(c, vars, base, x, maxSweeps_);
            if (cost < bestCost - 1e-12 ||
                (cost < bestCost + 1e-12 && priceOf(vars, x) < priceOf(vars, best))) {
                bestCost = cost;
                best = x;
            }
        }

        // ---- 결과 조립 ----
        Plan plan;
        plan.target = target;
        for (std::size_t i = 0; i < picks.size(); ++i) {
            PlanItem item;
            item.menu      = picks[i].menu;
            item.locked    = picks[i].locked;
            item.minAmount = grids[i].lo;
            item.maxAmount = grids[i].hi;
            item.step      = grids[i].step;
            item.nudgeStep = nudgeStepOf(grids[i]);
            item.amount    = picks[i].locked ? picks[i].amount : best[varOf[i]];
            plan.items.push_back(item);
        }
        evaluatePlan(plan, tolerance_);
        return plan;
    }

    Plan MealPlanner::solveFor(const std::vector<Pick>& picks, const Day& day) const {
        return solve(picks, day.remaining());
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
