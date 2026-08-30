#include "intake.h"
#include <cmath>
#include <stdexcept>
#include <utility>
#include <sstream>
#include <algorithm>

namespace domains {

    namespace {
        // 4 / 4 / 9 를 여기서 다시 적지 않고 domains.h 의 영양소 클래스에서 읽어온다.
        // 열량 상수를 고칠 일이 생기면 domains.h 한 곳만 고치면 된다.
        const double kCarbKcal    = Carbohydrate(0.0).kcalPerGram();
        const double kProteinKcal = Protein(0.0).kcalPerGram();
        const double kFatKcal     = Fat(0.0).kcalPerGram();

        const double kEps = 1e-9;

        int order(MealTime t) {
            switch (t) {
                case MealTime::Breakfast: return 0;
                case MealTime::Lunch:     return 1;
                case MealTime::Dinner:    return 2;
                default:                  return -1;   // Snack: 정해진 시간대 없음
            }
        }
    }

    // ---------- Macros ----------

    Macros::Macros(double carbG, double proteinG, double fatG)
        : carbG(carbG), proteinG(proteinG), fatG(fatG) {}

    double Macros::calories() const {
        return carbG * kCarbKcal + proteinG * kProteinKcal + fatG * kFatKcal;
    }

    Macros Macros::clampedToZero() const {
        return Macros(carbG    > 0.0 ? carbG    : 0.0,
                      proteinG > 0.0 ? proteinG : 0.0,
                      fatG     > 0.0 ? fatG     : 0.0);
    }

    bool Macros::isEmpty() const {
        return std::fabs(carbG) < kEps
            && std::fabs(proteinG) < kEps
            && std::fabs(fatG) < kEps;
    }

    Macros& Macros::operator+=(const Macros& o) {
        carbG    += o.carbG;
        proteinG += o.proteinG;
        fatG     += o.fatG;
        return *this;
    }

    Macros& Macros::operator-=(const Macros& o) {
        carbG    -= o.carbG;
        proteinG -= o.proteinG;
        fatG     -= o.fatG;
        return *this;
    }

    Macros operator+(Macros a, const Macros& b) { a += b; return a; }
    Macros operator-(Macros a, const Macros& b) { a -= b; return a; }

    Macros operator*(Macros m, double factor) {
        m.carbG    *= factor;
        m.proteinG *= factor;
        m.fatG     *= factor;
        return m;
    }

    // ---------- MealTime ----------

    bool isBefore(MealTime a, MealTime b) {
        int oa = order(a), ob = order(b);
        if (oa < 0 || ob < 0) return false;   // 간식은 순서를 따지지 않는다
        return oa < ob;
    }

    bool isEstimate(MacroSource source) {
        return source == MacroSource::Estimated || source == MacroSource::Unknown;
    }

    bool isAfter(MealTime a, MealTime b) {
        return isBefore(b, a);
    }

    TimeOfDay defaultTimeOf(MealTime t) {
        switch (t) {
            case MealTime::Breakfast: return TimeOfDay(8, 0);
            case MealTime::Lunch:     return TimeOfDay(12, 30);
            case MealTime::Dinner:    return TimeOfDay(19, 0);
            default:                  return TimeOfDay(15, 0);   // 간식
        }
    }

    // ---------- Meal ----------

    namespace {
        void checkMeal(const std::string& foodName, const Macros& m, double servings) {
            if (foodName.empty())
                throw std::invalid_argument("foodName must not be empty");
            if (servings <= 0.0)
                throw std::invalid_argument("servings must be > 0");
            if (m.carbG < 0.0 || m.proteinG < 0.0 || m.fatG < 0.0)
                throw std::invalid_argument("macros must be >= 0");
        }
    }

    Meal::Meal(std::string foodName, MealTime time, Macros perServing, double servings)
        : foodName_(std::move(foodName)),
          time_(time),
          clock_(defaultTimeOf(time)),
          hasExactTime_(false),
          source_(MacroSource::Manual),
          confirmed_(false),
          perServing_(perServing),
          servings_(servings) {
        checkMeal(foodName_, perServing_, servings_);
    }

    Meal::Meal(std::string foodName, MealTime time, TimeOfDay clock,
               Macros perServing, double servings)
        : foodName_(std::move(foodName)),
          time_(time),
          clock_(clock),
          hasExactTime_(true),
          source_(MacroSource::Manual),
          confirmed_(false),
          perServing_(perServing),
          servings_(servings) {
        checkMeal(foodName_, perServing_, servings_);
    }

    void Meal::setClock(TimeOfDay clock) {
        clock_ = clock;
        hasExactTime_ = true;
    }

    Macros Meal::total() const {
        return perServing_ * servings_;
    }

    double Meal::calories() const {
        return total().calories();
    }

    // ---------- DailyIntake ----------

    void DailyIntake::add(Meal meal) {
        meals_.push_back(std::move(meal));
    }

    void DailyIntake::clear() {
        meals_.clear();
    }

    Macros DailyIntake::total() const {
        Macros sum;
        for (const Meal& m : meals_) sum += m.total();
        return sum;
    }

    Macros DailyIntake::totalAt(MealTime t) const {
        Macros sum;
        for (const Meal& m : meals_)
            if (m.time() == t) sum += m.total();
        return sum;
    }

    Macros DailyIntake::totalUntil(const TimeOfDay& cutoff) const {
        Macros sum;
        for (const Meal& m : meals_)
            if (m.clock() <= cutoff) sum += m.total();
        return sum;
    }

    Macros DailyIntake::totalUpTo(MealTime t) const {
        TimeOfDay cutoff = defaultTimeOf(t);
        Macros sum;
        for (const Meal& m : meals_) {
            // 같은 끼니 이하로 분류돼 있으면 시각과 무관하게 포함한다
            // (점심을 오후 2시에 먹었어도 "점심까지" 에는 들어가야 한다)
            bool bySlot = order(m.time()) >= 0 && order(m.time()) <= order(t);
            // 그 밖에는 시각으로 판단한다. 간식은 항상 이쪽으로 걸러진다.
            bool byClock = m.clock() <= cutoff;
            if (bySlot || byClock) sum += m.total();
        }
        return sum;
    }

    // ---------- 목표 ----------

    const double kRemainderCarbShare = 0.70;
    const double kMaxProteinShare    = 0.40;

    namespace {
        // 목표 열량을 안전 범위 안으로 자른다.
        double clampToSafeRange(double kcal, Gender g, GoalAdjustment& how) {
            double floorKcal = minimumDailyCalories(g);
            if (kcal < floorKcal) {
                how = GoalAdjustment::RaisedToFloor;
                return floorKcal;
            }
            if (kcal > kMaximumDailyCalories) {
                how = GoalAdjustment::LoweredToCap;
                return kMaximumDailyCalories;
            }
            how = GoalAdjustment::None;
            return kcal;
        }

        // 단백질을 g 으로 먼저 확정한다.
        double proteinGramsFor(const User& user, ActivityLevel level, double kcal) {
            double refKg  = user.referenceWeightKg();
            double wanted = refKg * proteinPerKgFor(level);

            // 열량이 낮을 때 접시가 단백질로만 차지 않게 위를 자르고,
            // 그래도 RDA(0.8 g/kg) 아래로는 내리지 않는다.
            double cap     = kcal * kMaxProteinShare / kProteinKcal;
            double rdaFloor = refKg * 0.8;
            return std::max(rdaFloor, std::min(wanted, cap));
        }

        // 단백질을 뺀 나머지 열량을 탄수화물과 지방으로 나눈다.
        Macros splitCalories(double kcal, double proteinG) {
            double rest = kcal - proteinG * kProteinKcal;
            if (rest < 0.0) rest = 0.0;
            return Macros(rest * kRemainderCarbShare / kCarbKcal,
                          proteinG,
                          rest * (1.0 - kRemainderCarbShare) / kFatKcal);
        }
    }

    MacroRatio::MacroRatio(double carb, double protein, double fat)
        : carb(carb), protein(protein), fat(fat) {
        if (carb < 0.0 || protein < 0.0 || fat < 0.0)
            throw std::invalid_argument("macro ratio must be >= 0");
        if (std::fabs(carb + protein + fat - 1.0) > 1e-6)
            throw std::invalid_argument("macro ratio must sum to 1.0");
    }

    NutritionGoal::NutritionGoal(Macros target)
        : target_(target),
          rawCalories_(target.calories()),
          adjustment_(GoalAdjustment::None) {
        if (target_.carbG < 0.0 || target_.proteinG < 0.0 || target_.fatG < 0.0)
            throw std::invalid_argument("target macros must be >= 0");
    }

    NutritionGoal::NutritionGoal(Macros target, double rawCalories,
                                 GoalAdjustment adjustment)
        : target_(target), rawCalories_(rawCalories), adjustment_(adjustment) {}

    namespace {
        // 성인용 공식의 적용 범위를 벗어난 나이는 여기서 막는다.
        void checkAdultAge(const User& user) {
            if (user.isBmrReliable()) return;
            std::ostringstream o;
            o << "만 " << kMinAdultAge << "세 미만(" << user.age()
              << "세)에는 성인용 기초대사량 공식을 쓸 수 없습니다";
            throw std::invalid_argument(o.str());
        }
    }

    NutritionGoal NutritionGoal::forUser(const User& user) {
        return forUser(user, user.activityLevel());
    }

    NutritionGoal NutritionGoal::forUser(const User& user, MacroRatio ratio) {
        return forUser(user, user.activityLevel(), ratio);
    }

    // 기본 방식: 단백질을 g 으로 먼저 잡고 나머지를 탄/지로 나눈다
    NutritionGoal NutritionGoal::forUser(const User& user, ActivityLevel level) {
        checkAdultAge(user);

        double raw = user.bmr() * activityFactor(level);
        GoalAdjustment how = GoalAdjustment::None;
        double kcal = clampToSafeRange(raw, user.gender(), how);

        return NutritionGoal(splitCalories(kcal, proteinGramsFor(user, level, kcal)),
                             raw, how);
    }

    // 비율을 직접 지정한 경우: 그 비율을 그대로 지킨다
    NutritionGoal NutritionGoal::forUser(const User& user, ActivityLevel level,
                                         MacroRatio ratio) {
        checkAdultAge(user);

        double raw = user.bmr() * activityFactor(level);
        GoalAdjustment how = GoalAdjustment::None;
        double kcal = clampToSafeRange(raw, user.gender(), how);

        return NutritionGoal(Macros(kcal * ratio.carb    / kCarbKcal,
                                    kcal * ratio.protein / kProteinKcal,
                                    kcal * ratio.fat     / kFatKcal),
                             raw, how);
    }

    Macros NutritionGoal::remaining(const Macros& consumed) const {
        return target_ - consumed;
    }

    // ---------- DailyNutrition ----------

    DailyNutrition::DailyNutrition(NutritionGoal goal) : goal_(goal) {
        recompute();
    }

    void DailyNutrition::addMeal(Meal meal) {
        intake_.add(std::move(meal));
        recompute();
    }

    void DailyNutrition::reset() {
        intake_.clear();
        recompute();
    }

    void DailyNutrition::setGoal(NutritionGoal goal) {
        goal_ = goal;
        recompute();
    }

    void DailyNutrition::recompute() {
        consumed_  = intake_.total();
        remaining_ = goal_.remaining(consumed_);
    }

    Macros DailyNutrition::consumedUpTo(MealTime t) const {
        return intake_.totalUpTo(t);
    }

    Macros DailyNutrition::remainingUpTo(MealTime t) const {
        return goal_.remaining(intake_.totalUpTo(t));
    }

    double DailyNutrition::achievedRatio() const {
        double target = goal_.targetCalories();
        if (target <= 0.0) return 0.0;
        return consumed_.calories() / target;
    }

    bool DailyNutrition::isOverCalories() const {
        return remaining_.calories() < -kEps;
    }

}
