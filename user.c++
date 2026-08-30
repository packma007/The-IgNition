#include "user.h"
#include <stdexcept>

namespace domains {

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
    }

    // ---------- User ----------

    User::User(std::string name,
               int age,
               Gender gender,
               std::string email,
               double weightKg,
               double heightCm,
               double bodyFatPercent,
               double skeletalMuscleKg,
               Location location)
        : name_(std::move(name)),
          age_(age),
          gender_(gender),
          email_(std::move(email)),
          weightKg_(weightKg),
          heightCm_(heightCm),
          bodyFatPercent_(bodyFatPercent),
          skeletalMuscleKg_(skeletalMuscleKg),
          location_(location) {
        if (name_.empty()) throw std::invalid_argument("name must not be empty");
        if (email_.find('@') == std::string::npos)
            throw std::invalid_argument("email must contain '@'");
        checkAge(age_);
        checkWeight(weightKg_);
        checkHeight(heightCm_);
        setBodyFatPercent(bodyFatPercent_);
        setSkeletalMuscleKg(skeletalMuscleKg_);
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
        if (percent < 0.0 || percent >= 100.0)
            throw std::invalid_argument("bodyFatPercent must be 0..100 (0 = unknown)");
        bodyFatPercent_ = percent;
    }

    void User::setSkeletalMuscleKg(double kg) {
        if (kg < 0.0) throw std::invalid_argument("skeletalMuscleKg must be >= 0");
        if (kg > weightKg_)
            throw std::invalid_argument("skeletalMuscleKg must not exceed weightKg");
        skeletalMuscleKg_ = kg;
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

    BmrFormula User::bmrFormula() const {
        return hasBodyFat() ? BmrFormula::KatchMcArdle : BmrFormula::MifflinStJeor;
    }

    double User::bmr() const {
        if (hasBodyFat()) {
            // Katch-McArdle: 제지방량만 쓰므로 성별에 무관하다
            return 370.0 + 21.6 * leanBodyMassKg();
        }

        // Mifflin-St Jeor: 성별 상수만 다르다
        double base = 10.0 * weightKg_ + 6.25 * heightCm_ - 5.0 * age_;
        switch (gender_) {
            case Gender::Male:   return base + 5.0;
            case Gender::Female: return base - 161.0;
            default:             return base - 78.0;   // 남/여 상수의 중간값
        }
    }

}
