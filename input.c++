#include "input.h"
#include <istream>
#include <ostream>
#include <stdexcept>

namespace domains {
namespace input {

    namespace {
        std::string trim(const std::string& s) {
            std::size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) return "";
            std::size_t e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        }
    }

    std::string readLine(std::istream& in, std::ostream& out,
                         const std::string& prompt) {
        for (;;) {
            out << prompt;
            out.flush();

            std::string line;
            if (!std::getline(in, line))
                throw std::runtime_error("입력이 끊겼습니다");

            std::string s = trim(line);
            if (!s.empty()) return s;

            out << "  값을 입력해 주세요.\n";
        }
    }

    long long readLongLong(std::istream& in, std::ostream& out,
                           const std::string& prompt, long long min, long long max) {
        for (;;) {
            std::string s = readLine(in, out, prompt);
            try {
                std::size_t used = 0;
                long long v = std::stoll(s, &used);
                if (used != s.size()) throw std::invalid_argument("trailing");
                if (v < min || v > max) {
                    out << "  " << min << " ~ " << max << " 사이로 입력해 주세요.\n";
                    continue;
                }
                return v;
            } catch (const std::exception&) {
                out << "  정수를 입력해 주세요.\n";
            }
        }
    }

    int readInt(std::istream& in, std::ostream& out,
                const std::string& prompt, int min, int max) {
        return static_cast<int>(readLongLong(in, out, prompt, min, max));
    }

    double readDouble(std::istream& in, std::ostream& out,
                      const std::string& prompt, double min, double max) {
        for (;;) {
            std::string s = readLine(in, out, prompt);
            try {
                std::size_t used = 0;
                double v = std::stod(s, &used);
                if (used != s.size()) throw std::invalid_argument("trailing");
                if (v < min || v > max) {
                    out << "  " << min << " ~ " << max << " 사이로 입력해 주세요.\n";
                    continue;
                }
                return v;
            } catch (const std::exception&) {
                out << "  숫자를 입력해 주세요.\n";
            }
        }
    }

    bool readYesNo(std::istream& in, std::ostream& out, const std::string& prompt) {
        for (;;) {
            std::string s = readLine(in, out, prompt + " (y/n): ");
            if (s == "y" || s == "Y") return true;
            if (s == "n" || s == "N") return false;
            out << "  y 또는 n으로 답해 주세요.\n";
        }
    }

    // ---------- 도메인 객체 ----------

    Gender readGender(std::istream& in, std::ostream& out) {
        int n = readInt(in, out, "성별 (1=남성, 2=여성, 3=기타): ", 1, 3);
        if (n == 1) return Gender::Male;
        if (n == 2) return Gender::Female;
        return Gender::Other;
    }

    ActivityLevel readActivityLevel(std::istream& in, std::ostream& out) {
        out << "운동 얼마나 하세요?\n"
               "  1) 거의 안 함 (앉아서 일하고 따로 운동은 안 해요)\n"
               "  2) 주 1~3회 가볍게\n"
               "  3) 주 3~5회\n"
               "  4) 주 6~7회\n"
               "  5) 매일 고강도로 하거나 몸 쓰는 일을 해요\n";
        int n = readInt(in, out, "번호 (1~5): ", 1, 5);
        switch (n) {
            case 1:  return ActivityLevel::Sedentary;
            case 2:  return ActivityLevel::Light;
            case 3:  return ActivityLevel::Moderate;
            case 4:  return ActivityLevel::Active;
            default: return ActivityLevel::VeryActive;
        }
    }

    User readUser(std::istream& in, std::ostream& out) {
        // 활동량을 맨 앞에서 묻는다.
        // 이름/이메일부터 캐묻는 것보다 "운동 얼마나 하세요" 로 시작하는 편이
        // 이 앱이 무엇을 하려는지 바로 알려 준다.
        ActivityLevel level = readActivityLevel(in, out);

        out << "\n좋아요. 목표 열량을 맞추려면 몇 가지만 더 알면 됩니다.\n";

        std::string name = readLine(in, out, "이름: ");

        // 기초대사량 공식이 성인용이라 그 아래는 목표를 만들 수 없다.
        // 나중에 NutritionGoal::forUser 에서 예외로 터지는 것보다 여기서 막는 편이 낫다.
        int age = readInt(in, out, "나이(만): ", kMinAdultAge, 150);

        Gender gender = readGender(in, out);

        std::string email;
        for (;;) {
            email = readLine(in, out, "이메일: ");
            if (email.find('@') != std::string::npos) break;
            out << "  '@'가 들어간 주소를 입력해 주세요.\n";
        }

        // 0.1kg / 30cm 같은 값을 받아 두면 목표 열량이 몇백 kcal 로 계산된다.
        // 사람에게 있을 수 있는 범위로 좁힌다.
        double weight = readDouble(in, out, "몸무게(kg): ", 20.0, 300.0);
        double height = readDouble(in, out, "키(cm): ", 100.0, 250.0);

        // 0을 넣으면 "모름"으로 저장되고, 기초대사량은 Mifflin-St Jeor 로만 계산된다.
        // 0 과 3~65 사이만 받는다. readDouble 은 이어진 한 구간만 보므로 여기서 따로 본다.
        double bodyFat = 0.0;
        for (;;) {
            bodyFat = readDouble(in, out, "체지방률(%) - 모르면 0: ", 0.0, 100.0);
            if (bodyFat == 0.0) break;
            if (bodyFat >= kMinBodyFatPercent && bodyFat <= kMaxBodyFatPercent) break;
            out << "  체지방률은 " << kMinBodyFatPercent << "~" << kMaxBodyFatPercent
                << "% 사이여야 합니다 (모르면 0).\n";
        }

        return User(name, age, gender, email, weight, height, bodyFat, level);
    }

    MenuPtr readMenu(std::istream& in, std::ostream& out) {
        std::string name = readLine(in, out, "메뉴 이름: ");
        std::string unit = readLine(in, out, "단위 (잔/개/g/ml ...): ");
        long long price  = readLongLong(in, out, "단위 1개당 가격(원): ", 0, 100000000LL);

        int kind = readInt(in, out,
                           "판매 방식 (1=낱개, 2=연속): ", 1, 2);

        MenuPtr menu;
        if (kind == 1) {
            int minCount = readInt(in, out, "최소 주문 수량: ", 0, 1000000);
            int step     = readInt(in, out, "묶음 단위 (1이면 낱개): ", 1, 1000000);
            menu = std::make_shared<DiscreteMenu>(name, unit, price, minCount, step);
        } else {
            double minAmount = readDouble(in, out, "최소 판매량: ", 0.0, 1000000.0);
            double maxAmount = readDouble(in, out,
                                          "최대 판매량 - 제한 없으면 0: ", 0.0, 1000000.0);
            double step      = readDouble(in, out,
                                          "계량 단위 - 제한 없으면 0: ", 0.0, 1000000.0);
            menu = std::make_shared<ContinuousMenu>(name, unit, price,
                                                    minAmount, maxAmount, step);
        }

        readNutrients(in, out, *menu);
        return menu;
    }

    void readNutrients(std::istream& in, std::ostream& out, Menu& menu) {
        out << "-- " << menu.name() << " 1" << menu.unit() << "당 영양소 --\n";

        double carb = readDouble(in, out, "탄수화물(g): ", 0.0, 10000.0);
        if (carb > 0.0) menu.addNutrient<Carbohydrate>(carb);

        double protein = readDouble(in, out, "단백질(g): ", 0.0, 10000.0);
        if (protein > 0.0) menu.addNutrient<Protein>(protein);

        double fat = readDouble(in, out, "지방(g): ", 0.0, 10000.0);
        if (fat > 0.0) menu.addNutrient<Fat>(fat);
    }

}
}
