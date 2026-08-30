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
//         │                                      │
//   오늘 재고(StockLimits) ──── 재료 떨어진 것은 여기서 빠진다
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
//
// 딱 하나, 재고만은 추천이 아니라 벽이다:
//   영양은 못 맞춰도 답을 주지만, 없는 재료는 담을 수 없다. 오늘 다 나간 메뉴는
//   애초에 목록에서 빠지고(canServe/canAddMenu), 남은 것이 모자란 메뉴는
//   그 양까지만 담긴다(PlanItem::stockCap). 화면에 담긴 것은 전부 실제로 주문된다.
//   재고를 안 꽂으면 이 벽이 없고, 지금까지와 똑같이 돈다.
namespace domains {

    // ---------- 판매용 Menu 와 섭취용 Macros 를 잇는 다리 ----------

    // 메뉴를 amount 만큼 먹었을 때의 탄단지.
    // 메뉴에 없는 영양소는 0 이 된다. 탄/단/지 외의 영양소는 여기서 무시하므로,
    // 그런 영양소가 붙은 메뉴는 Menu::caloriesFor() 쪽이 조금 더 큰 값을 준다.
    Macros macrosOf(const Menu& menu, double amount);

    // 메뉴 1단위당 열량 (0 이면 영양 정보가 없는 메뉴)
    double kcalPerUnitOf(const Menu& menu);

    // ---------- 재고를 안 꽂았을 때의 상한 ----------

    // StockLimits 를 꽂지 않으면 모든 메뉴의 상한이 이 값이다.
    // 하루에 이만큼 나가는 메뉴는 없으므로 사실상 "제한 없음" 이다.
    // (inventory.h 의 kUnlimited 와 같은 크기지만, 서로를 모르는 두 파일이므로
    //  값이 같은 것에 기대지 않는다 - 어느 쪽이든 그냥 상한으로 눌러 볼 뿐이다.)
    extern const double kNoStockCap;

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

    // ---------- 예산 ----------

    // 값을 안 보고 영양만 풀면 700kcal 한 끼가 11,000원이 되어 나온다.
    // 단백질을 맞추려고 제일 비싼 닭가슴살을 상한까지 밀어 담기 때문이다.
    // 영양은 완벽하고 값은 감당이 안 되는 답인데, 예산을 안 걸면 이걸 거를 길이 없다.
    //
    // 그렇다고 값을 절대 넘지 못하는 벽으로 두지도 않는다.
    // 최소 판매량만 담아도 예산을 넘는 조합이 있고, 그때 답을 안 주는 것보다는
    // "이 조합은 12,000원입니다" 라고 말해 주는 편이 낫다. 열량 밴드와 같은 태도다.
    //
    //   limit  : 넘으면 경고를 띄울 선
    //   weight : 값을 영양과 견주는 저울눈. 예산을 1% 넘길 때 무는 벌점이다.
    //            단백질 부족의 벌점이 1%당 2.0, 지방 부족이 1.0 이므로
    //            기본값 1.0 은 "값은 지방만큼 중요하고 단백질보다는 덜 중요하다" 는 뜻이다.
    //            올리면 싸지고, 너무 올리면 싸고 부실한 답이 나온다.
    struct Budget {
        long long limit  = 0;     // 한 끼에 쓸 돈. 0 이면 예산을 걸지 않은 것
        double    weight = 1.0;   // 0 이면 동점일 때만 싼 쪽을 고른다 (예전 그대로)

        Budget() = default;
        explicit Budget(long long limit);
        Budget(long long limit, double weight);

        bool isSet() const { return limit > 0; }
    };

    // ---------- 어긋난 이유 ----------

    // UI 가 경고 뱃지로 그릴 재료다.
    enum class Issue {
        CaloriesOver,    // 열량 초과
        CaloriesUnder,   // 열량 부족
        ProteinShort,    // 단백질 부족
        FatShort,        // 지방 부족
        PriceOver,       // 예산 초과
        AmountLimited,   // 판매 가능한 양의 한계에 걸려 더 못 맞췄다 (메뉴를 바꿔야 한다)
        StockShort       // 재료가 부족해 더 못 담았다 (오늘 남은 양이 모자란다)
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

    // 오늘 재고까지 보고 고른다. 메뉴판에는 있지만 오늘 재료가 떨어진 것은
    // 빼고 soldOut 에 담는다 (널이면 안 담는다).
    //
    // 여기서 빼는 이유는 solve() 가 매진된 메뉴를 받으면 예외를 던지기 때문이다.
    // 0g 짜리 줄을 화면에 세워 두는 것보다, 애초에 안 세우고 "재료 소진" 이라고
    // 말해 주는 편이 낫다 - 담을 수 없는 것이 담긴 것처럼 보이면 안 된다.
    std::vector<Pick> picksFrom(const WeeklyMenu& week,
                                const std::vector<std::string>& names,
                                const StockLimits& stock,
                                std::vector<std::string>* missing = 0,
                                std::vector<std::string>* soldOut = 0);

    // 이미 만들어 둔 Pick 목록에서 재료가 떨어진 것만 걸러낸다.
    // 재고가 허락하는 양이 유저가 건 하한(minAmount)보다 적어도 빼지 않는다 -
    // 그건 담을 수는 있으나 원하는 만큼은 아닌 경우이고, 울타리를 내려 담은 뒤
    // PlanItem::stockLimited 로 알려 주는 것이 맞다.
    std::vector<Pick> availablePicks(const std::vector<Pick>& picks,
                                     const StockLimits& stock,
                                     std::vector<std::string>* soldOut = 0);

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

        // ---- 재고 ----
        //
        // 재고를 꽂지 않았으면 stockCap 은 아주 큰 값이고 stockLimited 는 거짓이다.
        // 꽂았다면 maxAmount 는 이미 재고 안으로 눌려 있으므로, 양을 조절하는 쪽은
        // 재고를 따로 볼 필요가 없다 - 늘리다 보면 그냥 거기서 멈춘다.
        double stockCap     = 0.0;    // 오늘 재고가 허락하는 최대
        bool   stockLimited = false;  // 재고 때문에 maxAmount 가 내려갔다

        // 재고 때문에 더 못 늘리는 상태인가. UI 가 이 줄에 "재료 부족" 뱃지를 단다.
        // (stockLimited 만으로는 부족하다 - 한계가 재고여도 아직 덜 담았으면
        //  더 담을 수 있으므로 경고할 일이 아니다)
        bool isStockCapped() const { return stockLimited && atMax; }

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

        // ---- 값 ----
        Budget budget;                    // 걸어 둔 예산 (양을 바꿔도 이건 안 변한다)
        long long priceDelta  = 0;        // 양수면 예산 초과. 예산이 없으면 0
        double    pricePct    = 0.0;      // 예산 대비 비율. 예산이 없으면 0

        std::vector<Issue> issues;        // 비어 있으면 범위 안
        double score          = 0.0;      // 낮을수록 좋음

        bool isWithinTolerance() const { return issues.empty(); }
        bool has(Issue issue) const;
        std::size_t itemCount() const { return items.size(); }

        // 예산 안인가. 예산을 안 걸었으면 언제나 참이다.
        bool isWithinBudget() const { return !budget.isSet() || price <= budget.limit; }

        // 오차범위를 못 맞춘 채로 내놓은 답인가.
        // 참이면 UI 는 경고를 띄우되 답을 감추지는 않는다.
        bool isBestEffort() const { return !issues.empty(); }

        // 한 줄 경고 문구. 범위 안이면 빈 문자열.
        std::string warning() const;

        // 한계에 걸린 항목들의 이름. UI 가 "밥을 더 담을 수 없습니다" 로 쓴다.
        std::vector<std::string> limitedMenus() const;

        // 그중 재료가 부족해서 막힌 것들. "제육볶음은 오늘 150g 까지만 남았습니다".
        // limitedMenus() 의 부분집합이다 - 재고가 아니라 메뉴가 파는 범위나
        // 유저가 건 울타리에 막힌 것은 여기 안 들어간다.
        std::vector<std::string> stockLimitedMenus() const;

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
        bool canAddMenu() const;      // 더 넣을 자리가 있는가 (넘겨 넣는 것은 실제로 막힌다)

        // 하나 빼도 확정할 수 있는 가짓수가 남는가.
        // 거짓이어도 빼는 것 자체는 막지 않는다 - "지금 빼면 확정이 잠긴다" 를
        // 유저에게 미리 보여 주기 위한 값이다.
        bool canRemoveMenu() const;

        // 가짓수가 어긋났을 때 화면에 띄울 문구. 괜찮으면 빈 문자열.
        std::string compositionWarning() const;
    };

    // ---------- 다시 읽은 재고가 계획을 어떻게 바꿨나 ----------

    // MealPlanner::refreshStock() 의 결과.
    // 화면에 그대로 띄울 재료다: "제육볶음이 150g 으로 줄었고, 계란말이는 다 나갔습니다".
    struct StockChange {
        std::vector<std::string> shrunk;    // 담아 둔 양이 재고에 맞춰 줄어든 메뉴
        std::vector<std::string> dropped;   // 재료가 다 떨어져 계획에서 빠진 메뉴

        bool changed() const { return !shrunk.empty() || !dropped.empty(); }

        // 한 줄 문구. 아무것도 안 변했으면 빈 문자열.
        std::string message() const;
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

        // ---- 오늘 재고 ----
        //
        // 꽂으면 그때부터 모든 계산이 "오늘 남은 것" 안에서만 이뤄진다.
        // 안 꽂으면 지금까지와 똑같이 돈다 (재고 제한 없음).
        // 수명은 꽂은 쪽이 관리한다 - 이 클래스가 delete 하지 않는다.
        //
        // DailyStock 을 그대로 넘기면 된다. 다른 것을 넘겨도 되고,
        // 그게 이 인터페이스를 domains.h 에 둔 이유다.
        void setStockLimits(const StockLimits* stock) { stock_ = stock; }
        const StockLimits* stockLimits() const { return stock_; }

        // 오늘 이 메뉴를 최대 얼마나 담을 수 있는가.
        // 재고를 안 꽂았으면 아주 큰 값(kNoStockCap)을 준다.
        double stockCapFor(const Menu& menu) const;

        // 오늘 이 메뉴를 담을 수 있는가. 재고를 안 꽂았으면 언제나 참.
        // UI 는 메뉴판을 그릴 때 이걸로 "재료 소진" 버튼을 잠근다 - 눌러 보고
        // 예외를 받는 것이 아니라, 누를 수 없다는 것이 먼저 보여야 한다.
        bool canServe(const Menu& menu) const;

        // 지금 이 계획에 이 메뉴를 더 넣을 수 있는가.
        // 자리(maxPicks), 중복, 재고를 모두 본다. addMenu() 가 던지는 조건과 같다.
        bool canAddMenu(const Plan& plan, const Menu& menu) const;

        // 넣을 수 없는 이유. 넣을 수 있으면 빈 문자열.
        // 버튼 옆에 그대로 띄우면 된다 ("재료가 부족합니다 (제육볶음)").
        std::string addBlockReason(const Plan& plan, const Menu& menu) const;

        // 고른 메뉴들의 양을 목표에 맞춘다.
        // target 은 "앞으로 더 먹어야 하는 양" (Day::remaining()).
        // 어떤 양으로도 오차범위 안에 못 들어가면, 가장 덜 어긋나는 양을 주고
        // issues 를 채워 돌려준다. 빈 결과를 주거나 예외를 던지지 않는다.
        //
        // 예외는 두 가지뿐이다:
        //   - 가짓수가 3..9 가 아니다
        //   - 재고를 꽂았는데 오늘 재료가 떨어진 메뉴가 섞여 있다
        //     (담을 수 없는 것을 0g 짜리 줄로 세워 두지 않는다.
        //      picksFrom(week, names, stock) 이나 availablePicks() 로 먼저 걸러라)
        Plan solve(const std::vector<Pick>& picks, const Macros& target) const;

        // 예산까지 걸고 푼다. 값을 영양과 함께 저울에 올린다.
        // 예산을 안 넘기는 것을 보장하지는 않는다 - 못 맞추면 Issue::PriceOver 로 알린다.
        Plan solve(const std::vector<Pick>& picks, const Macros& target,
                   const Budget& budget) const;

        // 그날 남은 영양분에 맞춘다. 주문 화면이 부르는 쪽.
        Plan solveFor(const std::vector<Pick>& picks, const Day& day) const;
        Plan solveFor(const std::vector<Pick>& picks, const Day& day,
                      const Budget& budget) const;

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
        // 오늘 재료가 떨어진 메뉴면 예외 - 넣기 전에 canAddMenu() 로 물어보면 된다.
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

        // 예산을 걸거나 바꾼다. 이것도 기준만 옮긴다 - 양을 다시 풀지는 않는다.
        // 유저가 "이 예산에 맞춰 다시 짜 줘" 를 누르면 그때 rebalance() 를 부른다.
        void setBudget(Plan& plan, const Budget& budget) const;

        // ---- 재고를 다시 읽는다 ----

        // 재고는 화면을 그리는 동안에도 줄어든다 (다른 손님이 결제한다).
        // 그렇다고 매번 다시 읽지는 않는다 - 조절 범위가 유저 손가락 밑에서
        // 슬금슬금 움직이면, 아까는 담기던 양이 왜 안 담기는지 알 수 없게 된다.
        // 그래서 재고는 계획을 세울 때 한 번 찍어 두고, 다시 읽는 것은 여기서만 한다.
        //
        // 결제를 누르기 직전에 한 번 부르면 된다. changed() 가 참이면
        // "그 사이에 재료가 줄었습니다" 를 띄우고 다시 확인받아야 한다.
        // 재고를 안 꽂았으면 아무 일도 일어나지 않는다.
        StockChange refreshStock(Plan& plan) const;

    private:
        // 메뉴 하나를 항목으로 만든다. 한계/계량 단위/양을 모두 채워 준다.
        // skip 번을 뺀 나머지 항목의 탄단지와 값에 맞춰 새 항목의 양을 잡는다
        // (넣을 때는 Plan::npos(), 갈아끼울 때는 그 자리 번호).
        PlanItem makeItem(const Pick& pick, const Plan& plan, std::size_t skip) const;

        // 재고가 떨어진 메뉴가 섞여 있으면 그 이름을 달아 예외를 던진다.
        void checkStock(const std::vector<Pick>& picks) const;

        Tolerance tolerance_;
        int maxSweeps_ = 12;
        const StockLimits* stock_ = 0;
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
