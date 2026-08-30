#ifndef RECOMMEND
#define RECOMMEND
#include <cstddef>
#include <string>
#include <vector>
#include "datetime.h"
#include "day.h"
#include "domains.h"
#include "intake.h"
#include "weeklymenu.h"

// 유저가 고른 메뉴들의 "양" 을 조절해서 영양 목표에 맞추는 곳.
//
// 무엇을 먹을지는 우리가 고르지 않는다. 그 주의 메뉴판 15가지에서 유저가 3~9가지를
// 직접 고르고, 우리는 그것들을 각각 얼마나 담을지만 푼다.
//
//   주간 메뉴판 15개 ──► 유저가 3~9개 선택 ──► Pick 목록
//                                                │
//                                    MealPlanner.solve(picks, 남은 영양분)
//                                                │
//                                                ▼
//                                    Plan (메뉴별 추천 양 + 어긋난 이유)
//                                                │
//                              유저가 손으로 조절 ──► setAmount() / nudge()
//                              메뉴 구성을 바꿈  ──► addMenu() / removeItem() / replaceItem()
//                                                │
//                                    확정 ──► logPlan() ──► Day 에 Meal 로 들어감
//
// 왜 "무엇을" 이 아니라 "얼마나" 인가:
//   - 먹고 싶은 것을 못 고르게 하는 식단은 지켜지지 않는다. 선택은 사람이 한다.
//   - 목표를 맞추는 일은 사람이 못 한다. 밥 몇 g, 닭가슴살 몇 개를 암산할 수는 없다.
//   기계가 잘하는 쪽(계산)과 사람이 잘하는 쪽(취향)을 갈라 놓은 것이 이 설계의 요점이다.
//
// 우리가 내놓는 양은 추천일 뿐이다. 최종 결정은 유저가 한다:
//   - 어떻게 해도 오차범위를 못 맞추는 조합이면 막지 않는다.
//     그나마 가장 덜 어긋나는 양을 주고, issues 에 이유를 담아 경고로 띄우게 한다.
//   - 추천된 양은 언제든 바꿀 수 있다. 낱개로 파는 메뉴는 개수로, 무게/부피로 파는
//     메뉴는 그 양으로 조절한다 (PlanItem::step 이 한 칸의 크기다).
//     바꾸면 합계와 경고가 그 자리에서 다시 계산된다.
//   - 메뉴 구성 자체도 바꿀 수 있다. 넣고 빼고 갈아끼울 때마다 열량과 영양성분이
//     그 자리에서 다시 계산된다 - 확정을 누르기 전까지는 계속 움직이는 숫자다.
//     이때 나머지 항목의 양은 건드리지 않는다. 하나 뺐다고 유저가 맞춰 둔 밥의 양이
//     제멋대로 바뀌면, 화면을 다시 읽어야 하고 그건 조절이 아니라 도박이다.
namespace domains {

    // ---------- 판매용 Menu 와 섭취용 Macros 를 잇는 다리 ----------

    // 메뉴를 amount 만큼 먹었을 때의 탄단지.
    // 메뉴에 없는 영양소는 0 이 된다. 탄/단/지 외의 영양소는 여기서 무시하므로,
    // 그런 영양소가 붙은 메뉴는 Menu::caloriesFor() 쪽이 조금 더 큰 값을 준다.
    Macros macrosOf(const Menu& menu, double amount);

    // 메뉴 1단위당 열량 (0 이면 영양 정보가 없는 메뉴)
    double kcalPerUnitOf(const Menu& menu);

    // ---------- 허용 오차 ----------

    // 영양소마다 위험한 방향이 다르므로 오차를 따로 잡는다.
    //   단백질 : 넘쳐도 무해, 모자라면 문제  -> 하한만
    //   지방   : 최소량이 필요               -> 하한만
    //   탄수화물: 필수 섭취량이 없음          -> 제한 없음 (남는 오차를 흡수)
    //   열량   : 체중을 움직이는 값           -> 위아래 밴드
    struct Tolerance {
        double kcalBandPct     = 0.10;   // 열량 +-10%
        double proteinFloorPct = 0.90;   // 단백질 목표의 90% 이상
        double fatFloorPct     = 0.20;   // 지방은 그 끼니 열량의 20% 이상

        Tolerance() = default;
        Tolerance(double kcalBandPct, double proteinFloorPct, double fatFloorPct);
    };

    // ---------- 어긋난 이유 ----------

    // UI 가 경고 뱃지로 그릴 재료다.
    enum class Issue {
        CaloriesOver,    // 열량 초과
        CaloriesUnder,   // 열량 부족
        ProteinShort,    // 단백질 부족
        FatShort,        // 지방 부족
        AmountLimited    // 판매 가능한 양의 한계에 걸려 더 못 맞췄다 (메뉴를 바꿔야 한다)
    };

    // 화면에 그대로 띄울 수 있는 짧은 문구
    std::string describe(Issue issue);

    // ---------- 유저가 고른 한 가지 ----------

    // 양은 아직 비어 있다. 그걸 정하는 것이 MealPlanner 의 일이다.
    // min/max 는 유저가 "밥은 200g 넘게는 싫다" 처럼 직접 걸 수 있는 울타리다.
    // 걸지 않으면 메뉴가 파는 범위와 남은 열량이 알아서 울타리가 된다.
    struct Pick {
        MenuPtr menu;
        double minAmount = 0.0;     // 0 = 메뉴의 최소 판매량에 맡긴다
        double maxAmount = 0.0;     // 0 = 상한 없음
        bool   locked    = false;   // true 면 처음 계산에서 양을 움직이지 않는다
        double amount    = 0.0;     // locked 일 때 쓸 양

        Pick() = default;
        explicit Pick(MenuPtr menu);
        Pick(MenuPtr menu, double minAmount, double maxAmount);

        // 양을 이미 정해 둔 항목 ("아메리카노는 무조건 1잔")
        static Pick fixed(MenuPtr menu, double amount);
    };

    // 그 주의 메뉴판에서 이름으로 골라 Pick 목록을 만든다.
    // 메뉴판에 없는 이름은 건너뛰고 missing 에 담는다 (널이면 안 담는다).
    // 지어내지 않는다 - 이번 주에 안 파는 메뉴를 조용히 통과시키면
    // 주문 화면과 주방이 서로 다른 것을 보게 된다.
    std::vector<Pick> picksFrom(const WeeklyMenu& week,
                                const std::vector<std::string>& names,
                                std::vector<std::string>* missing = 0);

    // ---------- 결과 ----------

    struct PlanItem {
        MenuPtr menu;
        double amount = 0.0;      // 판매 가능한 양으로 이미 보정된 값
        Macros macros;            // 그 양만큼의 탄단지
        long long price = 0;

        // ---- 유저가 손으로 조절할 때 UI 가 쓰는 값들 ----
        double minAmount  = 0.0;  // 더 줄일 수 없는 양
        double maxAmount  = 0.0;  // 더 늘릴 수 없는 양
        double step       = 0.0;  // 팔 수 있는 양의 간격. 0 이면 그 사이 아무 값이나 된다
        double nudgeStep  = 0.0;  // +/- 한 번에 움직일 양 (step 이 0 인 메뉴도 채워진다)

        bool atMin  = false;      // 더 줄일 수 없다
        bool atMax  = false;      // 더 늘릴 수 없다
        bool locked = false;      // 유저가 처음부터 양을 정해 둔 항목

        // 개수로 조절하는 메뉴인가 (아니면 무게/부피로 조절한다)
        bool countsByUnit() const;

        // "2개" / "180g" 처럼 화면에 쓸 단위
        const std::string& unit() const;
    };

    // 조합 전체의 영양소 한 줄. 탄단지 말고 다른 영양소가 붙은 메뉴가 섞여도
    // 화면에 그대로 늘어놓을 수 있게, 이름과 단위를 달고 다닌다.
    // 메뉴를 넣거나 빼거나 양을 바꿀 때마다 이 목록이 통째로 다시 계산된다.
    struct NutrientTotal {
        std::string name;                 // "탄수화물"
        std::string unit;                 // "g"
        double amount   = 0.0;            // 조합 전체의 함량
        double calories = 0.0;            // 그 함량이 내는 열량
    };

    struct Plan {
        std::vector<PlanItem> items;
        Macros macros;                    // 조합 전체의 탄단지
        long long price = 0;

        // 조합에 실린 모든 영양소의 합계. 탄/단/지가 앞에 오고 나머지는 나온 순서대로.
        // macros 는 탄단지만 보지만 이쪽은 메뉴에 붙은 영양소를 하나도 버리지 않는다.
        std::vector<NutrientTotal> nutrients;

        // 메뉴에 실린 영양소까지 모두 더한 열량. 탄단지 말고 다른 영양소가 붙은
        // 메뉴가 있으면 calories 보다 크다 (목표 판정은 calories 로 한다).
        double nutrientCalories = 0.0;

        // ---- UI 가 그대로 쓰면 되는 값들 ----
        Macros target;                    // 맞추려던 양 (양을 바꿔도 이건 안 변한다)
        double targetCalories = 0.0;
        double calories       = 0.0;
        double calorieDelta   = 0.0;      // 양수면 초과
        double caloriePct     = 0.0;      // 목표 대비 비율 (1.0 = 딱 맞음)
        double proteinPct     = 0.0;      // 단백질 목표 대비 비율
        double fatPct         = 0.0;      // 지방 하한 대비 비율

        std::vector<Issue> issues;        // 비어 있으면 범위 안
        double score          = 0.0;      // 낮을수록 좋음

        bool isWithinTolerance() const { return issues.empty(); }
        bool has(Issue issue) const;
        std::size_t itemCount() const { return items.size(); }

        // 오차범위를 못 맞춘 채로 내놓은 답인가.
        // 참이면 UI 는 경고를 띄우되 답을 감추지는 않는다.
        bool isBestEffort() const { return !issues.empty(); }

        // 한 줄 경고 문구. 범위 안이면 빈 문자열.
        std::string warning() const;

        // 한계에 걸린 항목들의 이름. UI 가 "밥을 더 담을 수 없습니다" 로 쓴다.
        std::vector<std::string> limitedMenus() const;

        // ---- 영양소 들여다보기 ----

        // 이름으로 찾는다. 그 영양소가 조합에 없으면 널.
        const NutrientTotal* nutrient(const std::string& name) const;
        double nutrientAmount(const std::string& name) const;   // 없으면 0

        // ---- 메뉴 구성 ----

        // 이름으로 찾는다. 없으면 npos.
        static std::size_t npos() { return static_cast<std::size_t>(-1); }
        std::size_t indexOf(const std::string& menuName) const;
        bool contains(const std::string& menuName) const;
        std::vector<std::string> menuNames() const;

        // 지금 가짓수로 주문을 확정할 수 있는가 (3~9 가지).
        // 유저가 고치는 도중에는 잠깐 벗어나도 된다. 확정 버튼만 이걸로 잠근다.
        bool isValidComposition() const;
        bool canAddMenu() const;      // 더 넣을 자리가 있는가
        bool canRemoveMenu() const;   // 빼도 최소 가짓수를 지키는가

        // 가짓수가 어긋났을 때 화면에 띄울 문구. 괜찮으면 빈 문자열.
        std::string compositionWarning() const;
    };

    // ---------- 양을 푸는 곳 ----------

    class MealPlanner {
    public:
        // 한 끼에 고를 수 있는 가짓수.
        //   3 미만이면 탄/단/지를 따로 움직일 손잡이가 모자라 목표를 맞출 수 없고,
        //   9 를 넘으면 한 끼에 담을 그릇 수를 넘는다.
        static std::size_t minPicks() { return 3; }
        static std::size_t maxPicks() { return 9; }
        static bool isValidPickCount(std::size_t n);

        MealPlanner() = default;
        explicit MealPlanner(Tolerance tolerance) : tolerance_(tolerance) {}

        const Tolerance& tolerance() const { return tolerance_; }
        void setTolerance(Tolerance tolerance) { tolerance_ = tolerance; }

        // 양을 다듬는 횟수. 늘려도 답이 크게 좋아지지는 않는다 (기본 12).
        int maxSweeps() const { return maxSweeps_; }
        void setMaxSweeps(int n);

        // 고른 메뉴들의 양을 목표에 맞춘다.
        // target 은 "앞으로 더 먹어야 하는 양" (Day::remaining()).
        // 어떤 양으로도 오차범위 안에 못 들어가면, 가장 덜 어긋나는 양을 주고
        // issues 를 채워 돌려준다. 빈 결과를 주거나 예외를 던지지 않는다.
        // 가짓수가 3..9 가 아닐 때만 예외.
        Plan solve(const std::vector<Pick>& picks, const Macros& target) const;

        // 그날 남은 영양분에 맞춘다. 주문 화면이 부르는 쪽.
        Plan solveFor(const std::vector<Pick>& picks, const Day& day) const;

        // ---- 추천은 추천일 뿐이다. 최종 결정은 유저가 한다 ----

        // index 번 항목의 양을 바꾼다.
        // 팔 수 있는 양으로 보정하고 한계 안으로 넣은 뒤 합계와 경고를 다시 계산한다.
        // 실제로 정해진 양을 돌려준다 (요청한 값과 다를 수 있다).
        double setAmount(Plan& plan, std::size_t index, double amount) const;

        // 한 칸씩 올리고 내린다. 낱개 메뉴는 개수로, 무게/부피 메뉴는 계량 단위로.
        // steps 가 음수면 줄인다. 실제로 정해진 양을 돌려준다.
        double nudge(Plan& plan, std::size_t index, int steps = 1) const;

        // items[i].amount 를 직접 만졌을 때 합계/경고/점수를 다시 맞춘다.
        void recompute(Plan& plan) const;

        // ---- 메뉴 구성도 유저가 바꾼다 ----
        //
        // 마지막 화면에서 유저는 양만 만지는 것이 아니라 "이건 빼고 저걸 넣자" 를 한다.
        // 그때마다 처음부터 다시 풀면 손으로 맞춰 둔 양이 통째로 날아간다.
        // 그래서 넣고 빼는 일은 나머지 항목의 양을 건드리지 않고, 합계와 경고만
        // 그 자리에서 다시 계산한다 - 화면의 숫자가 곧바로 따라 움직인다.
        //
        //   addMenu()    -> 새 항목의 양만 목표에 맞춰 잡는다
        //   removeItem() -> 뺀 만큼 합계가 줄어든다. 남은 항목의 양은 그대로
        //   replaceItem()-> 그 자리만 갈아끼운다
        //   rebalance()  -> "다시 추천해 줘". 고정하지 않은 항목의 양을 전부 다시 푼다
        //
        // 가짓수(3~9)는 여기서 강제하지 않는다. 고치는 도중에는 2가지도 10가지도
        // 잠깐 지나간다. 막아야 하는 곳은 확정 버튼이고, 그건 isValidComposition() 이다.
        // 다만 그릇 수 한계(maxPicks) 를 넘겨 담는 것만은 막는다 - 넣을 수 없는 것을
        // 넣은 척하면 주방과 화면이 갈린다.

        // 메뉴를 하나 더 넣는다. 양은 목표에 맞춰 알아서 잡아 준다.
        // 이미 있는 메뉴면 아무것도 하지 않고 그 자리의 번호를 돌려준다.
        // 자리가 다 찼으면(maxPicks) 예외. 널이어도 예외.
        std::size_t addMenu(Plan& plan, MenuPtr menu) const;

        // 유저가 울타리를 걸어 넣을 때 ("밥은 200g 까지만", "샐러드는 1개 고정")
        std::size_t addPick(Plan& plan, const Pick& pick) const;

        // 뺀다. 남은 항목의 양은 그대로 두고 합계와 경고만 다시 계산한다.
        void removeItem(Plan& plan, std::size_t index) const;
        bool removeMenu(Plan& plan, const std::string& menuName) const;   // 뺐으면 true

        // index 번을 다른 메뉴로 갈아끼운다. 새 메뉴의 양은 목표에 맞춰 다시 잡는다.
        // 이미 다른 자리에 있는 메뉴로 갈아끼우면 예외 (같은 메뉴가 두 줄이 된다).
        std::size_t replaceItem(Plan& plan, std::size_t index, MenuPtr menu) const;

        // 구성은 그대로 두고, 고정되지 않은 항목의 양을 목표에 맞춰 다시 푼다.
        // 유저가 손으로 맞춰 둔 양을 지우는 일이므로 유저가 누를 때만 부른다.
        void rebalance(Plan& plan) const;

        // 목표를 바꾼다 (그 사이에 다른 걸 먹어서 남은 양이 달라졌을 때).
        // 구성과 양은 그대로 두고 경고 기준만 옮긴다.
        void setTarget(Plan& plan, const Macros& target) const;

    private:
        // 메뉴 하나를 항목으로 만든다. 한계/계량 단위/양을 모두 채워 준다.
        // rest 는 나머지 항목들의 탄단지 - 새 항목의 양을 그것에 맞춰 잡는다.
        PlanItem makeItem(const Pick& pick, const Macros& target,
                          const Macros& rest) const;

        Tolerance tolerance_;
        int maxSweeps_ = 12;
    };

    // ---------- 확인이 필요한 추정치 ----------

    // 사진에서 뽑은 값은 일단 넣고 나중에 확인받는다. 다만 아무 때나 묻지 않고,
    // 그 답이 화면을 바꿀 만할 때만 묻는다 - 주문 화면을 여는 순간이 그런 때다.
    //
    // 남은 예산이 900kcal 인데 불확실한 항목이 40kcal 면 양이 거의 안 바뀐다. 묻지 않는다.
    // 700kcal 짜리면 담을 양이 완전히 달라진다. 그때 묻는다.
    struct PendingConfirmation {
        std::size_t mealIndex = 0;    // Day::meals() 안에서의 위치
        std::string foodName;
        double calories = 0.0;
        double shareOfGoal = 0.0;     // 그날 목표 열량 대비 이 항목의 비중
        bool worthAsking = false;     // 문턱을 넘어 물어볼 만한가
    };

    // 아직 확인 안 된 추정치들을 열량이 큰 순서로. 큰 것부터 물어야 한다.
    // threshold 는 목표 열량 대비 비중 (기본 0.10 = 10%).
    std::vector<PendingConfirmation> pendingConfirmations(const Day& day,
                                                          double threshold = 0.10);

    // 물어볼 만한 것이 하나라도 있는가. 주문 화면을 열 때 이걸 먼저 보면 된다.
    bool shouldAskBeforePlanning(const Day& day, double threshold = 0.10);

    // 사용자가 확인해 준 값으로 고친다. 양이 그대로면 macros 를 그대로 넘기면 된다.
    // 고치고 나면 confirmed 가 되어 다시 묻지 않는다.
    void confirmMeal(Day& day, std::size_t mealIndex, const Macros& correctedPerServing);
    void confirmMeal(Day& day, std::size_t mealIndex);   // 값은 그대로, 확인만

    // 유저가 확정한 계획을 그날의 식사로 기록한다. 메뉴 한 가지가 Meal 한 개가 된다.
    // 기록하고 나면 day.remaining() 이 곧바로 갱신된다.
    void logPlan(Day& day,
                 const Plan& plan,
                 MealTime slot = MealTime::Dinner,
                 TimeOfDay clock = TimeOfDay(19, 0));

}

#endif
