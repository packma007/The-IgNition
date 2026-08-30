#include "menuseed.h"
#include <stdexcept>

namespace domains {

    namespace {

        // 메뉴표 한 줄.
        // 영양소는 "1단위당" 이다 - g 으로 파는 메뉴는 1g 당, 낱개로 파는 메뉴는 1개당.
        // 헤더의 표는 읽기 쉬우라고 100g 기준으로 적어 두었으므로 여기 값과 100배 차이가 난다.
        struct Spec {
            const char* name;
            const char* unit;
            long long price;      // 1단위당 가격
            bool byCount;         // true = 낱개로 판다
            double a, b, c;       // 낱개: minCount, step, (안 씀)
                                  // 무게: minAmount, maxAmount, step
            double carb, protein, fat;
        };

        const Spec kSpecs[] = {
            // 이름                   단위   가격  낱개   최소   최대  단위   탄      단      지
            // 밥의 최소량을 크게 잡으면 그만큼 열량이 강제로 깔려 솔버가 쓸 여지가 줄어든다.
            // 반찬과 같이 먹는 구성이므로 50g(반 공기의 3분의 1)까지 열어 둔다.
            { "잡곡밥",               "g",     6, false,   50,  400,  10,  0.330, 0.035, 0.006 },
            { "고구마구이",           "g",     8, false,   50,  300,  10,  0.310, 0.016, 0.002 },
            { "감자조림",             "g",     8, false,   50,  250,  10,  0.200, 0.020, 0.030 },
            { "간장닭가슴살구이",     "g",    26, false,   50,  300,  10,  0.035, 0.270, 0.025 },
            { "닭가슴살 카레볶음",    "g",    27, false,   50,  300,  10,  0.060, 0.240, 0.050 },
            { "소고기 장조림",        "g",    45, false,   30,  200,  10,  0.050, 0.210, 0.060 },
            { "두부조림",             "g",    14, false,   50,  250,  10,  0.040, 0.095, 0.065 },
            { "새우 브로콜리 볶음",   "g",    38, false,   50,  250,  10,  0.045, 0.120, 0.040 },
            { "간장 닭다리살 구이",   "g",    30, false,   50,  300,  10,  0.040, 0.190, 0.090 },
            { "제육볶음",             "g",    32, false,   50,  300,  10,  0.070, 0.150, 0.130 },

            // 낱개로 파는 둘. 토막과 줄을 갈라서 팔 수는 없다.
            // 1토막 = 70g, 1줄 = 60g 기준으로 100g 값을 환산해 두었다.
            { "고등어구이",           "토막", 3200, true,    1,    1,   0,  0.000,14.000,10.500 },
            { "계란말이",             "줄",   2400, true,    1,    1,   0,  1.500, 6.600, 6.600 },

            { "삼겹살 김치볶음",      "g",    35, false,   50,  250,  10,  0.040, 0.110, 0.220 },
            { "버섯볶음",             "g",    16, false,   30,  200,  10,  0.050, 0.030, 0.050 },
            { "브로콜리 참깨무침",    "g",    15, false,   30,  200,  10,  0.060, 0.040, 0.045 }
        };

        const std::size_t kSpecCount = sizeof(kSpecs) / sizeof(kSpecs[0]);

        MenuPtr build(const Spec& s) {
            MenuPtr m = s.byCount
                ? MenuPtr(new DiscreteMenu(s.name, s.unit, s.price,
                                           static_cast<int>(s.a),
                                           static_cast<int>(s.b)))
                : MenuPtr(new ContinuousMenu(s.name, s.unit, s.price, s.a, s.b, s.c));

            // 탄단지를 셋 다 넣는다. 0 인 것도 빼지 않는다 -
            // 없는 영양소와 0 인 영양소는 다른 말이다. 고등어에 탄수화물이 없는 것은
            // 우리가 모르는 것이 아니라 정말 0 이다.
            m->addNutrient<Carbohydrate>(s.carb);
            m->addNutrient<Protein>(s.protein);
            m->addNutrient<Fat>(s.fat);
            return m;
        }

    }

    WeeklyMenu mvpWeeklyMenu(const Date& anyDayOfWeek) {
        WeeklyMenu week(anyDayOfWeek);
        for (std::size_t i = 0; i < kSpecCount; ++i)
            week.add(build(kSpecs[i]));

        // 표에 줄이 모자라면 메뉴판이 덜 찬 채로 나간다. 조용히 넘기지 않는다.
        if (!week.isFull())
            throw std::logic_error("MVP 메뉴표가 15가지를 채우지 못했습니다");
        return week;
    }

    WeeklyMenu mvpWeeklyMenu() {
        return mvpWeeklyMenu(Date::today());
    }

    MenuPtr mvpMenu(const std::string& name) {
        for (std::size_t i = 0; i < kSpecCount; ++i)
            if (name == kSpecs[i].name) return build(kSpecs[i]);
        return MenuPtr();
    }

    std::vector<std::string> mvpMenuNames() {
        std::vector<std::string> out;
        out.reserve(kSpecCount);
        for (std::size_t i = 0; i < kSpecCount; ++i)
            out.push_back(kSpecs[i].name);
        return out;
    }

}
