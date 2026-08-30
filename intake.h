#ifndef INTAKE
#define INTAKE
#include <string>
#include <vector>
#include "domains.h"
#include "datetime.h"
#include "user.h"

// 오늘 먹은 식사를 기록하고, 목표 영양분에서 얼마나 남았는지 계산하는 곳.
//
// 지금은 음식의 탄/단/지를 직접 입력받는다.
// 나중에 "음식 이름 -> 탄단지" 표(FoodDatabase)를 만들어 붙이면
// Meal 을 만드는 방법만 하나 늘어나고 아래 계산은 그대로 쓸 수 있다.
namespace domains {

    // ---------- 탄단지 묶음 ----------

    // 탄수화물/단백질/지방 3종을 한 덩어리로 다룬다.
    // 음수도 허용한다 (목표에서 뺀 결과가 음수면 = 이미 초과 섭취).
    struct Macros {
        double carbG    = 0.0;
        double proteinG = 0.0;
        double fatG     = 0.0;

        Macros() = default;
        Macros(double carbG, double proteinG, double fatG);

        // 탄 x4 + 단 x4 + 지 x9 (kcal 상수는 domains.h 의 영양소 클래스에서 가져온다)
        double calories() const;

        // 음수인 항목을 0으로 깎은 사본. "더 먹어야 하는 양"을 볼 때 쓴다.
        Macros clampedToZero() const;

        bool isEmpty() const;

        Macros& operator+=(const Macros& o);
        Macros& operator-=(const Macros& o);
    };

    Macros operator+(Macros a, const Macros& b);
    Macros operator-(Macros a, const Macros& b);
    Macros operator*(Macros m, double factor);   // 2인분 = macros * 2.0

    // ---------- 이 값이 어디서 왔는가 ----------

    // 영양성분은 값만으로는 부족하다. 우리 메뉴에서 온 정확한 값인지,
    // 공공 DB 에서 온 값인지, AI 가 추정한 값인지가 같이 다녀야 한다.
    // 추정치를 확정값처럼 보여주는 것이 이 앱이 할 수 있는 가장 나쁜 거짓말이다.
    enum class MacroSource {
        OurMenu,     // 우리가 파는 메뉴 - 정확
        Official,    // 공공 영양성분 DB - 신뢰할 만함
        Estimated,   // AI 등이 추정 - 화면에 "추정치" 표시가 필요
        Manual,      // 사용자가 직접 입력
        Unknown      // 출처를 모름 (옛 기록을 읽어들인 경우 등)
    };

    // 화면에 "추정치" 표시가 필요한가
    bool isEstimate(MacroSource source);

    // ---------- 한 끼 ----------

    enum class MealTime {
        Breakfast,   // 아침
        Lunch,       // 점심
        Dinner,      // 저녁
        Snack        // 간식 (정해진 시간대 없음)
    };

    // 시간 순서 비교용 (Snack 은 정해진 시간대가 없으므로 항상 false)
    bool isBefore(MealTime a, MealTime b);
    bool isAfter(MealTime a, MealTime b);

    // 시각을 따로 입력하지 않았을 때 쓰는 대표 시각.
    // 실제로 몇 시에 먹었는지는 Meal::hasExactTime() 으로 구분할 수 있다.
    TimeOfDay defaultTimeOf(MealTime t);

    // 먹은 음식 하나
    class Meal {
    public:
        // 시각을 모르는 경우. clock() 은 그 끼니의 대표 시각을 돌려준다.
        // servings 는 인분 수. macros 는 1인분 기준으로 넣는다.
        Meal(std::string foodName,
             MealTime time,
             Macros perServing,
             double servings = 1.0);

        // 몇 시에 먹었는지 아는 경우
        Meal(std::string foodName,
             MealTime time,
             TimeOfDay clock,
             Macros perServing,
             double servings = 1.0);

        const std::string& foodName() const { return foodName_; }
        MealTime time() const { return time_; }
        const Macros& perServing() const { return perServing_; }
        double servings() const { return servings_; }

        // 먹은 시각. 입력하지 않았으면 끼니의 대표 시각이 나온다.
        const TimeOfDay& clock() const { return clock_; }
        bool hasExactTime() const { return hasExactTime_; }
        void setClock(TimeOfDay clock);

        // 이 영양성분이 어디서 왔는가. 직접 만든 Meal 은 Manual 로 시작한다.
        MacroSource source() const { return source_; }
        void setSource(MacroSource source) { source_ = source; }
        bool isEstimated() const { return isEstimate(source_); }

        // 사람이 눈으로 보고 확인했는가.
        // "어디서 왔는가" 와는 다른 축이다 - 공공 DB 값이라도 양이 추측이면
        // 확인이 필요하고, AI 추정이라도 사용자가 맞다고 하면 확인된 값이다.
        bool isConfirmed() const { return confirmed_; }
        void setConfirmed(bool confirmed) { confirmed_ = confirmed; }

        // 아직 사람 손을 안 탄 추정치인가
        bool needsConfirmation() const { return isEstimate(source_) && !confirmed_; }

        Macros total() const;        // perServing x servings
        double calories() const;     // total().calories()

    private:
        std::string foodName_;
        MealTime time_;
        TimeOfDay clock_;
        bool hasExactTime_;
        MacroSource source_;
        bool confirmed_;
        Macros perServing_;
        double servings_;
    };

    // ---------- 하루치 식사 기록 (저장소) ----------

    class DailyIntake {
    public:
        void add(Meal meal);
        void clear();

        const std::vector<Meal>& meals() const { return meals_; }
        std::size_t size() const { return meals_.size(); }

        Macros total() const;                      // 기록된 전부의 합
        Macros totalAt(MealTime t) const;          // 그 끼니만의 합
        Macros totalUntil(const TimeOfDay& cutoff) const;   // 그 시각까지 먹은 합

        // 그 끼니까지의 누적 합.
        // 같은 끼니 이하로 분류된 것 + 그 끼니의 대표 시각 전에 먹은 것을 더한다.
        // (간식도 시각으로 판단하므로 저녁 이후 간식이 점심 합계에 섞이지 않는다)
        Macros totalUpTo(MealTime t) const;

        double calories() const { return total().calories(); }

    private:
        std::vector<Meal> meals_;
    };

    // ---------- 하루 목표 ----------

    // ActivityLevel / activityFactor 는 user.h 로 옮겼다.
    // 활동량은 목표를 만들 때 넘기는 값이 아니라 사용자가 가진 값이기 때문이다.

    // 목표 열량을 탄/단/지에 나누는 비율. 셋의 합이 1.0 이어야 한다.
    //
    // 이것을 직접 넘기면 옛 방식(비율 고정)으로 계산한다.
    // 넘기지 않으면 단백질을 g 으로 먼저 잡는 기본 방식을 쓴다. 아래 참고.
    struct MacroRatio {
        double carb    = 0.50;
        double protein = 0.30;
        double fat     = 0.20;

        MacroRatio() = default;
        MacroRatio(double carb, double protein, double fat);   // 합이 1이 아니면 예외
    };

    // 계산된 목표 열량이 안전 범위를 벗어나 손을 봤는가
    enum class GoalAdjustment {
        None,           // 계산값 그대로
        RaisedToFloor,  // 하한(minimumDailyCalories)까지 올렸다
        LoweredToCap    // 상한(kMaximumDailyCalories)까지 내렸다
    };

    // 남은 열량을 탄수화물과 지방에 나누는 비율 중 탄수화물 몫.
    // 단백질이 30% 일 때의 옛 50:20 분배와 같은 비율이라 결과가 크게 바뀌지 않는다.
    extern const double kRemainderCarbShare;

    // 단백질이 총 열량에서 차지할 수 있는 최대 몫.
    // 열량 목표가 낮을 때 접시가 단백질로만 차는 것을 막는다.
    extern const double kMaxProteinShare;

    // 오늘 먹어야 하는 양
    class NutritionGoal {
    public:
        // 목표를 g 으로 직접 지정하는 경우.
        // 직접 지정한 값에는 안전 하한을 적용하지 않는다 (의도된 값으로 본다).
        explicit NutritionGoal(Macros target);

        // 사용자 정보에서 뽑는 경우.
        //
        //   목표 열량 = 기초대사량 x 활동계수, 그 뒤 안전 범위로 자름
        //   단백질   = 기준 체중 x 활동량별 g/kg  (열량의 40% 를 넘지 않는 선에서)
        //   나머지 열량을 탄수화물 7 : 지방 3 으로 나눔
        //
        // 단백질을 비율이 아니라 g 으로 먼저 잡는 이유는 proteinPerKgFor() 의
        // 주석에 적어 두었다.
        //
        // 활동량은 user.activityLevel() 에서 온다. level 을 넘기면 그것을 쓰되
        // 사용자 정보는 바꾸지 않는다 ("만약 이만큼 움직인다면" 용도).
        //
        // 만 kMinAdultAge 세 미만이면 예외를 던진다. 성인용 공식으로 뽑은 숫자를
        // 성장기 아이의 목표라고 내놓는 것은 이 앱이 할 수 있는 가장 나쁜 거짓말이다.
        static NutritionGoal forUser(const User& user);
        static NutritionGoal forUser(const User& user, ActivityLevel level);
        static NutritionGoal forUser(const User& user, MacroRatio ratio);
        static NutritionGoal forUser(const User& user, ActivityLevel level,
                                     MacroRatio ratio);

        const Macros& target() const { return target_; }
        double targetCalories() const { return target_.calories(); }

        // 안전 범위 때문에 계산값에서 손을 봤는가.
        // None 이 아니면 화면에 그 사실을 알려 주어야 한다.
        GoalAdjustment adjustment() const { return adjustment_; }
        bool wasAdjusted() const { return adjustment_ != GoalAdjustment::None; }

        // 안전 범위를 적용하기 전의 계산값. 직접 지정한 목표면 targetCalories() 와 같다.
        double rawCalories() const { return rawCalories_; }

        // 목표 - 먹은 양. 음수면 그만큼 초과한 것이다.
        Macros remaining(const Macros& consumed) const;

    private:
        NutritionGoal(Macros target, double rawCalories, GoalAdjustment adjustment);

        Macros target_;
        double rawCalories_;
        GoalAdjustment adjustment_;
    };

    // ---------- 목표 + 기록 + 잔여를 함께 들고 있는 하루 단위 장부 ----------

    // 식사를 넣을 때마다 "먹은 양"과 "더 먹어야 하는 양"을 각각 따로 저장해 둔다.
    class DailyNutrition {
    public:
        explicit DailyNutrition(NutritionGoal goal);

        void addMeal(Meal meal);     // 넣으면 consumed_ / remaining_ 이 같이 갱신된다
        void reset();                // 기록만 비우고 목표는 유지
        void setGoal(NutritionGoal goal);

        const NutritionGoal& goal() const { return goal_; }
        const DailyIntake& intake() const { return intake_; }

        const Macros& consumed() const { return consumed_; }    // 지금까지 먹은 양
        const Macros& remaining() const { return remaining_; }  // 더 먹어야 하는 양 (음수 = 초과)

        // "점심까지 먹었을 때" 처럼 특정 시점 기준으로 다시 계산한 값
        Macros consumedUpTo(MealTime t) const;
        Macros remainingUpTo(MealTime t) const;

        double consumedCalories() const { return consumed_.calories(); }
        double remainingCalories() const { return remaining_.calories(); }
        double achievedRatio() const;    // 목표 열량 대비 달성률 (1.0 = 딱 맞음)
        bool isOverCalories() const;     // 열량을 넘겼는가

    private:
        void recompute();

        NutritionGoal goal_;
        DailyIntake intake_;
        Macros consumed_;
        Macros remaining_;
    };

}

#endif
