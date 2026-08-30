#ifndef VIEW
#define VIEW
#include <string>
#include "domains.h"
#include "user.h"
#include "intake.h"

// 화면에 보여줄 문자열을 만드는 곳.
// 도메인(domains / user)은 숫자만 계산하고, 표현은 전부 여기서 담당한다.
// 이 파일은 domains/user를 알지만, 반대 방향 의존은 없다.
namespace domains {
namespace view {

    // ---- 열거형 → 한국어 ----
    std::string toString(Gender g);          // "남성" / "여성" / "기타"
    std::string toString(BmrFormula f);      // "Katch-McArdle" / "Mifflin-St Jeor" / "혼합"
    std::string toString(Divisibility d);    // "낱개" / "연속"
    std::string toString(ActivityLevel l);   // "주 3~5회 운동"

    // ---- 메뉴 ----
    std::string formatNutrient(const Nutrient& n, double menuAmount);
    // "탄수화물 24g (96kcal)"

    std::string formatMenu(const Menu& m, double amount);
    // "아메리카노 2잔 - 6000원"

    std::string formatNutrition(const Menu& m, double amount);
    // "아메리카노 2잔 : 탄수화물 4g (16kcal), ... / 합계 20.2kcal"

    // ---- 사용자 ----
    std::string formatUser(const User& u);
    // "김창업 (27세, 남성, packma007@gmail.com)"

    std::string formatBody(const User& u);
    // "178cm / 72kg / BMI 22.7 / 체지방률 18% (제지방량 59kg) / ..."

    std::string formatBmr(const User& u);
    // "기초대사량 1674kcal (Katch-McArdle + Mifflin-St Jeor 혼합)"

    std::string formatActivity(const User& u);
    // "주 3~5회 운동 (x1.55) / 하루 소비 2595kcal"

    // ---- 목표 ----

    std::string formatGoal(const NutritionGoal& g);
    // "하루 목표 2595kcal - 탄수화물 351g / 단백질 101g / 지방 67g"

    // 목표가 안전 범위에 걸려 손봐졌으면 그 사실을 알리는 문장, 아니면 빈 문자열.
    // 계산값을 말없이 바꿔 놓고 넘어가면 사용자는 왜 이 숫자가 나왔는지 알 수 없다.
    std::string formatGoalNotice(const NutritionGoal& g);
    // "계산값 1149kcal 이 너무 낮아 하루 최소 권장량인 1200kcal 로 올렸습니다."

}
}

#endif
