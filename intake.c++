#include "intake.h"
#include <cmath>
#include <stdexcept>
#include <utility>

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

    double activityFactor(ActivityLevel level) {
        switch (level) {
            case ActivityLevel::Sedentary:  return 1.200;
            case ActivityLevel::Light:      return 1.375;
            case ActivityLevel::Moderate:   return 1.550;
            case ActivityLevel::Active:     return 1.725;
            case ActivityLevel::VeryActive: return 1.900;
        }
        return 1.375;
    }

    MacroRatio::MacroRatio(double carb, double protein, double fat)
        : carb(carb), protein(protein), fat(fat) {
        if (carb < 0.0 || protein < 0.0 || fat < 0.0)
            throw std::invalid_argument("macro ratio must be >= 0");
        if (std::fabs(carb + protein + fat - 1.0) > 1e-6)
            throw std::invalid_argument("macro ratio must sum to 1.0");
    }

    NutritionGoal::NutritionGoal(Macros target) : target_(target) {
        if (target_.carbG < 0.0 || target_.proteinG < 0.0 || target_.fatG < 0.0)
            throw std::invalid_argument("target macros must be >= 0");
    }

    NutritionGoal NutritionGoal::forUser(const User& user,
                                         ActivityLevel level,
                                         MacroRatio ratio) {
        double kcal = user.bmr() * activityFactor(level);
        return NutritionGoal(Macros(kcal * ratio.carb    / kCarbKcal,
                                    kcal * ratio.protein / kProteinKcal,
                                    kcal * ratio.fat     / kFatKcal));
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
