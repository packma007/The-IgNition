#ifndef USER
#define USER
#include <string>
#include <memory>
#include "location.h"

namespace domains {

    enum class Gender {
        Male,
        Female,
        Other        // 미지정 / 기타
    };

    // 기초대사량을 어떤 공식으로 구했는지
    enum class BmrFormula {
        KatchMcArdle,    // 체지방률을 아는 경우
        MifflinStJeor,   // 체지방률을 모르는 경우 (키/나이/성별 기반)
        Blended          // 둘을 섞은 것 (체지방률을 아는 경우의 실제 기본값)
    };

    // ---------- 활동량 ----------

    // 하루에 얼마나 움직이는가. 기초대사량에 곱해 하루 소비 열량을 만든다.
    //
    // 이 값은 User 가 들고 있다. 예전에는 목표를 만들 때마다 인자로 넘겼는데,
    // 아무도 넘기지 않아 사실상 전원이 Light 로 고정돼 있었다.
    enum class ActivityLevel {
        Sedentary,    // 거의 안 움직임          x1.2
        Light,        // 주 1~3회 가벼운 운동     x1.375
        Moderate,     // 주 3~5회                x1.55
        Active,       // 주 6~7회                x1.725
        VeryActive    // 매일 고강도 / 육체노동   x1.9
    };

    // 기초대사량에 곱하는 계수 (Harris-Benedict 계열의 표준값)
    double activityFactor(ActivityLevel level);

    // 활동량에 따른 단백질 권장량 (제지방 기준 체중 1kg 당 g).
    //
    // 단백질을 "총 열량의 30%" 로 잡으면 활동량이 올라갈 때 단백질도 같이
    // 1.9배가 되는데, 단백질 필요량은 활동량보다 근육량에 붙는 값이라
    // 그렇게까지 오르지 않는다. 그래서 비율이 아니라 g/kg 으로 직접 잡는다.
    double proteinPerKgFor(ActivityLevel level);

    // ---------- 안전 범위 ----------

    // 하루 목표 열량의 하한. 이 아래로는 의학적 감독 없이 권할 수 없다.
    // 입력 오타 하나로 600kcal 짜리 목표가 나오는 것을 막는 마지막 방어선이다.
    double minimumDailyCalories(Gender g);

    // 하루 목표 열량의 상한. 몸무게를 잘못 입력했을 때의 방어선이다.
    extern const double kMaximumDailyCalories;

    // 기초대사량 공식(Mifflin-St Jeor / Katch-McArdle)이 성립하는 최소 나이.
    // 성장기에는 성인용 공식이 맞지 않는다.
    extern const int kMinAdultAge;

    // 체지방률로 받아들일 범위. 0 은 "모름" 이라 따로 통과시킨다.
    extern const double kMinBodyFatPercent;   // 3   (남성 필수 체지방 근처)
    extern const double kMaxBodyFatPercent;   // 65  (그 위는 사실상 입력 오류)

    // 사용자 한 명
    class User {
    public:
        // 체지방률은 0이면 "모름"으로 취급한다.
        User(std::string name,
             int age,
             Gender gender,
             std::string email,
             double weightKg,
             double heightCm,
             double bodyFatPercent = 0.0,
             ActivityLevel activityLevel = ActivityLevel::Light,
             Location location = Location());

        const std::string& name() const { return name_; }
        int age() const { return age_; }
        Gender gender() const { return gender_; }
        const std::string& email() const { return email_; }
        double weightKg() const { return weightKg_; }
        double heightCm() const { return heightCm_; }
        double bodyFatPercent() const { return bodyFatPercent_; }
        ActivityLevel activityLevel() const { return activityLevel_; }

        // 배달할 곳. 넣지 않으면 isSet() 이 false 다.
        const Location& location() const { return location_; }
        void setLocation(Location location) { location_ = location; }
        bool hasLocation() const { return location_.isSet(); }

        // 측정값은 바뀌므로 수정할 수 있게 둔다.
        // 이 값들을 고쳐도 이미 만들어 둔 Calendar 의 목표는 따라오지 않는다.
        // 목표까지 다시 맞추려면 Calendar::refreshDefaultGoal() 을 부른다.
        void setAge(int age);
        void setEmail(std::string email);
        void setWeightKg(double weightKg);
        void setHeightCm(double heightCm);
        void setBodyFatPercent(double percent);      // 0이면 모름, 그 밖에는 3~65
        void setActivityLevel(ActivityLevel level) { activityLevel_ = level; }

        bool hasBodyFat() const { return bodyFatPercent_ > 0.0; }

        // ---- 조합해서 산출하는 값 ----
        double leanBodyMassKg() const;   // 제지방량 = 몸무게 x (1 - 체지방률/100)
        double bodyFatMassKg() const;    // 체지방량
        double bmi() const;              // 몸무게 / 키(m)^2

        // 단백질 권장량을 계산할 때 쓰는 기준 체중.
        //
        // 체지방률을 모르면 그냥 몸무게다.
        // 알면 제지방량을 "체지방 20% 인 몸" 의 체중으로 되돌린 값(LBM/0.8)을 쓴다.
        // 지방은 단백질을 요구하지 않으므로, 체지방이 많을수록 기준이 낮아진다.
        // 체지방이 적어 이 값이 실제 몸무게를 넘으면 몸무게로 자른다.
        double referenceWeightKg() const;

        BmrFormula bmrFormula() const;   // 어떤 공식이 쓰이는지
        double bmr() const;              // 기초대사량 (kcal/일)

        // 특정 공식으로만 계산한 기초대사량. 두 공식이 얼마나 벌어지는지
        // 화면에 보여주거나 검증할 때 쓴다. bmr() 은 이들을 섞은 값이다.
        double bmrBy(BmrFormula formula) const;

        // 하루 소비 열량 = 기초대사량 x 활동계수
        double tdee() const;

        // 성인용 공식의 적용 범위 안에 있는가 (만 kMinAdultAge 세 이상)
        bool isBmrReliable() const { return age_ >= kMinAdultAge; }

    private:
        std::string name_;
        int age_;
        Gender gender_;
        std::string email_;
        double weightKg_;
        double heightCm_;
        double bodyFatPercent_;    // 0이면 모름
        ActivityLevel activityLevel_;
        Location location_;        // 배달지. 안 넣었으면 좌표가 (0,0)
    };

    using UserPtr = std::shared_ptr<User>;

}

#endif
