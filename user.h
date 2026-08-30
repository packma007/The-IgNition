#ifndef USER
#define USER
#include <string>
#include <memory>

namespace domains {

    enum class Gender {
        Male,
        Female,
        Other        // 미지정 / 기타
    };

    // 기초대사량을 어떤 공식으로 구했는지
    enum class BmrFormula {
        KatchMcArdle,    // 체지방률을 아는 경우 (더 정확)
        MifflinStJeor    // 체지방률을 모르는 경우 (키/나이/성별 기반)
    };

    // 사용자 한 명
    class User {
    public:
        // 체지방률과 골격근량은 0이면 "모름"으로 취급한다.
        User(std::string name,
             int age,
             Gender gender,
             std::string email,
             double weightKg,
             double heightCm,
             double bodyFatPercent = 0.0,
             double skeletalMuscleKg = 0.0);

        const std::string& name() const { return name_; }
        int age() const { return age_; }
        Gender gender() const { return gender_; }
        const std::string& email() const { return email_; }
        double weightKg() const { return weightKg_; }
        double heightCm() const { return heightCm_; }
        double bodyFatPercent() const { return bodyFatPercent_; }
        double skeletalMuscleKg() const { return skeletalMuscleKg_; }

        // 측정값은 바뀌므로 수정할 수 있게 둔다
        void setAge(int age);
        void setEmail(std::string email);
        void setWeightKg(double weightKg);
        void setHeightCm(double heightCm);
        void setBodyFatPercent(double percent);      // 0이면 모름
        void setSkeletalMuscleKg(double kg);         // 0이면 모름

        bool hasBodyFat() const { return bodyFatPercent_ > 0.0; }
        bool hasSkeletalMuscle() const { return skeletalMuscleKg_ > 0.0; }

        // ---- 조합해서 산출하는 값 ----
        double leanBodyMassKg() const;   // 제지방량 = 몸무게 x (1 - 체지방률/100)
        double bodyFatMassKg() const;    // 체지방량
        double bmi() const;              // 몸무게 / 키(m)^2

        BmrFormula bmrFormula() const;   // 어떤 공식이 쓰이는지
        double bmr() const;              // 기초대사량 (kcal/일)

    private:
        std::string name_;
        int age_;
        Gender gender_;
        std::string email_;
        double weightKg_;
        double heightCm_;
        double bodyFatPercent_;    // 0이면 모름
        double skeletalMuscleKg_;  // 0이면 모름
    };

    using UserPtr = std::shared_ptr<User>;

}

#endif
