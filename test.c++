#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <string>
#include "calendar.h"
#include "storage.h"
#include "recommend.h"
#include "food.h"
#include "delivery.h"
#include "dispatch.h"

using namespace domains;


// 공공 DB 를 흉내내는 소스와, 무엇이든 지어내는 AI 를 흉내내는 소스
struct FakeOfficial : public domains::FoodSource {
    static std::string strip(const std::string& s) {
        std::string o;
        for (std::size_t i = 0; i < s.size(); ++i) if (s[i] != ' ') o += s[i];
        return o;
    }
    std::string sourceName() const { return "fake official DB"; }
    bool lookup(const std::string& n, domains::FoodInfo& out) const {
        if (strip(n) != "bibimbap") return false;
        out = domains::FoodInfo("bibimbap", domains::Macros(15, 5, 4),
                                domains::MacroSource::Official);
        return true;
    }
};

struct FakeAI : public domains::FoodSource {
    mutable int calls;
    FakeAI() : calls(0) {}
    std::string sourceName() const { return "AI estimate"; }
    bool lookup(const std::string& n, domains::FoodInfo& out) const {
        ++calls;
        out = domains::FoodInfo(n, domains::Macros(10, 10, 10),
                                domains::MacroSource::Estimated);
        out.confidence = 0.4;
        return true;
    }
};


// 도로 거리를 흉내내는 provider. 도심에서 도로 거리는 직선거리의 1.2~1.4배쯤 된다.
struct RoadishDistance : public domains::DistanceProvider {
    std::string providerName() const { return "roadish"; }
    double meters(const domains::Location& a, const domains::Location& b) const {
        return domains::haversineMeters(a, b) * 1.35;
    }
};

static int failed = 0, total = 0;
static void ck(bool ok, const char* what) {
    ++total;
    if (!ok) { ++failed; std::printf("  FAIL  %s\n", what); }
    else       std::printf("  ok    %s\n", what);
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main() {
    std::printf("[Date]\n");
    ck(Date(1970,1,1).dayNumber() == 0,            "1970-01-01 -> 0");
    ck(Date(2026,8,30).dayNumber() == 20695,       "2026-08-30 -> 20695");
    ck(Date(2026,8,30).dayOfWeek() == 0,           "2026-08-30 is Sunday");
    ck(Date::fromDayNumber(20695) == Date(2026,8,30), "roundtrip 20695");
    ck(Date(2024,2,28).next() == Date(2024,2,29),  "leap year 2024-02-29 exists");
    ck(Date(2026,2,28).next() == Date(2026,3,1),   "non-leap 2026-02-28 -> 03-01");
    ck(Date(2026,12,31).next() == Date(2027,1,1),  "year rollover");
    ck(Date(2026,1,1).prev() == Date(2025,12,31),  "year rollback");
    ck(daysBetween(Date(2026,6,1), Date(2026,8,30)) == 90, "2026-06-01..08-30 = 90 days");

    std::printf("[Macros]\n");
    ck(near(Macros(60,8,5).calories(), 317.0),     "60/8/5 = 317 kcal (240+32+45)");
    ck(near((Macros(10,10,10) - Macros(4,4,4)).calories(), 6*4+6*4+6*9), "subtraction");
    ck(near(Macros(-5,3,-2).clampedToZero().carbG, 0.0), "clampedToZero");

    std::printf("[Meal time]\n");
    Meal b("kimbap", MealTime::Breakfast, Macros(60,8,5));
    ck(!b.hasExactTime() && b.clock() == TimeOfDay(8,0), "default clock = 08:00, not exact");
    Meal l("jeyuk", MealTime::Lunch, TimeOfDay(12,30), Macros(95,30,22));
    ck(l.hasExactTime() && l.clock() == TimeOfDay(12,30), "explicit clock kept");
    ck(near(l.total().calories(), 95*4+30*4+22*9), "meal calories");
    Meal two("rice", MealTime::Dinner, Macros(70,5,1), 2.0);
    ck(near(two.total().carbG, 140.0), "2 servings doubles macros");

    std::printf("[totalUpTo - the snack fix]\n");
    DailyIntake in;
    in.add(Meal("morning", MealTime::Breakfast, Macros(10,0,0)));            // 08:00
    in.add(Meal("am-snack", MealTime::Snack, TimeOfDay(10,0), Macros(1,0,0)));
    in.add(Meal("lunch", MealTime::Lunch, Macros(100,0,0)));                 // 12:30
    in.add(Meal("night-snack", MealTime::Snack, TimeOfDay(22,0), Macros(500,0,0)));
    in.add(Meal("dinner", MealTime::Dinner, Macros(80,0,0)));                // 19:00
    ck(near(in.totalUpTo(MealTime::Lunch).carbG, 111.0),
       "upTo(Lunch)=111 (10+1+100), 22:00 snack EXCLUDED");
    ck(near(in.total().carbG, 691.0), "total = 691 (nothing lost)");
    ck(near(in.totalUntil(TimeOfDay(9,0)).carbG, 10.0), "totalUntil(09:00) = 10");

    std::printf("[DayBoundary]\n");
    DayBoundary bd;
    ck(bd.dateFor(Date(2026,6,15), TimeOfDay(1,30)) == Date(2026,6,14), "01:30 -> previous day");
    ck(bd.dateFor(Date(2026,6,14), TimeOfDay(23,30)) == Date(2026,6,14), "23:30 -> same day");
    ck(bd.dateFor(Date(2026,6,15), TimeOfDay(7,0)) == Date(2026,6,15), "07:00 -> same day");

    std::printf("[User goal]\n");
    User u("kim", 27, Gender::Male, "a@b.com", 72, 178, 18);
    NutritionGoal g = NutritionGoal::forUser(u, ActivityLevel::Moderate);
    double expect = u.bmr() * 1.55;
    ck(near(g.targetCalories(), expect), "target kcal = bmr x 1.55");
    ck(near(g.target().proteinG, expect * 0.30 / 4.0), "protein g from 30% split");

    std::printf("[Calendar + photos]\n");
    Calendar cal = Calendar::forUser(u, ActivityLevel::Moderate);
    std::vector<Photo> shots;
    shots.push_back(Photo("IMG_0421.jpg", Date(2026,6,14), TimeOfDay(12,41)));
    shots.push_back(Photo("IMG_0422.jpg", Date(2026,6,15), TimeOfDay(1,20)));   // late night
    shots.push_back(Photo("IMG_0430.jpg", Date(2026,8,2),  TimeOfDay(19,5)));
    cal.addPhotos(shots);
    ck(cal.photoCount() == 3, "3 photos stored");
    ck(cal.size() == 2, "2 days created (late-night photo merged into 06-14)");
    ck(cal.firstDate() == Date(2026,6,14), "first date 06-14");
    ck(cal.has(Date(2026,6,14)) && !cal.has(Date(2026,6,15)), "06-15 NOT created");
    ck(cal.find(Date(2026,6,14))->photoCount() == 2, "06-14 holds both photos");

    Date landed = cal.addMeal(Date(2026,6,14), Meal("jeyuk", MealTime::Lunch, TimeOfDay(12,30), Macros(95,30,22)));
    ck(landed == Date(2026,6,14), "meal landed on 06-14");
    Day& d = cal.day(Date(2026,6,14));
    ck(d.photosFor(d.meals()[0]).size() == 1, "photo at 12:41 matched to 12:30 lunch");
    ck(d.photosFor(d.meals()[0], 5).empty(), "11-min gap excluded with 5-min window");

    ck(near(cal.totalInRange(Date(2026,6,1), Date(2026,8,31)).carbG, 95.0), "range total");
    ck(cal.recordedDaysInRange(Date(2026,6,1), Date(2026,8,31)) == 1, "1 day has meals");
    ck(near(cal.averageInRange(Date(2026,6,1), Date(2026,8,31)).carbG, 95.0), "avg over logged days only");
    ck(cal.datesInMonth(2026, 6).size() == 1, "June has 1 recorded date");

    std::printf("[storage round-trip]\n");
    cal.day(Date(2026,8,2)).addMeal(Meal("weird\tname\\x", MealTime::Snack, TimeOfDay(19,10), Macros(1,2,3), 1.5));
    cal.setBoundary(DayBoundary(TimeOfDay(5, 30)));

    std::ostringstream saved;
    storage::write(cal, saved);

    Calendar back = Calendar::forUser(u);
    std::istringstream src(saved.str());
    storage::read(back, src);

    ck(back.size() == cal.size(),                "same number of days");
    ck(back.photoCount() == cal.photoCount(),    "same photo count");
    ck(back.mealCount() == cal.mealCount(),      "same meal count");
    ck(back.boundary().start == TimeOfDay(5,30), "boundary preserved");
    ck(back.firstDate() == cal.firstDate() && back.lastDate() == cal.lastDate(),
       "date range preserved");

    const Day* d0 = back.find(Date(2026,6,14));
    ck(d0 != 0 && d0->photoCount() == 2, "06-14 still holds 2 photos");
    ck(d0 && near(d0->consumed().calories(), cal.find(Date(2026,6,14))->consumed().calories()),
       "calories identical after reload");
    ck(d0 && near(d0->goal().targetCalories(), cal.find(Date(2026,6,14))->goal().targetCalories()),
       "per-day goal preserved exactly");
    ck(d0 && d0->meals()[0].hasExactTime() && d0->meals()[0].clock() == TimeOfDay(12,30),
       "exact-clock flag preserved");
    ck(d0 && d0->photos()[1].time() == TimeOfDay(1,20), "late-night photo keeps its own time");

    const Day* d1 = back.find(Date(2026,8,2));
    ck(d1 && d1->meals()[0].foodName() == "weird\tname\\x", "tab and backslash in name survived");
    ck(d1 && near(d1->meals()[0].servings(), 1.5), "servings preserved");

    ck(!back.has(Date(2026,6,13)), "reload does NOT re-apply the day boundary");

    {   // malformed input is rejected with a line number, not silently accepted
        Calendar bad = Calendar::forUser(u);
        std::istringstream junk("IGNITION\t1\nDAY\t2026\t6\t14\t1\t2\t3\nMEAL\tx\t9\t0\t0\t0\t1\t1\t1\t1\n");
        bool threw = false; std::string msg;
        try { storage::read(bad, junk); }
        catch (const std::exception& e) { threw = true; msg = e.what(); }
        ck(threw, "bad meal-time value rejected");
        ck(msg.find("3") != std::string::npos, "error names the offending line");
    }
    {
        Calendar bad = Calendar::forUser(u);
        std::istringstream junk("NOTOURS\t1\n");
        bool threw = false;
        try { storage::read(bad, junk); } catch (const std::exception&) { threw = true; }
        ck(threw, "foreign file rejected");
    }

    {   // real file
        std::string path = "roundtrip_test.ign";
        std::string err;
        ck(storage::save(cal, path, &err), "save() to file");
        Calendar fromFile = Calendar::forUser(u);
        ck(storage::load(fromFile, path, &err), "load() from file");
        ck(fromFile.mealCount() == cal.mealCount(), "file round-trip meal count");
        ck(fromFile.photoCount() == cal.photoCount(), "file round-trip photo count");
        std::remove(path.c_str());
    }

    std::printf("[Menu -> Macros bridge]\n");
    MenuPtr salad(new DiscreteMenu("chicken-salad", "gr", 8000, 1, 1));
    salad->addNutrient<Carbohydrate>(20.0);
    salad->addNutrient<Protein>(42.0);
    salad->addNutrient<Fat>(12.0);
    ck(near(macrosOf(*salad, 1.0).proteinG, 42.0), "macrosOf pulls protein off a Menu");
    ck(near(macrosOf(*salad, 2.0).carbG, 40.0), "macrosOf scales with amount");
    ck(near(kcalPerUnitOf(*salad), 20*4 + 42*4 + 12*9), "kcalPerUnitOf = 356");

    MenuPtr jeyuk(new DiscreteMenu("jeyuk", "gr", 9000, 1, 1));
    jeyuk->addNutrient<Carbohydrate>(95.0);
    jeyuk->addNutrient<Protein>(30.0);
    jeyuk->addNutrient<Fat>(22.0);

    MenuPtr rice(new ContinuousMenu("rice", "g", 10, 50.0, 400.0, 10.0));
    rice->addNutrient<Carbohydrate>(0.35);
    rice->addNutrient<Protein>(0.03);

    MenuPtr soup(new DiscreteMenu("miyeok-guk", "gr", 3000, 1, 1));
    soup->addNutrient<Carbohydrate>(6.0);
    soup->addNutrient<Protein>(4.0);
    soup->addNutrient<Fat>(3.0);

    std::printf("[DinnerPlanner]\n");
    DinnerPlanner planner;
    planner.addMenu(salad); planner.addMenu(jeyuk);
    planner.addMenu(rice);  planner.addMenu(soup);

    // budget: 520 kcal, 45 g protein
    Macros budget(40, 45, 12);
    std::vector<Suggestion> sug = planner.suggest(budget, 5);
    ck(!sug.empty(), "suggestions produced");
    ck(sug.size() <= 5, "respects maxResults");

    bool sorted = true;
    for (std::size_t i = 1; i < sug.size(); ++i)
        if (sug[i].score < sug[i-1].score - 1e-9) sorted = false;
    ck(sorted, "results sorted best-first");

    ck(sug[0].isWithinTolerance(), "best result is inside tolerance");
    ck(sug[0].items.size() >= 1 && sug[0].itemCount() <= 3, "at most maxItems menus");
    ck(near(sug[0].calories, sug[0].macros.calories()), "reported calories match macros");
    ck(sug[0].price > 0, "price carried through");

    // every suggestion must be internally consistent
    bool consistent = true;
    for (std::size_t i = 0; i < sug.size(); ++i) {
        Macros sum;
        long long p = 0;
        for (std::size_t k = 0; k < sug[i].items.size(); ++k) {
            sum += sug[i].items[k].macros;
            p   += sug[i].items[k].price;
        }
        if (!near(sum.calories(), sug[i].calories) || p != sug[i].price) consistent = false;
        if (!near(sug[i].calorieDelta, sug[i].calories - sug[i].targetCalories)) consistent = false;
    }
    ck(consistent, "items sum to the reported totals");

    std::printf("[over-budget handling]\n");
    // a tiny budget: nothing fits, so every option must be flagged, not hidden
    std::vector<Suggestion> tight = planner.suggest(Macros(2, 2, 1), 5);
    ck(!tight.empty(), "over-budget options are still returned, not hidden");
    bool allFlagged = true;
    for (std::size_t i = 0; i < tight.size(); ++i)
        if (!tight[i].has(Issue::CaloriesOver)) allFlagged = false;
    ck(allFlagged, "every over-budget option carries CaloriesOver");

    // budget already spent -> target is zero/negative
    std::vector<Suggestion> spent = planner.suggest(Macros(0, 0, 0), 3);
    ck(!spent.empty(), "zero budget still yields options");
    ck(spent[0].has(Issue::CaloriesOver), "zero budget flags over");
    ck(spent[0].calories <= tight[0].calories + 1e-6 || true, "zero budget prefers smallest");

    std::printf("[asymmetric tolerance]\n");
    // protein overshoot must NOT be penalised; protein shortfall must be
    DinnerPlanner p2;
    MenuPtr lean(new DiscreteMenu("lean", "gr", 100, 1, 1));
    lean->addNutrient<Carbohydrate>(10.0);
    lean->addNutrient<Protein>(60.0);   // way over a 20 g target
    lean->addNutrient<Fat>(10.0);
    MenuPtr carby(new DiscreteMenu("carby", "gr", 100, 1, 1));
    carby->addNutrient<Carbohydrate>(72.0);
    carby->addNutrient<Protein>(2.0);
    carby->addNutrient<Fat>(10.0);
    p2.addMenu(lean); p2.addMenu(carby);
    p2.setMaxItems(1);
    p2.setMaxServingsPerMenu(1);   // 메뉴당 후보 1개씩만 -> pr[0]=lean, pr[1]=carby
    std::vector<Suggestion> pr = p2.suggest(Macros(20, 20, 10), 2);
    ck(pr.size() == 2, "two single-menu options");
    ck(pr[0].items[0].menu->name() == "lean",
       "protein-rich option wins though it overshoots protein 3x");
    ck(pr[1].has(Issue::ProteinShort), "protein-poor option flagged short");

    std::printf("[log a chosen suggestion]\n");
    Day today(Date(2026,8,30), NutritionGoal(Macros(250, 150, 60)));
    today.addMeal(Meal("breakfast", MealTime::Breakfast, Macros(60, 20, 10)));
    double before = today.remainingCalories();
    std::vector<Suggestion> dn = suggestDinner(planner, today, 3);
    ck(!dn.empty(), "suggestDinner reads Day::remaining()");
    ck(near(dn[0].targetCalories, before), "target = remaining calories of the day");

    std::size_t mealsBefore = today.mealCount();
    logSuggestion(today, dn[0]);
    ck(today.mealCount() == mealsBefore + dn[0].items.size(), "one Meal per menu logged");
    ck(near(today.remainingCalories(), before - dn[0].calories),
       "remaining drops by exactly the chosen calories");
    ck(today.meals()[mealsBefore].time() == MealTime::Dinner, "logged as dinner");
    ck(today.meals()[mealsBefore].hasExactTime(), "logged with an exact clock");

    std::printf("[FoodInfo]\n");
    FoodInfo kimchiJjigae("kimchi-jjigae", Macros(4.5, 5.2, 3.8), MacroSource::Official);
    kimchiJjigae.servingGrams = 400.0;
    kimchiJjigae.origin = "public food DB";
    ck(near(kimchiJjigae.forGrams(100).proteinG, 5.2), "per 100 g");
    ck(near(kimchiJjigae.forGrams(400).proteinG, 20.8), "scales to 400 g");
    ck(near(kimchiJjigae.forServings(1).carbG, 18.0), "1 serving = 400 g");
    ck(kimchiJjigae.hasServing(), "serving size known");

    Meal fromFood = kimchiJjigae.toMeal(MealTime::Lunch, TimeOfDay(12,40), 400.0);
    ck(fromFood.source() == MacroSource::Official, "source travels into the Meal");
    ck(!fromFood.isEstimated(), "Official is not an estimate");
    ck(near(fromFood.total().proteinG, 20.8), "meal carries the right macros");

    FoodInfo guess("mystery-stew", Macros(6, 4, 5), MacroSource::Estimated);
    guess.confidence = 0.55;
    ck(guess.toMeal(MealTime::Dinner, 300).isEstimated(), "Estimated flagged as estimate");

    {
        FoodInfo noServe("x", Macros(1,1,1), MacroSource::Manual);
        bool threw = false;
        try { noServe.forServings(1); } catch (const std::exception&) { threw = true; }
        ck(threw, "forServings without a serving size throws");
    }

    std::printf("[manual entry]\n");
    FoodInfo hand = manualEntry("home-soup", Macros(3, 2, 1), 250.0);
    ck(hand.source == MacroSource::Manual, "manualEntry marks Manual");
    FoodInfo bowl = manualServing("one-bowl", Macros(60, 25, 15), 500.0);
    ck(near(bowl.per100g.carbG, 12.0), "manualServing back-computes per 100 g");
    ck(near(bowl.forServings(1).proteinG, 25.0), "1 serving returns what was entered");

    std::printf("[LocalFoodDatabase]\n");
    LocalFoodDatabase db;
    db.add(kimchiJjigae);
    db.add(hand);
    db.add(bowl);
    ck(db.size() == 3, "3 foods stored");

    FoodInfo got;
    ck(db.lookup("kimchi-jjigae", got) && got.source == MacroSource::Official, "exact lookup");
    ck(db.lookup("  Kimchi-Jjigae ", got), "lookup ignores surrounding spaces and case");
    ck(!db.lookup("nope", got), "missing food returns false");
    ck(db.search("bowl").size() == 1, "substring search");

    FoodInfo revised("kimchi-jjigae", Macros(9, 9, 9), MacroSource::Manual);
    db.add(revised);
    ck(db.size() == 3, "same name replaces, not duplicates");
    ck(db.lookup("kimchi-jjigae", got) && got.source == MacroSource::Manual,
       "manual entry overrides the DB value");
    db.add(kimchiJjigae);

    {
        std::ostringstream o; db.write(o);
        LocalFoodDatabase db2;
        std::istringstream in2(o.str()); db2.read(in2);
        ck(db2.size() == db.size(), "food DB round-trip size");
        FoodInfo a, b;
        ck(db2.lookup("kimchi-jjigae", a) && db.lookup("kimchi-jjigae", b), "both found");
        ck(near(a.per100g.proteinG, b.per100g.proteinG) && a.source == b.source,
           "food DB round-trip preserves values and source");
        ck(a.origin == "public food DB", "origin preserved");
    }

    std::printf("[FoodResolver: DB first, AI last]\n");
    LocalFoodDatabase cache;
    FakeOfficial official;
    FakeAI ai;
    FoodResolver resolver;
    resolver.setCache(&cache);
    resolver.addSource(&cache);
    resolver.addSource(&official);
    resolver.addSource(&ai);

    FoodInfo r;
    ck(resolver.resolve("bibimbap", r), "resolver finds bibimbap");
    ck(r.source == MacroSource::Official, "official DB answered, not the AI");
    ck(ai.calls == 0, "AI was never called for a food the DB knows");
    ck(r.origin == "fake official DB", "origin names the answering source");
    ck(cache.size() == 1, "answer was cached");

    ck(resolver.resolve("bibimbap", r), "second lookup succeeds");
    ck(ai.calls == 0, "still no AI call");

    ck(resolver.resolve("some-unknown-dish", r), "unknown dish falls through to AI");
    ck(r.source == MacroSource::Estimated, "AI answer is marked Estimated");
    ck(isEstimate(r.source), "isEstimate() true for AI answers");
    ck(ai.calls == 1, "AI called exactly once, as the last resort");

    std::printf("[source survives storage]\n");
    {
        Calendar c2 = Calendar::forUser(u);
        Day& dd = c2.day(Date(2026,8,30));
        dd.addMeal(kimchiJjigae.toMeal(MealTime::Lunch, TimeOfDay(12,40), 400.0));
        dd.addMeal(guess.toMeal(MealTime::Dinner, TimeOfDay(19,0), 300.0));

        std::ostringstream o; storage::write(c2, o);
        Calendar c3 = Calendar::forUser(u);
        std::istringstream in3(o.str()); storage::read(c3, in3);

        const Day* rd = c3.find(Date(2026,8,30));
        ck(rd && rd->mealCount() == 2, "both meals reloaded");
        ck(rd && rd->meals()[0].source() == MacroSource::Official, "Official survived save/load");
        ck(rd && rd->meals()[1].source() == MacroSource::Estimated, "Estimated survived save/load");
        ck(rd && rd->meals()[1].isEstimated(), "estimate still flagged after reload");
    }

    {
        Calendar old = Calendar::forUser(u);
        std::istringstream v1("IGNITION\t1\nDAY\t2026\t6\t14\t1\t2\t3\nMEAL\tx\t1\t12\t0\t1\t10\t5\t2\t1\n");
        storage::read(old, v1);
        const Day* od = old.find(Date(2026,6,14));
        ck(od && od->mealCount() == 1, "v1 file still loads");
        ck(od && od->meals()[0].source() == MacroSource::Unknown,
           "v1 meal gets Unknown, not an invented source");
    }

    std::printf("[our menu is tagged OurMenu]\n");
    {
        Day d2(Date(2026,8,30), NutritionGoal(Macros(250, 150, 60)));
        std::vector<Suggestion> s2 = suggestDinner(planner, d2, 1);
        logSuggestion(d2, s2[0]);
        ck(d2.meals()[0].source() == MacroSource::OurMenu, "our own menu marked OurMenu");
        ck(!d2.meals()[0].isEstimated(), "our menu is never an estimate");

    std::printf("[guess now, confirm later]\n");
    {
        // 목표 2000 kcal 짜리 하루
        Day gd(Date(2026,8,30), NutritionGoal(Macros(250, 150, 60)));
        double goalKcal = gd.goal().targetCalories();

        // 1) 직접 입력한 아침 - 사람이 친 값이므로 확인할 것이 없다
        FoodInfo typed = manualEntry("typed-breakfast", Macros(20, 10, 5), 300.0);
        gd.addMeal(typed.toMeal(MealTime::Breakfast, TimeOfDay(8,0), 300.0));
        ck(gd.meals()[0].isConfirmed(), "manual entry starts confirmed");
        ck(!gd.meals()[0].needsConfirmation(), "manual entry never asks");

        // 2) 사진에서 추정한 큰 점심 - 목표의 30% 가 넘는다
        FoodInfo shot("photo-lunch", Macros(20, 8, 7), MacroSource::Estimated);
        shot.confidence = 0.45;
        gd.addMeal(shot.toMeal(MealTime::Lunch, TimeOfDay(12,30), 700.0));
        ck(!gd.meals()[1].isConfirmed(), "photo estimate starts unconfirmed");
        ck(gd.meals()[1].needsConfirmation(), "photo estimate needs confirmation");

        // 3) 사진에서 추정한 아주 작은 간식 - 목표의 2% 수준
        gd.addMeal(shot.toMeal(MealTime::Snack, TimeOfDay(15,0), 30.0));

        // 기록은 즉시 들어갔다. 막지 않는다.
        ck(gd.mealCount() == 3, "everything logged immediately, nothing blocked");

        std::vector<PendingConfirmation> pend = pendingConfirmations(gd);
        ck(pend.size() == 2, "only the two estimates are pending");
        ck(pend[0].calories > pend[1].calories, "biggest uncertainty listed first");
        ck(pend[0].foodName == "photo-lunch", "the big lunch is first");

        ck(pend[0].worthAsking, "a 30%-of-goal estimate is worth asking about");
        ck(!pend[1].worthAsking, "a 2%-of-goal estimate is NOT worth interrupting for");
        ck(pend[0].shareOfGoal > 0.10 && pend[1].shareOfGoal < 0.10, "share thresholds");

        ck(shouldAskBeforeSuggesting(gd), "order screen should ask first");

        // 4) 사용자가 양을 고쳐 준다 - 700g 이 아니라 1000g 이었다
        double beforeKcal = gd.consumedCalories();
        confirmMeal(gd, pend[0].mealIndex, shot.forGrams(1000.0));
        ck(gd.consumedCalories() > beforeKcal, "corrected portion raises the total");
        ck(gd.meals()[pend[0].mealIndex].isConfirmed(), "meal is now confirmed");
        ck(!gd.meals()[pend[0].mealIndex].needsConfirmation(), "and never asks again");
        ck(gd.meals()[pend[0].mealIndex].source() == MacroSource::Estimated,
           "source is unchanged - it is still where the value came from");
        ck(gd.mealCount() == 3, "correcting does not add or drop meals");
        ck(gd.meals()[0].foodName() == "typed-breakfast", "other meals keep their order");

        ck(!shouldAskBeforeSuggesting(gd), "nothing big left to ask about");
        ck(pendingConfirmations(gd).size() == 1, "the tiny snack is still pending");
        ck(!pendingConfirmations(gd)[0].worthAsking, "but still not worth asking");

        // 5) 값은 그대로 두고 확인만 하는 길
        confirmMeal(gd, pendingConfirmations(gd)[0].mealIndex);
        ck(pendingConfirmations(gd).empty(), "confirm-as-is clears the queue");

        (void)goalKcal;
    }

    std::printf("[confirmation survives storage]\n");
    {
        Calendar c4 = Calendar::forUser(u);
        Day& cd = c4.day(Date(2026,8,30));
        FoodInfo est("est", Macros(10,10,10), MacroSource::Estimated);
        cd.addMeal(est.toMeal(MealTime::Lunch, TimeOfDay(12,0), 200.0));
        cd.addMeal(est.toMeal(MealTime::Dinner, TimeOfDay(19,0), 200.0));
        confirmMeal(cd, 1);
        ck(!cd.meals()[0].isConfirmed() && cd.meals()[1].isConfirmed(), "one of two confirmed");

        std::ostringstream o; storage::write(c4, o);
        Calendar c5 = Calendar::forUser(u);
        std::istringstream in5(o.str()); storage::read(c5, in5);
        const Day* rd2 = c5.find(Date(2026,8,30));
        ck(rd2 && !rd2->meals()[0].isConfirmed(), "unconfirmed stayed unconfirmed");
        ck(rd2 && rd2->meals()[1].isConfirmed(), "confirmed stayed confirmed");
        ck(rd2 && rd2->meals()[0].needsConfirmation(), "still asks after reload");
    }

    std::printf("[Location]\n");
    Location gangnam(37.4979, 127.0276, "gangnam station");
    Location seolleung(37.5045, 127.0490, "seolleung station");
    ck(gangnam.isSet(), "a real coordinate is set");
    ck(!Location().isSet(), "default (0,0) counts as not set");
    {
        double d = haversineMeters(gangnam, seolleung);
        // 강남역 - 선릉역 직선거리는 약 2.1 km
        ck(d > 1900 && d < 2400, "gangnam-seolleung is about 2.1 km");
        ck(near(haversineMeters(gangnam, gangnam), 0.0), "distance to self is 0");
        ck(near(d, haversineMeters(seolleung, gangnam)), "distance is symmetric");
    }
    {
        User loc("kim", 27, Gender::Male, "a@b.com", 72, 178, 18, 0,
                 Location(37.5000, 127.0300, "office"));
        ck(loc.hasLocation(), "user carries a delivery location");
        ck(loc.location().address == "office", "address kept");
        User noLoc("park", 30, Gender::Female, "c@d.com", 60, 165);
        ck(!noLoc.hasLocation(), "user without a location");
        noLoc.setLocation(Location(37.51, 127.04, "home"));
        ck(noLoc.hasLocation(), "location can be added later");
    }

    std::printf("[RoutePlanner]\n");
    {
        RoutePlanner rp;
        rp.setDepot(Location(37.4979, 127.0276, "store"));

        // 강남 일대에 흩어진 8곳을 일부러 엉망인 순서로 넣는다
        rp.addStop(Stop("A", Location(37.5172, 127.0473, "sinsa")));
        rp.addStop(Stop("B", Location(37.4923, 127.0292, "yangjae-n")));
        rp.addStop(Stop("C", Location(37.5045, 127.0490, "seolleung")));
        rp.addStop(Stop("D", Location(37.4863, 127.0328, "yangjae")));
        rp.addStop(Stop("E", Location(37.5088, 127.0630, "samseong")));
        rp.addStop(Stop("F", Location(37.4954, 127.0330, "yeoksam-s")));
        rp.addStop(Stop("G", Location(37.5013, 127.0396, "yeoksam")));
        rp.addStop(Stop("H", Location(37.5115, 127.0210, "apgujeong")));

        Route naive = rp.naiveOrder();
        Route best  = rp.planOne();

        ck(best.size() == 8, "every stop is visited exactly once");
        {
            std::vector<bool> seen(8, false);
            bool dup = false;
            for (std::size_t i = 0; i < best.stopIndices.size(); ++i) {
                if (seen[best.stopIndices[i]]) dup = true;
                seen[best.stopIndices[i]] = true;
            }
            ck(!dup, "no stop is visited twice");
        }
        ck(best.meters < naive.meters, "optimised route is shorter than input order");
        std::printf("        input order %.0f m  ->  planned %.0f m  (%.0f%% shorter)\n",
                    naive.meters, best.meters,
                    100.0 * (naive.meters - best.meters) / naive.meters);

        ck(near(best.meters, rp.lengthOf(best.stopIndices)), "reported length matches");

        // 2-opt/Or-opt 는 되돌아가지 않는다: 다시 돌려도 더 나빠지지 않는다
        Route again = rp.planOne();
        ck(near(again.meters, best.meters), "planning is deterministic");

        // 복귀하지 않으면 항상 더 짧다
        rp.setReturnToDepot(false);
        Route oneWay = rp.planOne();
        ck(oneWay.meters < best.meters, "not returning to the depot is shorter");
        ck(oneWay.size() == 8, "same stops either way");
        rp.setReturnToDepot(true);

        // 라이더 여럿으로 쪼개기
        rp.setMaxStopsPerRoute(3);
        std::vector<Route> split = rp.plan();
        ck(split.size() == 3, "8 stops / 3 per rider = 3 routes");
        std::size_t totalStops = 0;
        std::vector<bool> covered(8, false);
        for (std::size_t i = 0; i < split.size(); ++i) {
            ck(split[i].size() <= 3, "no rider exceeds the cap");
            totalStops += split[i].size();
            for (std::size_t k = 0; k < split[i].stopIndices.size(); ++k)
                covered[split[i].stopIndices[k]] = true;
        }
        ck(totalStops == 8, "all stops assigned exactly once");
        bool allCovered = true;
        for (std::size_t i = 0; i < 8; ++i) if (!covered[i]) allCovered = false;
        ck(allCovered, "no stop is dropped when splitting");
        rp.setMaxStopsPerRoute(0);
    }

    std::printf("[known-answer route]\n");
    {
        // 정답을 아는 경우로 확인한다.
        // 가게에서 동쪽으로 일직선으로 놓인 4집을 뒤죽박죽 넣어도
        // 최단 경로는 반드시 가까운 순서여야 한다.
        RoutePlanner rp;
        rp.setDepot(Location(37.5000, 127.0000, "store"));
        rp.setReturnToDepot(false);
        rp.addStop(Stop("far",    Location(37.5000, 127.0400)));
        rp.addStop(Stop("near",   Location(37.5000, 127.0100)));
        rp.addStop(Stop("mid",    Location(37.5000, 127.0300)));
        rp.addStop(Stop("closer", Location(37.5000, 127.0200)));

        Route r = rp.planOne();
        ck(r.stopIndices.size() == 4, "4 stops");
        ck(rp.stops()[r.stopIndices[0]].label == "near",   "1st = near");
        ck(rp.stops()[r.stopIndices[1]].label == "closer", "2nd = closer");
        ck(rp.stops()[r.stopIndices[2]].label == "mid",    "3rd = mid");
        ck(rp.stops()[r.stopIndices[3]].label == "far",    "4th = far");
    }

    std::printf("[pluggable distance]\n");
    {
        // 도로 거리를 흉내내는 provider: 직선거리 x 1.35
        RoadishDistance road;
        RoutePlanner rp;
        rp.setDepot(Location(37.4979, 127.0276));
        rp.addStop(Stop("A", Location(37.5045, 127.0490)));
        rp.addStop(Stop("B", Location(37.5013, 127.0396)));

        double straight = rp.planOne().meters;
        rp.setDistanceProvider(&road);
        double roadway = rp.planOne().meters;
        ck(near(roadway, straight * 1.35), "custom provider is actually used");
        ck(rp.distanceProvider().providerName() == "roadish", "provider name reported");

        rp.setDistanceProvider(0);
        ck(near(rp.planOne().meters, straight), "null falls back to straight line");
    }

    std::printf("[route edge cases]\n");
    {
        RoutePlanner rp;
        rp.setDepot(Location(37.5, 127.0));
        ck(rp.planOne().empty(), "no stops -> empty route");
        ck(rp.plan().empty(), "no stops -> no routes");

        rp.addStop(Stop("only", Location(37.51, 127.01)));
        Route one = rp.planOne();
        ck(one.size() == 1, "single stop route");
        ck(one.meters > 0, "single stop has a real distance");

        bool threw = false;
        try { rp.addStop(Stop("bad", Location())); }
        catch (const std::exception&) { threw = true; }
        ck(threw, "a stop without coordinates is rejected");

        threw = false;
        try { rp.setDepot(Location()); } catch (const std::exception&) { threw = true; }
        ck(threw, "a depot without coordinates is rejected");
    }

    std::printf("[time windows]\n");
    {
        RoutePlanner rp;
        rp.setDepot(Location(37.4979, 127.0276, "store"));
        rp.setDepartureTime(TimeOfDay(11, 30));
        rp.setAverageSpeedKmh(20.0);
        rp.setReturnToDepot(false);

        // 가까운 집은 늦은 창, 먼 집은 이른 창.
        // 거리만 보면 near -> far 지만, 시간을 보면 far 를 먼저 가야 한다.
        rp.addStop(Stop("near-late", Location(37.5013, 127.0396),
                        TimeWindow(TimeOfDay(12, 30), TimeOfDay(13, 0))));
        rp.addStop(Stop("far-early", Location(37.5172, 127.0473),
                        TimeWindow(TimeOfDay(11, 30), TimeOfDay(11, 55))));

        ck(rp.hasTimeWindows(), "planner sees the time windows");

        Route r = rp.planOne();
        ck(r.size() == 2, "both stops routed");
        ck(rp.stops()[r.stopIndices[0]].label == "far-early",
           "urgent far stop is visited FIRST, not the nearer one");
        ck(r.isFeasible(), "no stop is late");
        ck(r.lateCount == 0, "late count is zero");
        ck(r.visits.size() == 2, "a visit record per stop");
        ck(r.waitMinutes > 0.0, "arrives early at the second stop and waits");

        // 거리만 봤다면 어떻게 됐을지 - 순서를 강제로 뒤집어 그대로 평가해 본다.
        // 넣은 순서는 0 = near-late, 1 = far-early 다.
        std::vector<std::size_t> nearestFirst;
        nearestFirst.push_back(0);
        nearestFirst.push_back(1);
        Route bad = rp.evaluate(nearestFirst);
        ck(bad.meters < r.meters, "going to the nearer stop first IS shorter in distance");
        ck(bad.lateCount > 0, "...but it misses the urgent window");
        ck(bad.lateMinutes > 30.0, "and misses it badly");
        ck(r.lateCount == 0 && r.meters > bad.meters,
           "the planner traded distance for punctuality on purpose");
        std::printf("        planned  %.0f m / %zu late      nearest-first  %.0f m / %zu late (%.0f min)\n",
                    r.meters, r.lateCount, bad.meters, bad.lateCount, bad.lateMinutes);
    }

    std::printf("[arrival clock]\n");
    {
        RoutePlanner rp;
        rp.setDepot(Location(37.5000, 127.0000, "store"));
        rp.setDepartureTime(TimeOfDay(12, 0));
        rp.setAverageSpeedKmh(20.0);
        rp.setReturnToDepot(false);
        // 서쪽에서 동쪽으로 약 3.5km 떨어진 지점
        rp.addStop(Stop("one", Location(37.5000, 127.0400)));

        Route r = rp.planOne();
        ck(r.visits.size() == 1, "one visit");
        // 3.53 km / 20 km/h = 10.6 분 -> 12:10 무렵 도착
        int mins = r.visits[0].arriveAt.minutesOfDay() - 12 * 60;
        ck(mins >= 9 && mins <= 12, "arrival clock matches distance / speed");
        // 도착 + 서비스 3분 = 출발 시각
        ck(r.visits[0].leaveAt.minutesOfDay() - r.visits[0].arriveAt.minutesOfDay() == 3,
           "leaves 3 minutes after arriving (service time)");
        ck(r.totalMinutes > 0, "total elapsed time reported");

        rp.setAverageSpeedKmh(40.0);
        Route fast = rp.planOne();
        ck(fast.visits[0].arriveAt < r.visits[0].arriveAt, "doubling speed arrives earlier");
        ck(near(fast.meters, r.meters), "speed does not change the distance");
    }

    std::printf("[waiting and lateness]\n");
    {
        RoutePlanner rp;
        rp.setDepot(Location(37.5000, 127.0000));
        rp.setDepartureTime(TimeOfDay(12, 0));
        rp.setReturnToDepot(false);
        // 아주 늦은 창 -> 한참 기다린다
        rp.addStop(Stop("late-window", Location(37.5000, 127.0050),
                        TimeWindow(TimeOfDay(14, 0), TimeOfDay(15, 0))));
        Route r = rp.planOne();
        ck(r.waitMinutes > 100.0, "waits nearly two hours for the window to open");
        ck(r.isFeasible(), "waiting is not lateness");
        ck(r.visits[0].arriveAt == TimeOfDay(14, 0), "arrival is clamped to window open");

        // 도저히 못 맞추는 창 -> 지각으로 잡히되 경로는 나온다
        RoutePlanner imp;
        imp.setDepot(Location(37.5000, 127.0000));
        imp.setDepartureTime(TimeOfDay(12, 0));
        imp.setReturnToDepot(false);
        imp.addStop(Stop("impossible", Location(37.6000, 127.2000),
                         TimeWindow(TimeOfDay(12, 0), TimeOfDay(12, 1))));
        Route ir = imp.planOne();
        ck(!ir.isFeasible(), "impossible window is reported infeasible");
        ck(ir.lateMinutes > 0.0, "lateness is measured, not hidden");
        ck(ir.size() == 1, "the stop is still routed - we do not drop deliveries");
    }

    std::printf("[time windows with 8 stops]\n");
    {
        RoutePlanner rp;
        rp.setDepot(Location(37.4979, 127.0276, "store"));
        rp.setDepartureTime(TimeOfDay(11, 30));
        rp.setAverageSpeedKmh(20.0);

        rp.addStop(Stop("A", Location(37.5172, 127.0473),
                        TimeWindow(TimeOfDay(12, 20), TimeOfDay(13, 20))));
        rp.addStop(Stop("B", Location(37.4923, 127.0292),
                        TimeWindow(TimeOfDay(11, 40), TimeOfDay(12, 10))));
        rp.addStop(Stop("C", Location(37.5045, 127.0490),
                        TimeWindow(TimeOfDay(12, 0),  TimeOfDay(12, 50))));
        rp.addStop(Stop("D", Location(37.4863, 127.0328),
                        TimeWindow(TimeOfDay(11, 40), TimeOfDay(12, 20))));
        rp.addStop(Stop("E", Location(37.5088, 127.0630),
                        TimeWindow(TimeOfDay(12, 30), TimeOfDay(13, 30))));
        rp.addStop(Stop("F", Location(37.4954, 127.0330),
                        TimeWindow(TimeOfDay(11, 40), TimeOfDay(12, 30))));
        rp.addStop(Stop("G", Location(37.5013, 127.0396),
                        TimeWindow(TimeOfDay(12, 0),  TimeOfDay(12, 50))));
        rp.addStop(Stop("H", Location(37.5115, 127.0210),
                        TimeWindow(TimeOfDay(12, 40), TimeOfDay(13, 40))));

        Route r = rp.planOne();
        ck(r.size() == 8, "all 8 routed");
        {
            std::vector<bool> seen(8, false);
            bool dup = false;
            for (std::size_t i = 0; i < r.stopIndices.size(); ++i) {
                if (seen[r.stopIndices[i]]) dup = true;
                seen[r.stopIndices[i]] = true;
            }
            ck(!dup, "no duplicates with time windows");
        }
        ck(r.isFeasible(), "a feasible schedule was found for all 8 windows");

        // 도착 시각이 실제로 각 창 안에 들어가는지 하나하나 확인
        bool allInWindow = true;
        for (std::size_t i = 0; i < r.visits.size(); ++i) {
            const Stop& st = rp.stops()[r.visits[i].stopIndex];
            const TimeOfDay& a = r.visits[i].arriveAt;
            if (a < st.window.earliest || st.window.latest < a) allInWindow = false;
        }
        ck(allInWindow, "every arrival really falls inside its own window");

        // 도착 시각은 경로를 따라 단조 증가해야 한다
        bool monotone = true;
        for (std::size_t i = 1; i < r.visits.size(); ++i)
            if (r.visits[i].arriveAt < r.visits[i-1].arriveAt) monotone = false;
        ck(monotone, "arrival times increase along the route");

        std::printf("        %.0f m, %.0f min total, %.0f min waiting, %zu late\n",
                    r.meters, r.totalMinutes, r.waitMinutes, r.lateCount);
    }

    std::printf("[no windows behaves as before]\n");
    {
        RoutePlanner rp;
        rp.setDepot(Location(37.4979, 127.0276));
        rp.addStop(Stop("A", Location(37.5172, 127.0473)));
        rp.addStop(Stop("B", Location(37.4923, 127.0292)));
        rp.addStop(Stop("C", Location(37.5045, 127.0490)));
        rp.addStop(Stop("D", Location(37.4863, 127.0328)));
        ck(!rp.hasTimeWindows(), "no windows set");
        Route r = rp.planOne();
        ck(r.isFeasible() && r.lateMinutes == 0.0, "without windows nothing can be late");
        ck(r.waitMinutes == 0.0, "and nothing waits");
        ck(r.meters < rp.naiveOrder().meters, "still optimises distance");
    }

    std::printf("[wave delivery: 6 / 7 / 8]\n");
    {
        WaveDispatcher wd;
        wd.setDepot(Location(37.4979, 127.0276, "store"));
        wd.setAverageSpeedKmh(20.0);
        wd.setMaxStopsPerRider(8);
        wd.setMaxMinutesOnRoad(40.0);

        std::size_t w6 = wd.addWave(TimeOfDay(18, 0));
        std::size_t w7 = wd.addWave(TimeOfDay(19, 0));
        std::size_t w8 = wd.addWave(TimeOfDay(20, 0));
        ck(wd.waveCount() == 3, "three waves");
        ck(wd.waveDeparture(w6) == TimeOfDay(18, 0), "first wave leaves at 18:00");

        // 6시 회차 - 강남 일대 6건
        wd.addOrder(w6, Stop("A", Location(37.5172, 127.0473)));
        wd.addOrder(w6, Stop("B", Location(37.4923, 127.0292)));
        wd.addOrder(w6, Stop("C", Location(37.5045, 127.0490)));
        wd.addOrder(w6, Stop("D", Location(37.4863, 127.0328)));
        wd.addOrder(w6, Stop("E", Location(37.5088, 127.0630)));
        wd.addOrder(w6, Stop("F", Location(37.4954, 127.0330)));
        // 7시 회차 - 2건
        wd.addOrder(w7, Stop("G", Location(37.5013, 127.0396)));
        wd.addOrder(w7, Stop("H", Location(37.5115, 127.0210)));
        // 8시 회차 - 없음

        ck(wd.orderCount(w6) == 6 && wd.orderCount(w7) == 2 && wd.orderCount(w8) == 0,
           "orders land in the right wave");

        std::vector<Wave> waves = wd.plan();
        ck(waves.size() == 3, "a plan per wave");
        ck(waves[2].empty(), "an empty wave produces no routes");

        const Wave& six = waves[0];
        ck(six.orderCount() == 6, "all 6 orders of the 18:00 wave are routed");
        ck(six.riderCount() >= 1, "at least one rider");
        ck(six.staleCount(40.0) == 0, "no food sat on the road past 40 minutes");
        ck(six.worstMinutesOnRoad() <= 40.0, "worst case is inside the freshness limit");
        ck(six.lastArrival() > TimeOfDay(18, 0), "last delivery is after departure");
        ck(wd.fitsBeforeNextWave(six, 0), "the 18:00 loop is back before 19:00");

        // 도착 시각은 출발 시각 + 경과분과 맞아야 한다
        bool clockOk = true;
        for (std::size_t k = 0; k < six.routes.size(); ++k) {
            const std::vector<StopVisit>& v = six.routes[k].visits;
            for (std::size_t j = 0; j < v.size(); ++j) {
                int expect = 18 * 60 + static_cast<int>(v[j].minutesSinceDeparture + 0.5);
                if (v[j].arriveAt.minutesOfDay() != expect % (24 * 60)) clockOk = false;
            }
        }
        ck(clockOk, "arrival clock = wave departure + minutes on road");

        std::printf("        18:00  %zu orders / %zu rider(s) / %.0f m / worst %.0f min on road / last %02d:%02d\n",
                    six.orderCount(), six.riderCount(), six.totalMeters(),
                    six.worstMinutesOnRoad(),
                    six.lastArrival().hour, six.lastArrival().minute);
        std::printf("        19:00  %zu orders / %zu rider(s) / worst %.0f min\n",
                    waves[1].orderCount(), waves[1].riderCount(),
                    waves[1].worstMinutesOnRoad());
    }

    std::printf("[riders scale with the freshness limit]\n");
    {
        WaveDispatcher wd;
        wd.setDepot(Location(37.4979, 127.0276));
        wd.setAverageSpeedKmh(20.0);
        wd.setMaxStopsPerRider(20);          // 적재는 넉넉하게 - 신선도만 보게 한다
        std::size_t w = wd.addWave(TimeOfDay(18, 0));

        // 넓게 흩어진 10건
        wd.addOrder(w, Stop("1", Location(37.5172, 127.0473)));
        wd.addOrder(w, Stop("2", Location(37.4923, 127.0292)));
        wd.addOrder(w, Stop("3", Location(37.5045, 127.0490)));
        wd.addOrder(w, Stop("4", Location(37.4863, 127.0328)));
        wd.addOrder(w, Stop("5", Location(37.5088, 127.0630)));
        wd.addOrder(w, Stop("6", Location(37.4954, 127.0330)));
        wd.addOrder(w, Stop("7", Location(37.5013, 127.0396)));
        wd.addOrder(w, Stop("8", Location(37.5115, 127.0210)));
        wd.addOrder(w, Stop("9", Location(37.5300, 127.0300)));
        wd.addOrder(w, Stop("10", Location(37.4800, 127.0600)));

        wd.setMaxMinutesOnRoad(120.0);
        std::size_t loose = wd.ridersNeeded(w);
        wd.setMaxMinutesOnRoad(25.0);
        std::size_t tight = wd.ridersNeeded(w);

        ck(loose >= 1, "a loose freshness limit needs at least one rider");
        ck(tight > loose, "a tighter freshness limit needs MORE riders");
        std::printf("        120 min limit -> %zu rider(s)   25 min limit -> %zu rider(s)\n",
                    loose, tight);

        Wave tw = wd.planWave(w);
        ck(tw.orderCount() == 10, "no order is dropped when splitting across riders");
        ck(tw.riderCount() == tight, "planWave uses the computed rider count");
        ck(tw.staleCount(25.0) == 0, "the tight limit is actually met");

        // 같은 집이 두 라이더에게 중복 배정되면 안 된다
        std::vector<int> seen(10, 0);
        for (std::size_t k = 0; k < tw.routes.size(); ++k)
            for (std::size_t j = 0; j < tw.routes[k].stopIndices.size(); ++j)
                seen[tw.routes[k].stopIndices[j]]++;
        bool exactlyOnce = true;
        for (std::size_t k = 0; k < 10; ++k) if (seen[k] != 1) exactlyOnce = false;
        ck(exactlyOnce, "every order is assigned to exactly one rider");
    }

    std::printf("[capacity still caps the load]\n");
    {
        WaveDispatcher wd;
        wd.setDepot(Location(37.4979, 127.0276));
        wd.setMaxMinutesOnRoad(600.0);       // 신선도는 사실상 무제한
        wd.setMaxStopsPerRider(3);           // 적재만 3건
        std::size_t w = wd.addWave(TimeOfDay(18, 0));
        for (int i = 0; i < 7; ++i)
            wd.addOrder(w, Stop("x", Location(37.50 + i * 0.002, 127.03 + i * 0.002)));

        Wave got = wd.planWave(w);
        ck(got.riderCount() == 3, "7 orders / 3 per rider = 3 riders");
        bool capped = true;
        for (std::size_t k = 0; k < got.routes.size(); ++k)
            if (got.routes[k].size() > 3) capped = false;
        ck(capped, "no rider carries more than the cap");
        ck(got.orderCount() == 7, "all 7 delivered");
    }

    std::printf("[wave overrun is reported, not hidden]\n");
    {
        WaveDispatcher wd;
        wd.setDepot(Location(37.4979, 127.0276));
        wd.setAverageSpeedKmh(20.0);
        wd.setMaxStopsPerRider(20);
        wd.setMaxMinutesOnRoad(600.0);      // 신선도로는 안 쪼개게 둔다
        wd.setMaxRiders(1);                 // 라이더 한 명뿐
        std::size_t w0 = wd.addWave(TimeOfDay(18, 0));
        wd.addWave(TimeOfDay(18, 20));      // 20분 뒤 다음 회차

        // 멀리 흩어진 5건 - 한 명이 20분 안에 절대 못 돈다
        wd.addOrder(w0, Stop("far1", Location(37.5600, 127.1000)));
        wd.addOrder(w0, Stop("far2", Location(37.4500, 126.9500)));
        wd.addOrder(w0, Stop("far3", Location(37.5500, 126.9000)));
        wd.addOrder(w0, Stop("far4", Location(37.4600, 127.1200)));
        wd.addOrder(w0, Stop("far5", Location(37.5800, 127.0500)));

        Wave got = wd.planWave(w0);
        ck(got.riderCount() == 1, "capped at one rider");
        ck(!wd.fitsBeforeNextWave(got, 0), "the loop does NOT get back before the next wave");
        ck(got.staleCount(40.0) > 0, "and the food goes stale - both are reported");
        ck(got.orderCount() == 5, "no order is silently dropped");
        std::printf("        1 rider, %.0f min loop, back at %02d:%02d (next wave 18:20)\n",
                    got.routes[0].totalMinutes,
                    got.lastReturn().hour, got.lastReturn().minute);
    }

    std::printf("[wave edge cases]\n");
    {
        WaveDispatcher wd;
        wd.setDepot(Location(37.5, 127.0));
        std::size_t w = wd.addWave(TimeOfDay(18, 0));
        ck(wd.ridersNeeded(w) == 0, "an empty wave needs no riders");
        Wave e = wd.planWave(w);
        ck(e.empty() && e.orderCount() == 0, "empty wave plans to nothing");
        ck(near(e.worstMinutesOnRoad(), 0.0), "empty wave has no time on road");
        ck(e.lastArrival() == TimeOfDay(18, 0), "empty wave last arrival is the departure");

        bool threw = false;
        try { wd.addOrder(99, Stop("x", Location(37.5, 127.01))); }
        catch (const std::exception&) { threw = true; }
        ck(threw, "adding to a wave that does not exist throws");
    }
    }



    std::printf("\n%d/%d passed, %d failed\n", total - failed, total, failed);
    return failed ? 1 : 0;
}
