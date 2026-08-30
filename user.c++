#include "user.h"
#include <stdexcept>
#include <algorithm>

namespace domains {

    const double kMaximumDailyCalories = 6000.0;
    const int    kMinAdultAge          = 18;
    const double kMinBodyFatPercent    = 3.0;
    const double kMaxBodyFatPercent    = 65.0;

    namespace {
        void checkWeight(double w) {
            if (w <= 0.0) throw std::invalid_argument("weightKg must be > 0");
        }
        void checkHeight(double h) {
            if (h <= 0.0) throw std::invalid_argument("heightCm must be > 0");
        }
        void checkAge(int a) {
            if (a < 0 || a > 150) throw std::invalid_argument("age must be 0..150");
        }

        // 체지방률을 알 때 Katch-McArdle 에 주는 무게.
        //
        // 왜 1.0 이 아닌가: 가정용 체성분계의 오차는 ±5%p 수준인데,
        // Katch-McArdle 을 그대로 100% 믿으면 체지방률을 입력하는 순간
        // 하루 목표가 250kcal 넘게 튄다 (70kg/175cm/25세 남, 체지방 25% 기준).
        // 몸무게는 그대로인데 목표만 움직이는 것은 사용자가 납득할 수 없다.
        // 반씩 섞으면 그 점프가 절반으로 줄고, 체지방률이 정확할수록
        // 두 공식이 어차피 가까워지므로 손해도 거의 없다.
        const double kKatchWeight = 0.5;
    }

    // ---------- 활동량 ----------

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

    // 활동량별 단백질 권장량 (기준 체중 1kg 당 g).
    // 앉아 지내는 사람의 1.0 부터 매일 훈련하는 사람의 1.8 까지.
    // 권장 상한(약 2.2 g/kg)을 넘지 않는 범위 안에서만 움직인다.
    double proteinPerKgFor(ActivityLevel level) {
        switch (level) {
            case ActivityLevel::Sedentary:  return 1.0;
            case ActivityLevel::Light:      return 1.2;
            case ActivityLevel::Moderate:   return 1.4;
            case ActivityLevel::Active:     return 1.6;
            case ActivityLevel::VeryActive: return 1.8;
        }
        return 1.2;
    }

    // ---------- 안전 범위 ----------

    double minimumDailyCalories(Gender g) {
        // 통상 쓰이는 하한. 성별을 밝히지 않은 경우는 낮은 쪽에 맞춘다.
        return g == Gender::Male ? 1500.0 : 1200.0;
    }

    // ---------- User ----------

    User::User(std::string name,
               int age,
               Gender gender,
               std::string email,
               double weightKg,
               double heightCm,
               double bodyFatPercent,
               ActivityLevel activityLevel,
               Location location)
        : name_(std::move(name)),
          age_(age),
          gender_(gender),
          email_(std::move(email)),
          weightKg_(weightKg),
          heightCm_(heightCm),
          bodyFatPercent_(bodyFatPercent),
          activityLevel_(activityLevel),
          location_(location) {
        if (name_.empty()) throw std::invalid_argument("name must not be empty");
        if (email_.find('@') == std::string::npos)
            throw std::invalid_argument("email must contain '@'");
        checkAge(age_);
        checkWeight(weightKg_);
        checkHeight(heightCm_);
        setBodyFatPercent(bodyFatPercent_);
    }

    void User::setAge(int age) {
        checkAge(age);
        age_ = age;
    }

    void User::setEmail(std::string email) {
        if (email.find('@') == std::string::npos)
            throw std::invalid_argument("email must contain '@'");
        email_ = std::move(email);
    }

    void User::setWeightKg(double weightKg) {
        checkWeight(weightKg);
        weightKg_ = weightKg;
    }

    void User::setHeightCm(double heightCm) {
        checkHeight(heightCm);
        heightCm_ = heightCm;
    }

    void User::setBodyFatPercent(double percent) {
        // 0 은 "모름". 그 밖에는 사람에게 있을 수 있는 범위여야 한다.
        // 19 를 91 로 잘못 친 것을 여기서 잡지 못하면
        // 제지방량이 6kg 으로 계산돼 600kcal 짜리 목표가 만들어진다.
        if (percent == 0.0) {
            bodyFatPercent_ = 0.0;
            return;
        }
        if (percent < kMinBodyFatPercent || percent > kMaxBodyFatPercent)
            throw std::invalid_argument("bodyFatPercent must be 0 (unknown) or 3..65");
        bodyFatPercent_ = percent;
    }

    // ---------- 산출값 ----------

    double User::bodyFatMassKg() const {
        if (!hasBodyFat()) return 0.0;
        return weightKg_ * bodyFatPercent_ / 100.0;
    }

    double User::leanBodyMassKg() const {
        if (!hasBodyFat()) return 0.0;   // 체지방률을 모르면 계산 불가
        return weightKg_ - bodyFatMassKg();
    }

    double User::bmi() const {
        double m = heightCm_ / 100.0;
        return weightKg_ / (m * m);
    }

    double User::referenceWeightKg() const {
        if (!hasBodyFat()) return weightKg_;
        return std::min(weightKg_, leanBodyMassKg() / 0.80);
    }

    BmrFormula User::bmrFormula() const {
        return hasBodyFat() ? BmrFormula::Blended : BmrFormula::MifflinStJeor;
    }

    double User::bmrBy(BmrFormula formula) const {
        switch (formula) {
            case BmrFormula::KatchMcArdle: {
                // 제지방량만 쓰므로 성별에 무관하다.
                // 체지방률을 모르면 제지방량이 0 이라 쓸 수 없다.
                if (!hasBodyFat())
                    throw std::logic_error("Katch-McArdle needs bodyFatPercent");
                return 370.0 + 21.6 * leanBodyMassKg();
            }
            case BmrFormula::MifflinStJeor: {
                // 성별 상수만 다르다
                double base = 10.0 * weightKg_ + 6.25 * heightCm_ - 5.0 * age_;
                switch (gender_) {
                    case Gender::Male:   return base + 5.0;
                    case Gender::Female: return base - 161.0;
                    default:             return base - 78.0;   // 남/여 상수의 중간값
                }
            }
            default:
                return bmr();
        }
    }

    double User::bmr() const {
        double mifflin = bmrBy(BmrFormula::MifflinStJeor);
        if (!hasBodyFat()) return mifflin;

        double katch = bmrBy(BmrFormula::KatchMcArdle);
        return kKatchWeight * katch + (1.0 - kKatchWeight) * mifflin;
    }

    double User::tdee() const {
        return bmr() * activityFactor(activityLevel_);
    }

}
