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
            default:                       return "Mifflin-St Jeor";
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

        if (u.hasSkeletalMuscle())
            s += " / 골격근량 " + trimZeros(u.skeletalMuscleKg()) + "kg";

        return s + " / " + formatBmr(u);
    }

    std::string formatBmr(const User& u) {
        return "기초대사량 " + std::to_string(std::llround(u.bmr())) + "kcal"
             + " (" + toString(u.bmrFormula()) + ")";
    }

}
}
