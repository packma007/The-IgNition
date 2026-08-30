#include "recommend.h"
#include <algorithm>
#include <cmath>
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

    // ---------- Suggestion ----------

    bool Suggestion::has(Issue issue) const {
        for (std::size_t i = 0; i < issues.size(); ++i)
            if (issues[i] == issue) return true;
        return false;
    }

    // ---------- DinnerPlanner ----------

    void DinnerPlanner::addMenu(MenuPtr menu) {
        if (!menu) throw std::invalid_argument("menu must not be null");
        menus_.push_back(menu);
    }

    void DinnerPlanner::clearMenus() {
        menus_.clear();
    }

    void DinnerPlanner::setMaxItems(int n) {
        if (n < 1) throw std::invalid_argument("maxItems must be >= 1");
        maxItems_ = n;
    }

    void DinnerPlanner::setMaxServingsPerMenu(int n) {
        if (n < 1) throw std::invalid_argument("maxServingsPerMenu must be >= 1");
        maxServings_ = n;
    }

    namespace {

        // 이 메뉴로 시도해 볼 양들.
        // 낱개 메뉴는 1인분씩, 무게로 파는 메뉴는 남은 열량을 채우는 양을 역산해 본다.
        std::vector<double> candidateAmounts(const Menu& menu,
                                             double budgetKcal,
                                             int maxServings) {
            std::vector<double> out;
            double perUnit = kcalPerUnitOf(menu);

            if (menu.divisibility() == Divisibility::Discrete) {
                for (int i = 1; i <= maxServings; ++i) {
                    double a = menu.normalize(static_cast<double>(i));
                    if (menu.isValidAmount(a)) out.push_back(a);
                }
            } else {
                // 남은 열량을 통째로 / 반씩 / 셋으로 나눠 채우는 양을 후보로 둔다.
                // 다른 메뉴와 섞일 자리를 남겨 두기 위해서다.
                if (perUnit > kEps && budgetKcal > kEps) {
                    for (int div = 1; div <= 3; ++div) {
                        double raw = budgetKcal / static_cast<double>(div) / perUnit;
                        double a = menu.normalize(raw);
                        if (menu.isValidAmount(a)) out.push_back(a);
                    }
                }
                // 최소 판매량도 후보에 넣는다 (예산이 아주 작을 때 쓰인다)
                double least = menu.normalize(0.0);
                if (menu.isValidAmount(least)) out.push_back(least);
            }

            // 같은 양이 여러 번 나오면 하나만 남긴다
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        }

        struct Ctx {
            const std::vector<MenuPtr>* menus;
            const Tolerance* tol;
            Macros target;
            double targetKcal;
            int maxItems;
            int maxServings;
            std::vector<Suggestion>* out;
        };

        void finish(const Ctx& ctx, const std::vector<SuggestionItem>& items) {
            if (items.empty()) return;

            Suggestion s;
            s.items = items;
            for (std::size_t i = 0; i < items.size(); ++i) {
                s.macros += items[i].macros;
                s.price  += items[i].price;
            }

            s.targetCalories = ctx.targetKcal;
            s.calories       = s.macros.calories();
            s.calorieDelta   = s.calories - ctx.targetKcal;
            s.caloriePct     = ratio(s.calories, ctx.targetKcal);
            s.proteinPct     = ratio(s.macros.proteinG, ctx.target.proteinG);

            // 지방 하한은 "그 끼니 열량의 몇 %" 로 본다 (지방 1g = 9kcal)
            double fatFloorG = ctx.targetKcal > kEps
                ? ctx.targetKcal * ctx.tol->fatFloorPct / 9.0
                : 0.0;
            s.fatPct = ratio(s.macros.fatG, fatFloorG);

            // ---- 범위를 벗어났는지 ----
            if (ctx.targetKcal > kEps) {
                double band = ctx.targetKcal * ctx.tol->kcalBandPct;
                if (s.calorieDelta >  band + kEps) s.issues.push_back(Issue::CaloriesOver);
                if (s.calorieDelta < -band - kEps) s.issues.push_back(Issue::CaloriesUnder);
            } else {
                // 예산이 이미 없다. 뭘 먹든 초과다.
                if (s.calories > kEps) s.issues.push_back(Issue::CaloriesOver);
            }
            if (ctx.target.proteinG > kEps &&
                s.macros.proteinG < ctx.target.proteinG * ctx.tol->proteinFloorPct - kEps)
                s.issues.push_back(Issue::ProteinShort);
            if (fatFloorG > kEps && s.macros.fatG < fatFloorG - kEps)
                s.issues.push_back(Issue::FatShort);

            // ---- 점수 (낮을수록 좋다) ----
            // 열량은 어긋난 만큼, 단백질과 지방은 "모자란 만큼"만 벌점을 준다.
            // 넘치는 단백질과 탄수화물에는 벌점이 없다 - 그게 이 설계의 요점이다.
            double kcalScale = ctx.targetKcal > kEps ? ctx.targetKcal : 500.0;
            s.score = std::fabs(s.calorieDelta) / kcalScale
                    + 2.0 * clampLow(1.0 - s.proteinPct)
                    + 1.0 * clampLow(1.0 - s.fatPct)
                    + 0.02 * static_cast<double>(items.size());   // 단순한 조합을 살짝 선호

            ctx.out->push_back(s);
        }

        // 메뉴를 인덱스 순서대로만 골라서 같은 조합이 순서만 바꿔 중복되지 않게 한다
        void search(const Ctx& ctx,
                    std::size_t from,
                    std::vector<SuggestionItem>& picked,
                    double spentKcal) {
            if (static_cast<int>(picked.size()) >= ctx.maxItems) return;

            double leftKcal = ctx.targetKcal - spentKcal;

            for (std::size_t i = from; i < ctx.menus->size(); ++i) {
                const MenuPtr& mp = (*ctx.menus)[i];
                std::vector<double> amounts =
                    candidateAmounts(*mp, leftKcal > 0.0 ? leftKcal : 0.0, ctx.maxServings);

                for (std::size_t k = 0; k < amounts.size(); ++k) {
                    SuggestionItem item;
                    item.menu   = mp;
                    item.amount = amounts[k];
                    item.macros = macrosOf(*mp, amounts[k]);
                    item.price  = mp->priceFor(amounts[k]);

                    picked.push_back(item);
                    finish(ctx, picked);
                    search(ctx, i + 1, picked, spentKcal + item.macros.calories());
                    picked.pop_back();
                }
            }
        }

        bool byScore(const Suggestion& a, const Suggestion& b) {
            if (std::fabs(a.score - b.score) > 1e-9) return a.score < b.score;
            // 점수가 같으면 싼 것을 앞에
            return a.price < b.price;
        }

    }

    std::vector<Suggestion> DinnerPlanner::suggest(const Macros& remaining,
                                                   std::size_t maxResults) const {
        std::vector<Suggestion> all;
        if (menus_.empty() || maxResults == 0) return all;

        Ctx ctx;
        ctx.menus       = &menus_;
        ctx.tol         = &tolerance_;
        ctx.target      = remaining;
        ctx.targetKcal  = remaining.calories();
        ctx.maxItems    = maxItems_;
        ctx.maxServings = maxServings_;
        ctx.out         = &all;

        std::vector<SuggestionItem> picked;
        search(ctx, 0, picked, 0.0);

        std::sort(all.begin(), all.end(), byScore);
        if (all.size() > maxResults) all.resize(maxResults);
        return all;
    }

    // ---------- UI 편의 함수 ----------

    std::vector<Suggestion> suggestDinner(const DinnerPlanner& planner,
                                          const Day& day,
                                          std::size_t maxResults) {
        return planner.suggest(day.remaining(), maxResults);
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

    bool shouldAskBeforeSuggesting(const Day& day, double threshold) {
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

    void logSuggestion(Day& day,
                       const Suggestion& suggestion,
                       MealTime slot,
                       TimeOfDay clock) {
        for (std::size_t i = 0; i < suggestion.items.size(); ++i) {
            const SuggestionItem& it = suggestion.items[i];
            // 메뉴 1단위당 값을 넣고 인분 수를 amount 로 준다.
            // 그래야 Meal 쪽에서도 "무엇을 얼마나" 가 그대로 남는다.
            Meal m(it.menu->name(), slot, clock, macrosOf(*it.menu, 1.0), it.amount);
            m.setSource(MacroSource::OurMenu);   // 우리 메뉴이므로 정확한 값이다
            m.setConfirmed(true);                // 추측이 아니므로 확인할 것이 없다
            day.addMeal(m);
        }
    }

}
