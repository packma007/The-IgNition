#ifndef RECOMMEND
#define RECOMMEND
#include <cstddef>
#include <string>
#include <vector>
#include "datetime.h"
#include "day.h"
#include "domains.h"
#include "intake.h"

// 남은 영양분에 맞는 저녁 조합을 골라 주는 곳.
//
// 화면을 그리거나 입력을 받지 않는다. UI 가 주문 화면을 열 때 suggest() 를 부르면
// 후보 목록이 점수순으로 나오고, 사용자가 하나 고르면 logSuggestion() 으로 기록한다.
//
//   화면 열림 ──► planner.suggest(day.remaining())  ──► Suggestion 목록
//   사용자 선택 ──► logSuggestion(day, 고른것, ...)  ──► Day 에 Meal 로 들어감
//
// 범위를 벗어나는 조합도 목록에 넣는다. 대신 issues 에 이유가 담기므로
// UI 는 그걸 보고 "열량 700kcal 초과" 같은 표시를 붙이면 된다. 막지는 않는다.
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

    // ---------- 후보 ----------

    // 범위를 벗어난 이유. UI 가 뱃지로 그릴 재료다.
    enum class Issue {
        CaloriesOver,    // 열량 초과
        CaloriesUnder,   // 열량 부족
        ProteinShort,    // 단백질 부족
        FatShort         // 지방 부족
    };

    struct SuggestionItem {
        MenuPtr menu;
        double amount = 0.0;      // 판매 가능한 양으로 이미 보정된 값
        Macros macros;            // 그 양만큼의 탄단지
        long long price = 0;
    };

    struct Suggestion {
        std::vector<SuggestionItem> items;
        Macros macros;                    // 조합 전체의 탄단지
        long long price = 0;

        // ---- UI 가 그대로 쓰면 되는 값들 ----
        double targetCalories = 0.0;
        double calories       = 0.0;
        double calorieDelta   = 0.0;      // 양수면 초과
        double caloriePct     = 0.0;      // 목표 대비 비율 (1.0 = 딱 맞음)
        double proteinPct     = 0.0;      // 단백질 목표 대비 비율
        double fatPct         = 0.0;      // 지방 목표 대비 비율

        std::vector<Issue> issues;        // 비어 있으면 범위 안
        double score          = 0.0;      // 낮을수록 좋음

        bool isWithinTolerance() const { return issues.empty(); }
        bool has(Issue issue) const;
        std::size_t itemCount() const { return items.size(); }
    };

    // ---------- 추천기 ----------

    class DinnerPlanner {
    public:
        DinnerPlanner() = default;
        explicit DinnerPlanner(Tolerance tolerance) : tolerance_(tolerance) {}

        // ---- 후보 메뉴 ----
        void addMenu(MenuPtr menu);          // 널이면 예외
        void clearMenus();
        const std::vector<MenuPtr>& menus() const { return menus_; }

        // ---- 설정 ----
        const Tolerance& tolerance() const { return tolerance_; }
        void setTolerance(Tolerance tolerance) { tolerance_ = tolerance; }

        // 한 끼에 몇 가지 메뉴까지 섞을지 (기본 3). 실제로 3가지를 넘겨 시키는 일은 드물고,
        // 제한이 없으면 탐색할 조합이 폭발한다.
        int maxItems() const { return maxItems_; }
        void setMaxItems(int n);

        // 같은 메뉴를 최대 몇 인분까지 (기본 2)
        int maxServingsPerMenu() const { return maxServings_; }
        void setMaxServingsPerMenu(int n);

        // ---- 추천 ----
        // remaining 은 "앞으로 더 먹어야 하는 양" (Day::remaining()).
        // 점수가 낮은 순으로 최대 maxResults 개. 범위를 벗어난 후보도 포함된다.
        std::vector<Suggestion> suggest(const Macros& remaining,
                                        std::size_t maxResults = 5) const;

    private:
        std::vector<MenuPtr> menus_;
        Tolerance tolerance_;
        int maxItems_ = 3;
        int maxServings_ = 2;
    };

    // ---------- UI 가 부르는 편의 함수 ----------

    // 그날 남은 영양분으로 바로 추천한다. 주문 화면을 열 때 부르면 된다.
    std::vector<Suggestion> suggestDinner(const DinnerPlanner& planner,
                                          const Day& day,
                                          std::size_t maxResults = 5);

    // ---------- 확인이 필요한 추정치 ----------

    // 사진에서 뽑은 값은 일단 넣고 나중에 확인받는다. 다만 아무 때나 묻지 않고,
    // 그 답이 화면을 바꿀 만할 때만 묻는다 - 저녁 추천을 여는 순간이 그런 때다.
    //
    // 남은 예산이 900kcal 인데 불확실한 항목이 40kcal 면 추천이 안 바뀐다. 묻지 않는다.
    // 700kcal 짜리면 추천이 완전히 달라진다. 그때 묻는다.
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
    bool shouldAskBeforeSuggesting(const Day& day, double threshold = 0.10);

    // 사용자가 확인해 준 값으로 고친다. 양이 그대로면 macros 를 그대로 넘기면 된다.
    // 고치고 나면 confirmed 가 되어 다시 묻지 않는다.
    void confirmMeal(Day& day, std::size_t mealIndex, const Macros& correctedPerServing);
    void confirmMeal(Day& day, std::size_t mealIndex);   // 값은 그대로, 확인만

    // 사용자가 고른 후보를 그날의 식사로 기록한다. 메뉴 한 가지가 Meal 한 개가 된다.
    // 기록하고 나면 day.remaining() 이 곧바로 갱신된다.
    void logSuggestion(Day& day,
                       const Suggestion& suggestion,
                       MealTime slot = MealTime::Dinner,
                       TimeOfDay clock = TimeOfDay(19, 0));

}

#endif
