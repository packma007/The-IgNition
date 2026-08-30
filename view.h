#ifndef VIEW
#define VIEW
#include <string>
#include "domains.h"
#include "user.h"

// 화면에 보여줄 문자열을 만드는 곳.
// 도메인(domains / user)은 숫자만 계산하고, 표현은 전부 여기서 담당한다.
// 이 파일은 domains/user를 알지만, 반대 방향 의존은 없다.
namespace domains {
namespace view {

    // ---- 열거형 → 한국어 ----
    std::string toString(Gender g);          // "남성" / "여성" / "기타"
    std::string toString(BmrFormula f);      // "Katch-McArdle" / "Mifflin-St Jeor"
    std::string toString(Divisibility d);    // "낱개" / "연속"

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
    // "기초대사량 1645kcal (Katch-McArdle)"

}
}

#endif
