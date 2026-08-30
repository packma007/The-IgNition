#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <string>
#include "calendar.h"
#include "storage.h"
#include "recommend.h"
#include "menuseed.h"
#include "food.h"
#include "foodcsv.h"
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
    std::fflush(stdout);
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

    std::printf("[WeeklyMenu: 일요일부터 토요일까지 고정]\n");
    // 2026-08-30 은 일요일이다
    ck(WeeklyMenu::weekStartOf(Date(2026,9,2)) == Date(2026,8,30),
       "수요일을 넣으면 그 주의 일요일이 나온다");
    ck(WeeklyMenu::weekStartOf(Date(2026,8,30)) == Date(2026,8,30),
       "일요일은 그대로 주 시작일");
    ck(WeeklyMenu::weekStartOf(Date(2026,9,5)) == Date(2026,8,30),
       "토요일까지가 같은 주");
    ck(WeeklyMenu::weekStartOf(Date(2026,9,6)) == Date(2026,9,6),
       "다음 일요일부터는 다음 주");
    ck(!WeeklyMenu::isSameWeek(Date(2026,9,5), Date(2026,9,6)),
       "토요일과 그다음 일요일은 다른 주");

    WeeklyMenu week(Date(2026,9,2));          // 일 8/30 ~ 토 9/5
    ck(week.weekStart() == Date(2026,8,30), "주 시작 = 일요일");
    ck(week.weekEnd()   == Date(2026,9,5),  "주 끝 = 토요일");
    ck(week.covers(Date(2026,9,5)),  "토요일도 이 주에 든다");
    ck(!week.covers(Date(2026,9,6)), "다음 일요일은 이 주가 아니다");

    week.add(salad); week.add(jeyuk); week.add(rice); week.add(soup);
    ck(week.size() == 4, "메뉴 4가지");
    week.add(salad);                          // 같은 이름을 다시
    ck(week.size() == 4, "같은 이름은 덮어쓰고 가짓수가 늘지 않는다");
    ck(week.find("rice") == rice, "이름으로 찾기");
    ck(!week.find("없는메뉴"), "없는 이름은 널");
    ck(week.slotsLeft() == WeeklyMenu::capacity() - 4, "남은 자리");

    for (int i = 0; week.size() < WeeklyMenu::capacity(); ++i) {
        std::ostringstream nm;
        nm << "filler-" << i;
        MenuPtr f(new DiscreteMenu(nm.str(), "gr", 1000, 1, 1));
        f->addNutrient<Carbohydrate>(10.0);
        f->addNutrient<Protein>(2.0);
        week.add(f);
    }
    ck(week.isFull() && week.size() == 15, "한 주는 15가지로 찬다");
    {
        bool threw = false;
        MenuPtr extra(new DiscreteMenu("sixteenth", "gr", 1000, 1, 1));
        try { week.add(extra); } catch (const std::exception&) { threw = true; }
        ck(threw, "16번째 메뉴는 조용히 버려지지 않고 예외가 된다");
    }
    ck(week.nextWeek().weekStart() == Date(2026,9,6), "다음 주 메뉴판은 다음 일요일");
    ck(week.nextWeek().empty(), "다음 주는 빈 메뉴판으로 시작한다");

    std::printf("[MenuBook: 매주 갈아끼운다]\n");
    MenuBook book;
    book.set(week);
    ck(book.size() == 1, "한 주가 들어갔다");
    ck(book.forDate(Date(2026,9,2)) != 0, "주중 아무 날로도 찾힌다");
    ck(book.forDate(Date(2026,9,5)) != 0, "토요일에도 같은 메뉴판");
    ck(book.forDate(Date(2026,9,6)) == 0, "다음 주는 아직 없다");
    {
        WeeklyMenu next = week.nextWeek();
        next.add(jeyuk);
        book.set(next);
        ck(book.size() == 2, "주마다 메뉴판이 쌓인다");
        ck(book.forDate(Date(2026,9,6))->size() == 1, "다음 주는 1가지뿐");
        ck(book.forDate(Date(2026,9,2))->size() == 15,
           "지난 주 메뉴판은 그대로 남는다 - 그래야 지난 기록을 해석할 수 있다");
    }

    std::printf("[주간 메뉴판 저장/복원]\n");
    {
        Calendar cal2(NutritionGoal(Macros(250,150,60)));
        cal2.day(Date(2026,9,2)).addMeal(Meal("rice", MealTime::Lunch, Macros(70,6,0), 1.0));

        std::ostringstream out;
        storage::write(cal2, book, out);
        ck(out.str().find("WEEKMENU\t2026\t8\t30") != std::string::npos,
           "WEEKMENU 줄이 주 시작일로 적힌다");

        Calendar back(NutritionGoal(Macros(1,1,1)));
        MenuBook backBook;
        std::istringstream in(out.str());
        storage::read(back, backBook, in);

        ck(backBook.size() == book.size(), "주차 수가 그대로");
        const WeeklyMenu* w = backBook.forDate(Date(2026,9,2));
        ck(w && w->size() == 15, "15가지가 그대로 돌아온다");
        ck(w && w->contains("rice"), "이름이 그대로");

        MenuPtr r = w ? w->find("rice") : MenuPtr();
        ck(r && r->divisibility() == Divisibility::Continuous, "무게 메뉴는 무게 메뉴로");
        ck(r && near(macrosOf(*r, 100.0).carbG, 35.0), "영양소가 그대로");
        ck(r && !r->isValidAmount(45.0) && r->isValidAmount(50.0),
           "최소 판매량과 계량 단위까지 복원된다");

        MenuPtr j = w ? w->find("jeyuk") : MenuPtr();
        ck(j && j->divisibility() == Divisibility::Discrete, "낱개 메뉴는 낱개 메뉴로");
        ck(j && j->unitPrice() == 9000, "가격이 그대로");

        ck(back.mealCount() == 1, "같은 파일에서 그날의 기록도 같이 돌아온다");

        Calendar plain(NutritionGoal(Macros(1,1,1)));
        std::istringstream in2(out.str());
        storage::read(plain, in2);
        ck(plain.mealCount() == 1, "메뉴판을 안 받는 read() 도 v4 파일을 읽는다");
    }

    std::printf("[MealPlanner: 고른 메뉴의 양을 맞춘다]\n");
    // 유저가 고르고, 우리는 양만 푼다.
    MenuPtr chicken(new ContinuousMenu("chicken", "g", 25, 50.0, 300.0, 10.0));
    chicken->addNutrient<Protein>(0.31);
    chicken->addNutrient<Fat>(0.04);

    MealPlanner planner;

    ck(!MealPlanner::isValidPickCount(2), "2가지는 너무 적다");
    ck(MealPlanner::isValidPickCount(3) && MealPlanner::isValidPickCount(9),
       "3~9 가지가 유효 범위");
    ck(!MealPlanner::isValidPickCount(10), "10가지는 너무 많다");
    {
        std::vector<Pick> few;
        few.push_back(Pick(salad));
        few.push_back(Pick(rice));
        bool threw = false;
        try { planner.solve(few, Macros(40,45,12)); }
        catch (const std::exception&) { threw = true; }
        ck(threw, "가짓수가 모자라면 조용히 통과시키지 않고 예외");
    }

    // 남은 영양분 700 kcal / 단백질 50 g
    Macros budget(80, 50, 20);
    ck(near(budget.calories(), 700.0), "예산은 700kcal");

    std::vector<Pick> picks;
    picks.push_back(Pick(rice));
    picks.push_back(Pick(chicken));
    picks.push_back(Pick(salad));

    Plan plan = planner.solve(picks, budget);
    ck(plan.itemCount() == 3, "고른 가짓수는 그대로 나온다 - 우리가 빼거나 더하지 않는다");
    ck(near(plan.targetCalories, 700.0), "목표는 남은 열량");

    bool sellable = true, consistent = true;
    Macros sum;
    long long price = 0;
    for (std::size_t i = 0; i < plan.items.size(); ++i) {
        if (!plan.items[i].menu->isValidAmount(plan.items[i].amount)) sellable = false;
        if (!near(plan.items[i].macros.carbG,
                  macrosOf(*plan.items[i].menu, plan.items[i].amount).carbG))
            consistent = false;
        sum   += plan.items[i].macros;
        price += plan.items[i].price;
    }
    ck(sellable, "정해진 양은 모두 실제로 팔 수 있는 양이다");
    ck(consistent, "항목의 탄단지는 그 양에서 나온 값이다");
    ck(near(sum.calories(), plan.calories) && price == plan.price,
       "항목을 더하면 합계가 된다");
    ck(near(plan.calorieDelta, plan.calories - plan.targetCalories), "delta 가 맞다");

    ck(plan.isWithinTolerance(), "밥/닭/샐러드 셋이면 700kcal 목표에 들어간다");
    ck(plan.warning().empty(), "범위 안이면 경고 문구가 없다");
    ck(!plan.isBestEffort(), "범위 안이면 최선책이 아니라 정답이다");
    ck(std::fabs(plan.calorieDelta) <= 70.0 + 1e-6, "열량 오차 10% 안");
    ck(plan.macros.proteinG >= 45.0 - 1e-6, "단백질은 목표의 90% 이상");
    ck(plan.macros.fatG >= 700.0 * 0.20 / 9.0 - 1e-6, "지방은 그 끼니 열량의 20% 이상");

    std::printf("[양만 바꿔서 목표를 따라간다]\n");
    {
        // 같은 메뉴를 골라도 목표가 작아지면 양이 줄어야 한다
        Plan small = planner.solve(picks, Macros(40, 25, 10));
        double before = 0.0, after = 0.0;
        for (std::size_t i = 0; i < plan.items.size(); ++i)  before += plan.items[i].amount;
        for (std::size_t i = 0; i < small.items.size(); ++i) after  += small.items[i].amount;
        ck(small.calories < plan.calories, "목표가 줄면 담는 열량도 준다");
        ck(after < before, "같은 메뉴인데 양이 줄었다");
        ck(small.price <= plan.price, "적게 담으면 값도 싸다");
    }

    std::printf("[유저가 양을 고정한 항목]\n");
    {
        std::vector<Pick> locked;
        locked.push_back(Pick(rice));
        locked.push_back(Pick(chicken));
        locked.push_back(Pick::fixed(salad, 1.0));   // 샐러드는 무조건 1개
        Plan lp = planner.solve(locked, budget);
        ck(near(lp.items[2].amount, 1.0), "고정한 양은 손대지 않는다");
        ck(lp.items[2].locked, "고정 표시가 남는다");
        ck(lp.isWithinTolerance(), "나머지 둘을 움직여 목표에 맞춘다");
    }

    std::printf("[유저가 건 상한]\n");
    {
        std::vector<Pick> capped;
        capped.push_back(Pick(rice, 0.0, 100.0));    // 밥은 100g 까지만
        capped.push_back(Pick(chicken));
        capped.push_back(Pick(salad));
        Plan cp = planner.solve(capped, budget);
        ck(cp.items[0].amount <= 100.0 + 1e-9, "유저가 건 상한을 넘지 않는다");
    }

    std::printf("[맞출 수 없을 때는 숨기지 않고 알린다]\n");
    {
        // 아무리 담아도 700kcal 에 못 미치는 메뉴들
        MenuPtr tiny1(new ContinuousMenu("tiny-1", "g", 5, 10.0, 20.0, 10.0));
        tiny1->addNutrient<Carbohydrate>(0.1);
        MenuPtr tiny2(new ContinuousMenu("tiny-2", "g", 5, 10.0, 20.0, 10.0));
        tiny2->addNutrient<Protein>(0.1);
        MenuPtr tiny3(new ContinuousMenu("tiny-3", "g", 5, 10.0, 20.0, 10.0));
        tiny3->addNutrient<Fat>(0.1);

        std::vector<Pick> tp;
        tp.push_back(Pick(tiny1)); tp.push_back(Pick(tiny2)); tp.push_back(Pick(tiny3));
        Plan t = planner.solve(tp, budget);
        ck(!t.isWithinTolerance(), "못 맞춘 답을 맞춘 척하지 않는다");
        ck(t.has(Issue::CaloriesUnder), "열량 부족이 표시된다");
        ck(t.has(Issue::AmountLimited), "양의 한계에 걸렸음을 알린다");
        ck(!t.limitedMenus().empty(), "어떤 메뉴가 한계인지 이름으로 알려 준다");
        ck(t.itemCount() == 3, "그래도 답은 돌려준다 - 막지 않는다");
        ck(t.isBestEffort(), "오차범위를 못 맞춘 답임을 스스로 밝힌다");
        ck(!t.warning().empty(), "화면에 그대로 띄울 경고 문구가 나온다");

        // 그나마 가장 덜 어긋난 양이어야 한다 - 셋 다 끝까지 담은 것이 최선이다
        bool allMax = true;
        for (std::size_t i = 0; i < t.items.size(); ++i)
            if (!t.items[i].atMax) allMax = false;
        ck(allMax, "맞출 수 없으면 가장 덜 어긋나는 양으로 담아 준다");

        // 그래도 유저는 여전히 양을 바꿀 수 있다
        planner.nudge(t, 0, -1);
        ck(t.items[0].amount < t.items[0].maxAmount, "경고가 떠도 양은 유저가 바꾼다");
    }

    std::printf("[탄수화물만 골랐다면 단백질은 알려만 준다]\n");
    {
        MenuPtr c1(new ContinuousMenu("carb-1", "g", 3, 0.0, 500.0, 10.0));
        c1->addNutrient<Carbohydrate>(0.8);
        MenuPtr c2(new ContinuousMenu("carb-2", "g", 3, 0.0, 500.0, 10.0));
        c2->addNutrient<Carbohydrate>(0.6);
        MenuPtr c3(new ContinuousMenu("carb-3", "g", 3, 0.0, 500.0, 10.0));
        c3->addNutrient<Carbohydrate>(0.4);

        std::vector<Pick> cp;
        cp.push_back(Pick(c1)); cp.push_back(Pick(c2)); cp.push_back(Pick(c3));
        Plan c = planner.solve(cp, budget);
        ck(c.has(Issue::ProteinShort), "단백질 부족이 표시된다");
        ck(c.has(Issue::FatShort), "지방 부족이 표시된다");
        ck(std::fabs(c.calorieDelta) <= 70.0 + 1e-6,
           "맞출 수 있는 열량은 그래도 맞춘다 - 넘치는 탄수화물에는 벌점이 없다");
    }

    std::printf("[예산이 이미 없을 때]\n");
    {
        Plan zero = planner.solve(picks, Macros(0, 0, 0));
        ck(zero.has(Issue::CaloriesOver), "예산이 없으면 뭘 담아도 초과다");
        bool allMin = true;
        for (std::size_t i = 0; i < zero.items.size(); ++i)
            if (!zero.items[i].atMin) allMin = false;
        ck(allMin, "그래도 가장 적은 양으로 담아 준다");
    }

    std::printf("[추천은 추천일 뿐 - 유저가 양을 바꾼다]\n");
    {
        Plan p = planner.solve(picks, budget);   // 0=rice(무게) 1=chicken(무게) 2=salad(낱개)
        ck(!p.items[0].countsByUnit(), "밥은 양(g)으로 조절한다");
        ck(p.items[2].countsByUnit(),  "샐러드는 개수로 조절한다");
        ck(near(p.items[0].step, 10.0), "밥의 한 칸은 계량 단위 10g");
        ck(near(p.items[2].nudgeStep, 1.0), "샐러드의 한 칸은 1개");
        ck(p.items[0].unit() == "g", "화면에 쓸 단위가 항목에 실려 있다");
        ck(p.items[0].minAmount >= 50.0 && p.items[0].maxAmount <= 400.0,
           "조절할 수 있는 범위도 실려 있다");

        double asked = p.items[0].amount + 35.0;
        double got = planner.setAmount(p, 0, asked);
        ck(near(got, p.items[0].amount), "정해진 양을 그대로 돌려준다");
        ck(p.items[0].menu->isValidAmount(got),
           "손으로 넣은 값도 팔 수 있는 양으로 보정된다");
        ck(std::fabs(got - asked) <= 5.0 + 1e-9, "가장 가까운 칸으로 붙는다");

        Macros sum2;
        for (std::size_t i = 0; i < p.items.size(); ++i) sum2 += p.items[i].macros;
        ck(near(sum2.calories(), p.calories), "양을 바꾸면 합계가 그 자리에서 다시 계산된다");
        ck(near(p.calorieDelta, p.calories - p.targetCalories), "경고 기준도 같이 갱신된다");

        planner.setAmount(p, 0, 99999.0);
        ck(near(p.items[0].amount, p.items[0].maxAmount) && p.items[0].atMax,
           "상한을 넘겨 넣으면 상한에서 멈춘다");
        planner.setAmount(p, 0, -5.0);
        ck(near(p.items[0].amount, p.items[0].minAmount) && p.items[0].atMin,
           "최소 판매량 아래로는 못 내려간다");

        double up1 = planner.nudge(p, 2, +1);       // 샐러드 1개 늘리기
        ck(near(up1, p.items[2].minAmount + 1.0), "낱개 메뉴는 1개씩 움직인다");
        planner.nudge(p, 2, -1);
        ck(near(p.items[2].amount, p.items[2].minAmount), "다시 1개 줄이면 제자리");

        double back = p.items[1].amount;
        planner.nudge(p, 1, +2);
        ck(near(p.items[1].amount, back + 20.0), "무게 메뉴는 계량 단위만큼 움직인다");
    }

    std::printf("[손으로 맞춰도 추천과 같은 잣대로 본다]\n");
    {
        // 추천이 내놓은 그 양을 손으로 그대로 다시 넣으면 판정도 같아야 한다
        Plan a = planner.solve(picks, budget);
        Plan b = a;
        for (std::size_t i = 0; i < b.items.size(); ++i)
            planner.setAmount(b, i, a.items[i].amount);
        ck(near(a.score, b.score), "같은 양이면 같은 점수");
        ck(a.issues.size() == b.issues.size(), "같은 양이면 같은 경고");
        ck(a.isWithinTolerance() == b.isWithinTolerance(), "초록불/빨간불이 갈리지 않는다");
    }

    std::printf("[그날 남은 영양분으로 바로 풀기]\n");
    Day today(Date(2026,8,30), NutritionGoal(Macros(250, 150, 60)));
    today.addMeal(Meal("breakfast", MealTime::Breakfast, Macros(60, 20, 10)));
    double before = today.remainingCalories();

    Plan dinner = planner.solveFor(picks, today);
    ck(near(dinner.targetCalories, before), "목표 = 그날 남은 열량");

    std::size_t mealsBefore = today.mealCount();
    logPlan(today, dinner);
    ck(today.mealCount() == mealsBefore + dinner.itemCount(), "메뉴 하나가 Meal 하나");
    ck(near(today.remainingCalories(), before - dinner.calories),
       "남은 양이 담은 열량만큼 정확히 줄어든다");
    ck(today.meals()[mealsBefore].time() == MealTime::Dinner, "저녁으로 기록");
    ck(today.meals()[mealsBefore].hasExactTime(), "시각까지 남는다");
    ck(near(today.meals()[mealsBefore].servings(), dinner.items[0].amount),
       "기록에 담은 양이 그대로 남는다");

    std::printf("[주간 메뉴판에서 이름으로 고르기]\n");
    {
        std::vector<std::string> names;
        names.push_back("rice");
        names.push_back("jeyuk");
        names.push_back("miyeok-guk");
        names.push_back("이번주에는 없는 메뉴");
        std::vector<std::string> missing;
        std::vector<Pick> fromWeek = picksFrom(week, names, &missing);
        ck(fromWeek.size() == 3, "메뉴판에 있는 것만 고른다");
        ck(missing.size() == 1 && missing[0] == "이번주에는 없는 메뉴",
           "안 파는 메뉴는 지어내지 않고 빠진 것으로 알린다");
        ck(planner.solve(fromWeek, budget).itemCount() == 3, "그대로 풀 수 있다");
    }


    std::printf("[MVP 메뉴판 15가지]\n");
    WeeklyMenu mvp = mvpWeeklyMenu(Date(2026,9,2));
    ck(mvp.size() == 15, "15가지가 모두 채워진다");
    ck(mvp.isFull(), "메뉴판이 찼다");
    ck(mvp.weekStart() == Date(2026,8,30), "그 날이 속한 주로 맞춰진다");
    ck(mvpMenuNames().size() == 15, "이름 목록도 15개");

    {
        // 표에 적은 대로 들어갔는가 (100g / 1개 기준)
        MenuPtr bap = mvp.find("잡곡밥");
        ck(bap && near(macrosOf(*bap, 100.0).carbG, 33.0), "잡곡밥 100g 탄수화물 33g");
        ck(bap && near(macrosOf(*bap, 100.0).proteinG, 3.5), "잡곡밥 100g 단백질 3.5g");
        ck(bap && near(kcalPerUnitOf(*bap) * 100.0, 151.4), "잡곡밥 100g = 151.4kcal");

        MenuPtr godeung = mvp.find("고등어구이");
        ck(godeung && godeung->divisibility() == Divisibility::Discrete,
           "고등어구이는 토막으로 판다 - 43g 만 팔 수는 없다");
        ck(godeung && godeung->unit() == "토막", "단위는 토막");
        ck(godeung && near(macrosOf(*godeung, 1.0).proteinG, 14.0), "1토막 단백질 14g");
        ck(godeung && near(macrosOf(*godeung, 1.0).carbG, 0.0),
           "생선에 탄수화물이 0 인 것은 모르는 것이 아니라 정말 0 이다");
        ck(godeung && godeung->findNutrient("탄수화물") != 0,
           "그래서 0 이라도 영양소를 빼지 않고 넣어 둔다");

        MenuPtr jeyuk2 = mvp.find("제육볶음");
        ck(jeyuk2 && near(kcalPerUnitOf(*jeyuk2) * 100.0, 205.0), "제육볶음 100g = 205kcal");
    }

    {
        // 열다섯 가지 전부 팔 수 있는 상태인가
        bool sane = true;
        for (std::size_t i = 0; i < mvp.menus().size(); ++i) {
            const MenuPtr& m = mvp.at(i);
            if (m->unitPrice() <= 0) sane = false;
            if (!m->isValidAmount(m->normalize(0.0))) sane = false;
            if (!m->findNutrient("탄수화물") || !m->findNutrient("단백질")
                || !m->findNutrient("지방")) sane = false;
            double per100 = kcalPerUnitOf(*m)
                          * (m->divisibility() == Divisibility::Discrete ? 1.0 : 100.0);
            if (per100 < 50.0 || per100 > 300.0) sane = false;   // 상식 밖의 값 거르기
        }
        ck(sane, "열다섯 가지 모두 가격/영양소/최소 판매량이 멀쩡하다");
    }

    ck(mvpMenu("제육볶음") && mvpMenu("제육볶음")->name() == "제육볶음", "한 가지만 꺼내기");
    ck(!mvpMenu("탕수육"), "표에 없는 이름은 지어내지 않는다");

    std::printf("[MVP 메뉴로 실제 한 끼 풀기]\n");
    {
        MealPlanner mp;
        Macros dinnerGoal(80, 50, 20);          // 700kcal / 단백질 50g
        ck(near(dinnerGoal.calories(), 700.0), "저녁 목표는 700kcal");

        std::vector<std::string> want;
        want.push_back("잡곡밥");
        want.push_back("간장 닭다리살 구이");
        want.push_back("계란말이");
        std::vector<std::string> lost;
        std::vector<Pick> chosen = picksFrom(mvp, want, &lost);
        ck(chosen.size() == 3 && lost.empty(), "메뉴판에서 세 가지를 골랐다");

        Plan m = mp.solve(chosen, dinnerGoal);
        ck(m.isWithinTolerance(), "밥/닭다리살/계란말이 조합은 700kcal 목표에 들어간다");
        ck(m.macros.proteinG >= 45.0, "단백질 하한(90%) 이상");
        ck(m.macros.fatG >= 700.0 * 0.20 / 9.0, "지방 하한(끼니 열량의 20%) 이상");

        bool sellable = true;
        for (std::size_t i = 0; i < m.items.size(); ++i)
            if (!m.items[i].menu->isValidAmount(m.items[i].amount)) sellable = false;
        ck(sellable, "밥은 10g 단위로, 계란말이는 줄 단위로 나온다");

        // 계란말이는 낱개라 반 줄이 나올 수 없다
        MenuPtr egg = mvp.find("계란말이");
        std::size_t eggAt = 2;
        ck(m.items[eggAt].menu == egg && m.items[eggAt].countsByUnit(),
           "계란말이는 개수로 조절하는 항목");
        ck(near(m.items[eggAt].amount, std::floor(m.items[eggAt].amount)),
           "낱개 메뉴에 소수점이 나오지 않는다");
        ck(near(m.items[eggAt].nudgeStep, 1.0), "한 번에 1줄씩 움직인다");
        ck(m.items[0].step > 0.0 && near(m.items[0].step, 10.0), "밥은 10g 씩");
    }

    std::printf("[어떻게 해도 안 맞는 조합은 경고하고 최선을 준다]\n");
    {
        MealPlanner mp;
        // 삼겹살 김치볶음은 100g 에 258kcal 인데 단백질은 11g 뿐이다.
        // 515kcal 안에서 단백질 35g 을 뽑는 것은 이 조합으로는 불가능하다.
        std::vector<std::string> want;
        want.push_back("잡곡밥");
        want.push_back("삼겹살 김치볶음");
        want.push_back("브로콜리 참깨무침");
        std::vector<Pick> chosen = picksFrom(mvp, want, 0);

        Plan m = mp.solve(chosen, Macros(60, 35, 15));
        ck(m.itemCount() == 3, "못 맞춰도 답은 돌려준다");
        ck(m.isBestEffort() && !m.warning().empty(), "경고를 띄운다");
        ck(m.has(Issue::CaloriesOver), "열량 초과가 이유로 잡힌다");

        // 유저가 직접 줄일 수 있어야 한다
        double before = m.calories;
        mp.setAmount(m, 1, 100.0);              // 삼겹살을 100g 으로
        ck(near(m.items[1].amount, 100.0), "유저가 넣은 양이 그대로 들어간다");
        ck(m.calories < before, "합계가 그 자리에서 줄어든다");
    }

    std::printf("[MVP 메뉴판도 파일로 오간다]\n");
    {
        MenuBook mvpBook;
        mvpBook.set(mvp);
        Calendar empty(NutritionGoal(Macros(250,150,60)));

        std::ostringstream out;
        storage::write(empty, mvpBook, out);

        Calendar back(NutritionGoal(Macros(1,1,1)));
        MenuBook backBook;
        std::istringstream in(out.str());
        storage::read(back, backBook, in);

        const WeeklyMenu* w = backBook.forDate(Date(2026,9,2));
        ck(w && w->size() == 15, "15가지가 그대로 돌아온다");
        MenuPtr g = w ? w->find("고등어구이") : MenuPtr();
        ck(g && g->divisibility() == Divisibility::Discrete && g->unit() == "토막",
           "토막으로 파는 것까지 그대로");
        ck(g && near(macrosOf(*g, 2.0).fatG, 21.0), "2토막 지방 21g");
    }

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
        logPlan(d2, planner.solveFor(picks, d2));
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

        ck(shouldAskBeforePlanning(gd), "order screen should ask first");

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

        ck(!shouldAskBeforePlanning(gd), "nothing big left to ask about");
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



    // ================= 공공 영양성분 DB 표 파일 =================

    std::printf("[csv 한 줄 자르기]\n");
    {
        std::vector<std::string> c = parseCsvLine("D001,비빔밥,100g", ',');
        ck(c.size() == 3 && c[1] == "비빔밥", "commas split into cells");

        c = parseCsvLine("D002,\"라면, 봉지\",100g", ',');
        ck(c.size() == 3, "a comma inside quotes does not split the cell");
        ck(c[1] == "라면, 봉지", "the quoted cell keeps its comma");

        c = parseCsvLine("a,\"he said \"\"hi\"\"\",b", ',');
        ck(c[1] == "he said \"hi\"", "doubled quotes become one quote");

        c = parseCsvLine("a\tb\tc", '\t');
        ck(c.size() == 3 && c[1] == "b", "tab separated splits too");

        c = parseCsvLine("a,,c", ',');
        ck(c.size() == 3 && c[1].empty(), "an empty cell in the middle survives");
    }

    std::printf("[csv 기준량과 숫자 읽기]\n");
    {
        ck(near(gramsFromAmount("100g"), 100.0), "100g");
        ck(near(gramsFromAmount("1회분(200g)"), 200.0), "the grams inside the parentheses win");
        ck(near(gramsFromAmount("1개(60g)"), 60.0), "1 piece = 60g");
        ck(near(gramsFromAmount("200mL"), 200.0), "1mL is taken as 1g");
        ck(near(gramsFromAmount("0.5kg"), 500.0), "kg becomes grams");
        ck(near(gramsFromAmount("1컵"), 0.0), "a unit we cannot weigh gives 0, not a guess");
        ck(near(gramsFromAmount(""), 0.0), "an empty amount gives 0");

        bool ok = false;
        ck(near(nutrientNumber("12.5", ok), 12.5) && ok, "a plain number reads");
        nutrientNumber("", ok);
        ck(!ok, "a blank cell is unknown, not zero");
        nutrientNumber("-", ok);
        ck(!ok, "a dash is unknown, not zero");
        nutrientNumber("N/A", ok);
        ck(!ok, "N/A is unknown, not zero");
        ck(near(nutrientNumber("Tr", ok), 0.0) && ok, "Tr (trace) is a KNOWN zero");
        ck(near(nutrientNumber("1,234.5", ok), 1234.5) && ok, "thousands separator");
    }

    std::printf("[이름에서 분류 접미 떼기]\n");
    {
        ck(foodBaseKey("김치찌개_음식점") == foodKey("김치찌개"), "underscore suffix dropped");
        ck(foodBaseKey("라면, 봉지") == foodKey("라면"), "comma suffix dropped");
        ck(foodBaseKey("우유(저지방)") == foodKey("우유"), "parenthesised suffix dropped");
        ck(foodBaseKey("비빔밥") == foodKey("비빔밥"), "a plain name is its own base");
    }

    std::printf("[식약처 표 읽기]\n");
    FoodCsvSource mfds;
    {
        std::string csv =
            "\xEF\xBB\xBF" "식품영양성분DB 20240301 배포본 (샘플 11줄)\r\n"
            "식품코드,식품명,데이터구분명,영양성분함량기준량,에너지(kcal),단백질(g),지방(g),탄수화물(g),식품중량\r\n"
            "D001,김치찌개_음식점,음식점,100g,45,4.5,2.8,3.1,400g\r\n"
            "D002,비빔밥,음식점,100g,131,5.2,3.4,20.1,500g\r\n"
            "D003,닭가슴살_구운것,가공식품,100g,165,31.0,3.6,0.0,100g\r\n"
            "D004,된장찌개,음식점,100g,52,4.1,2.2,4.0,400g\r\n"
            "D005,흰밥,가정식,100g,143,2.6,0.3,31.7,210g\r\n"
            "D006,오렌지주스,가공식품,200mL,88,1.4,0.2,20.6,200mL\r\n"
            "D007,제육덮밥,음식점,1회분(450g),,25.0,28.0,110.0,450g\r\n"
            "D008,\"라면, 봉지\",가공식품,100g,,9.0,17.0,65.0,120g\r\n"
            "D009,미역국,음식점,100g,,2.0,Tr,1.5,350g\r\n"
            "D010,물,가공식품,100g,,-,-,-,\r\n"
            "D011,,가공식품,100g,,1.0,1.0,1.0,100g\r\n";
        std::istringstream in(csv);
        CsvReport r = mfds.read(in);

        ck(r.delimiter == ',', "comma delimiter detected");
        ck(mfds.size() == 9, "9 foods loaded");
        ck(r.dataRows == 11, "11 data rows seen");
        ck(r.skipped == 2, "2 rows dropped: no macros at all, and no name");
        ck(r.warnings.size() >= 2, "and every dropped row is reported, not swallowed");
        ck(!r.encodingSuspect, "a utf-8 file passes the encoding check");
        ck(r.mlRows == 1, "one row was measured in mL");
        std::printf("        %s\n", r.summary().c_str());

        FoodInfo f;
        ck(mfds.lookup("비빔밥", f), "exact name found");
        ck(f.source == MacroSource::Official, "marked as public DB, not an estimate");
        ck(near(f.per100g.carbG, 20.1) && near(f.per100g.proteinG, 5.2),
           "per-100g values come straight from the table");
        ck(near(f.servingGrams, 500.0), "식품중량 500g became the serving size");
        ck(near(f.confidence, 1.0), "an exact name is fully confident");
        ck(f.origin.find("식약처") != std::string::npos, "origin says where it came from");

        FoodInfo k;
        ck(mfds.lookup("김치찌개", k), "김치찌개 found through 김치찌개_음식점");
        ck(k.name == "김치찌개_음식점", "and it reports the row it actually matched");
        ck(near(k.per100g.proteinG, 4.5), "with that row's values");
        ck(k.confidence < 1.0, "a suffix match is NOT presented as certain");

        FoodInfo sp;
        ck(mfds.lookup("김치 찌개", sp) && sp.name == k.name, "a space in the typed name is ignored");

        // 기준량이 100g 이 아닌 줄. 여기서 안 맞추면 한 그릇을 두 그릇으로 기록한다.
        FoodInfo je;
        ck(mfds.lookup("제육덮밥", je), "row measured per serving, not per 100g");
        ck(near(je.per100g.proteinG, 25.0 * 100.0 / 450.0), "converted down to per-100g");
        ck(near(je.forServings(1.0).proteinG, 25.0), "one serving gives the table value back");
        ck(near(je.forServings(1.0).calories(), 110.0 * 4 + 25.0 * 4 + 28.0 * 9),
           "and the calories of one serving survive the round trip");

        FoodInfo oj;
        ck(mfds.lookup("오렌지주스", oj), "a drink measured in 200mL");
        ck(near(oj.per100g.carbG, 10.3), "200mL basis halved to 100g");

        FoodInfo ra;
        ck(mfds.lookup("라면", ra) && ra.name == "라면, 봉지", "quoted name is matched by its base");

        FoodInfo mi;
        ck(mfds.lookup("미역국", mi) && near(mi.per100g.fatG, 0.0), "a Tr row is kept as zero fat");

        FoodInfo miss;
        ck(!mfds.lookup("김치", miss), "김치 does NOT silently become 김치찌개");
        ck(!mfds.lookup("밥", miss), "밥 does NOT silently become 비빔밥");
        ck(!mfds.lookup("탕수육", miss), "a food that is not in the table is reported missing");
    }

    std::printf("[표에서 후보 늘어놓기]\n");
    {
        std::vector<FoodInfo> hits = mfds.search("찌개");
        ck(hits.size() == 2, "두 찌개 both show up as candidates");
        ck(hits[0].name == "된장찌개", "the shorter, more general name comes first");

        std::vector<FoodInfo> rice = mfds.search("밥");
        ck(rice.size() == 3, "밥 matches three foods - here we DO show them all");
        ck(rice[0].name == "흰밥", "shortest first");
        ck(mfds.search("밥", 1).size() == 1, "limit is honoured");
        ck(mfds.search("없는것").empty(), "no candidates for a food we do not have");
    }

    std::printf("[FoodResolver + 식약처 표]\n");
    {
        LocalFoodDatabase cache;
        FakeAI ai;
        FoodResolver rs;
        rs.setCache(&cache);
        rs.addSource(&cache);   // 1. 이미 아는 것
        rs.addSource(&mfds);    // 2. 공공 DB
        rs.addSource(&ai);      // 3. 마지막 수단

        FoodInfo got;
        ck(rs.resolve("비빔밥", got), "resolved through the public DB");
        ck(got.source == MacroSource::Official, "and it is Official, not an estimate");
        ck(ai.calls == 0, "the AI was never called for a food the public DB knows");
        ck(cache.size() == 1, "the answer was cached");

        FoodInfo again;
        ck(rs.resolve("비빔밥", again) && ai.calls == 0, "the second time comes from the cache");

        FoodInfo weird;
        ck(rs.resolve("사장님표 정체불명 볶음", weird), "an unknown food still gets an answer");
        ck(weird.source == MacroSource::Estimated, "but it is flagged as an estimate");
        ck(ai.calls == 1, "and only then was the AI called");

        Meal m = got.toMeal(MealTime::Lunch, TimeOfDay(12, 30), 500.0);
        ck(m.source() == MacroSource::Official, "the meal carries the public DB as its source");
        ck(!m.isConfirmed(), "the amount is still our guess, so it is not confirmed");
        ck(near(m.total().carbG, 100.5), "500g of bibimbap = 100.5g carbs");

        LocalFoodDatabase small;
        std::vector<std::string> favourites;
        favourites.push_back("비빔밥");
        favourites.push_back("된장찌개");
        favourites.push_back("없는음식");
        ck(mfds.exportTo(small, favourites) == 2, "two of three favourites were found");
        ck(small.size() == 2, "and they moved into a small local table we can ship");
    }

    std::printf("[칸 순서가 달라도, 탭으로 나눠도]\n");
    {
        std::string tsv =
            "단백질(g)\t식품명\t지방\t탄수화물\n"
            "31.0\t닭가슴살\t3.6\t0.0\n";
        std::istringstream in(tsv);
        FoodCsvSource f;
        CsvReport r = f.read(in);
        ck(r.delimiter == '\t', "tab delimiter detected");
        ck(f.size() == 1, "column order does not matter - we read by header name");
        FoodInfo c;
        ck(f.lookup("닭가슴살", c) && near(c.per100g.proteinG, 31.0), "found with the right values");
        ck(!c.hasServing(), "no 식품중량 column means we admit we do not know the serving");
        ck(near(c.per100g.calories(), 31.0 * 4 + 3.6 * 9), "no 기준량 column falls back to 100g");
    }

    std::printf("[표가 아닌 파일, 잘못된 인코딩]\n");
    {
        std::string junk = "hello\nworld\n1,2,3\n";
        std::istringstream in(junk);
        FoodCsvSource f;
        CsvReport r = f.read(in);
        ck(r.loaded == 0 && f.empty(), "nothing loads from a file that is not a food table");
        ck(!r.warnings.empty(), "and it says it could not find the header");
        FoodInfo x;
        ck(!f.lookup("비빔밥", x), "an empty source answers nothing instead of crashing");

        // 엑셀이 기본으로 뱉는 CP949 파일. 조용히 깨진 이름을 담으면 영영 못 찾는다.
        std::string cp949 =
            "식품명,단백질(g),지방(g),탄수화물(g),영양성분함량기준량\n";
        cp949 += "\xB1\xE8\xC4\xA1\xC2\xCC\xB0\xB3,4.5,2.8,3.1,100g\n";
        std::istringstream in2(cp949);
        FoodCsvSource f2;
        CsvReport r2 = f2.read(in2);
        ck(r2.encodingSuspect, "a cp949 file is caught, not silently mangled");
        ck(!r2.warnings.empty(), "and the warning tells the user how to re-save it");
    }

    std::printf("[표 파일을 디스크에서]\n");
    {
        FoodCsvSource f;
        CsvReport r;
        std::string err;
        bool loaded = f.load("sample/food_nutrition_sample.csv", &r, &err);
        ck(loaded, "the sample csv loads from disk");
        if (!loaded) std::printf("        %s\n", err.c_str());
        ck(f.size() == 9, "the same 9 foods as the in-memory copy");
        FoodInfo b;
        ck(f.lookup("비빔밥", b) && near(b.per100g.carbG, 20.1), "and they resolve the same way");

        FoodCsvSource none;
        std::string err2;
        ck(!none.load("sample/이런파일없다.csv", 0, &err2), "a missing file fails cleanly");
        ck(!err2.empty(), "with a message that names the file");
    }

    std::printf("\n%d/%d passed, %d failed\n", total - failed, total, failed);
    return failed ? 1 : 0;
}
