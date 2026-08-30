#include "view.h"
#include "format.h"
#include <cmath>

namespace domains {
namespace view {

    std::string toString(Gender g) {
        switch (g) {
            case Gender::Male:   return "남성";
            case Gender::Female: return "여성";
            default:             return "기타";
        }
    }

    std::string toString(BmrFormula f) {
        switch (f) {
            case BmrFormula::KatchMcArdle: return "Katch-McArdle";
            case BmrFormula::Blended:      return "Katch-McArdle + Mifflin-St Jeor 혼합";
            default:                       return "Mifflin-St Jeor";
        }
    }

    std::string toString(ActivityLevel l) {
        switch (l) {
            case ActivityLevel::Sedentary:  return "거의 안 움직임";
            case ActivityLevel::Light:      return "주 1~3회 가벼운 운동";
            case ActivityLevel::Moderate:   return "주 3~5회 운동";
            case ActivityLevel::Active:     return "주 6~7회 운동";
            default:                        return "매일 고강도 운동 / 육체노동";
        }
    }

    std::string toString(Divisibility d) {
        switch (d) {
            case Divisibility::Discrete: return "낱개";
            default:                     return "연속";
        }
    }

    // ---------- 메뉴 ----------

    std::string formatNutrient(const Nutrient& n, double menuAmount) {
        return n.name() + " " + roundTo(n.amountFor(menuAmount), 1) + n.unit()
             + " (" + roundTo(n.caloriesFor(menuAmount), 1) + "kcal)";
    }

    std::string formatMenu(const Menu& m, double amount) {
        double a = m.normalize(amount);
        return m.name() + " " + trimZeros(a) + m.unit()
             + " - " + std::to_string(m.priceFor(a)) + "원";
    }

    std::string formatNutrition(const Menu& m, double amount) {
        double a = m.normalize(amount);
        const auto& list = m.nutrients();
        if (list.empty()) return m.name() + " - 영양 정보 없음";

        std::string s = m.name() + " " + trimZeros(a) + m.unit() + " : ";
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (i > 0) s += ", ";
            s += formatNutrient(*list[i], a);
        }
        return s + " / 합계 " + roundTo(m.caloriesFor(a), 1) + "kcal";
    }

    // ---------- 사용자 ----------

    std::string formatUser(const User& u) {
        return u.name() + " (" + std::to_string(u.age()) + "세, " + toString(u.gender())
             + ", " + u.email() + ")";
    }

    std::string formatBody(const User& u) {
        std::string s = trimZeros(u.heightCm()) + "cm / " + trimZeros(u.weightKg()) + "kg"
                      + " / BMI " + roundTo(u.bmi(), 1);

        if (u.hasBodyFat())
            s += " / 체지방률 " + trimZeros(u.bodyFatPercent()) + "%"
               + " (제지방량 " + roundTo(u.leanBodyMassKg(), 1) + "kg)";
        else
            s += " / 체지방률 모름";

        return s + " / " + formatBmr(u);
    }

    std::string formatBmr(const User& u) {
        std::string s = "기초대사량 " + std::to_string(std::llround(u.bmr())) + "kcal"
                      + " (" + toString(u.bmrFormula()) + ")";

        // 성인용 공식이라는 사실을 숨기지 않는다.
        // 이 경우 NutritionGoal::forUser 는 애초에 목표를 만들어 주지 않는다.
        if (!u.isBmrReliable())
            s += " ※ 만 " + std::to_string(kMinAdultAge) + "세 미만에는 적용할 수 없는 공식입니다";

        return s;
    }

    std::string formatActivity(const User& u) {
        return toString(u.activityLevel())
             + " (x" + trimZeros(activityFactor(u.activityLevel())) + ")"
             + " / 하루 소비 " + std::to_string(std::llround(u.tdee())) + "kcal";
    }

    // ---------- 목표 ----------

    std::string formatGoal(const NutritionGoal& g) {
        const Macros& m = g.target();
        return "하루 목표 " + std::to_string(std::llround(g.targetCalories())) + "kcal"
             + " - 탄수화물 " + roundTo(m.carbG, 0) + "g"
             + " / 단백질 "   + roundTo(m.proteinG, 0) + "g"
             + " / 지방 "     + roundTo(m.fatG, 0) + "g";
    }

    std::string formatGoalNotice(const NutritionGoal& g) {
        std::string raw = std::to_string(std::llround(g.rawCalories()));
        std::string now = std::to_string(std::llround(g.targetCalories()));

        switch (g.adjustment()) {
            case GoalAdjustment::RaisedToFloor:
                return "계산값 " + raw + "kcal 이 너무 낮아 하루 최소 권장량인 "
                     + now + "kcal 로 올렸습니다. 입력한 몸 정보를 다시 확인해 주세요.";
            case GoalAdjustment::LoweredToCap:
                return "계산값 " + raw + "kcal 이 너무 높아 " + now
                     + "kcal 로 낮췄습니다. 입력한 몸 정보를 다시 확인해 주세요.";
            default:
                return "";
        }
    }

}
}
