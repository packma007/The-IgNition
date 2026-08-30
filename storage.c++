#include "storage.h"
#include <fstream>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace domains {
namespace storage {

    const int kFormatVersion = 3;   // v2: MEAL 에 출처, v3: 확인 여부 칸이 붙었다

    namespace {

        const char kSep = '\t';

        // 이름이나 경로에 탭/줄바꿈이 들어와도 한 줄이 깨지지 않게 감싼다
        std::string esc(const std::string& s) {
            std::string o;
            o.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                char c = s[i];
                switch (c) {
                    case '\\': o += "\\\\"; break;
                    case '\t': o += "\\t";  break;
                    case '\n': o += "\\n";  break;
                    case '\r': o += "\\r";  break;
                    default:   o += c;      break;
                }
            }
            return o;
        }

        std::string unesc(const std::string& s) {
            std::string o;
            o.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
                char n = s[++i];
                switch (n) {
                    case '\\': o += '\\'; break;
                    case 't':  o += '\t'; break;
                    case 'n':  o += '\n'; break;
                    case 'r':  o += '\r'; break;
                    default:   o += n;    break;
                }
            }
            return o;
        }

        // 소수점 아래를 잃지 않을 만큼 충분한 자리수로 적는다 (읽으면 같은 값이 나온다)
        std::string num(double v) {
            std::ostringstream o;
            o << std::setprecision(17) << v;
            return o.str();
        }

        std::vector<std::string> split(const std::string& line) {
            std::vector<std::string> out;
            std::string cur;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (line[i] == kSep) { out.push_back(cur); cur.clear(); }
                else                 { cur += line[i]; }
            }
            out.push_back(cur);
            return out;
        }

        // ---- 읽기 도우미: 틀린 줄이 어디인지 알려 준다 ----

        void fail(std::size_t lineNo, const std::string& why) {
            std::ostringstream o;
            o << lineNo << "번째 줄: " << why;
            throw std::runtime_error(o.str());
        }

        void needFields(const std::vector<std::string>& f, std::size_t n,
                        std::size_t lineNo, const std::string& tag) {
            if (f.size() < n) {
                std::ostringstream o;
                o << tag << " 레코드는 칸이 " << n << "개여야 하는데 "
                  << f.size() << "개뿐입니다";
                fail(lineNo, o.str());
            }
        }

        double toDouble(const std::string& s, std::size_t lineNo) {
            std::istringstream i(s);
            double v;
            if (!(i >> v)) fail(lineNo, "숫자가 아닙니다: " + s);
            return v;
        }

        int toInt(const std::string& s, std::size_t lineNo) {
            std::istringstream i(s);
            int v;
            if (!(i >> v)) fail(lineNo, "정수가 아닙니다: " + s);
            return v;
        }

        MealTime toMealTime(int v, std::size_t lineNo) {
            switch (v) {
                case 0: return MealTime::Breakfast;
                case 1: return MealTime::Lunch;
                case 2: return MealTime::Dinner;
                case 3: return MealTime::Snack;
            }
            fail(lineNo, "끼니 값이 0..3 이 아닙니다");
            return MealTime::Snack;   // fail 이 예외를 던지므로 여기까지 오지 않는다
        }

        int fromSource(MacroSource s) {
            switch (s) {
                case MacroSource::OurMenu:   return 0;
                case MacroSource::Official:  return 1;
                case MacroSource::Estimated: return 2;
                case MacroSource::Manual:    return 3;
                default:                     return 4;
            }
        }

        MacroSource toSource(int v) {
            switch (v) {
                case 0: return MacroSource::OurMenu;
                case 1: return MacroSource::Official;
                case 2: return MacroSource::Estimated;
                case 3: return MacroSource::Manual;
                default: return MacroSource::Unknown;
            }
        }

        int fromMealTime(MealTime t) {
            switch (t) {
                case MealTime::Breakfast: return 0;
                case MealTime::Lunch:     return 1;
                case MealTime::Dinner:    return 2;
                default:                  return 3;
            }
        }

    }

    // ---------- 쓰기 ----------

    void write(const Calendar& calendar, std::ostream& out) {
        out << "IGNITION" << kSep << kFormatVersion << "\n";

        const DayBoundary& b = calendar.boundary();
        out << "BOUNDARY" << kSep << b.start.hour << kSep << b.start.minute << "\n";

        const Macros& dg = calendar.defaultGoal().target();
        out << "DEFAULTGOAL" << kSep << num(dg.carbG) << kSep
            << num(dg.proteinG) << kSep << num(dg.fatG) << "\n";

        const std::map<Date, Day>& days = calendar.days();
        for (std::map<Date, Day>::const_iterator it = days.begin();
             it != days.end(); ++it) {
            const Date& d = it->first;
            const Day& day = it->second;
            const Macros& g = day.goal().target();

            out << "DAY" << kSep << d.year << kSep << d.month << kSep << d.day
                << kSep << num(g.carbG) << kSep << num(g.proteinG)
                << kSep << num(g.fatG) << "\n";

            const std::vector<Meal>& meals = day.meals();
            for (std::size_t i = 0; i < meals.size(); ++i) {
                const Meal& m = meals[i];
                const Macros& mm = m.perServing();
                out << "MEAL" << kSep << esc(m.foodName())
                    << kSep << fromMealTime(m.time())
                    << kSep << m.clock().hour << kSep << m.clock().minute
                    << kSep << (m.hasExactTime() ? 1 : 0)
                    << kSep << num(mm.carbG) << kSep << num(mm.proteinG)
                    << kSep << num(mm.fatG)
                    << kSep << num(m.servings())
                    << kSep << fromSource(m.source())
                    << kSep << (m.isConfirmed() ? 1 : 0) << "\n";
            }

            const std::vector<Photo>& photos = day.photos();
            for (std::size_t i = 0; i < photos.size(); ++i) {
                const Photo& p = photos[i];
                out << "PHOTO" << kSep << esc(p.path())
                    << kSep << p.date().year << kSep << p.date().month
                    << kSep << p.date().day
                    << kSep << p.time().hour << kSep << p.time().minute
                    << kSep << esc(p.note()) << "\n";
            }
        }
    }

    // ---------- 읽기 ----------

    void read(Calendar& calendar, std::istream& in) {
        calendar.clear();

        std::string line;
        std::size_t lineNo = 0;
        bool sawHeader = false;
        bool haveDay = false;
        int fileVersion = kFormatVersion;
        Date current;

        while (std::getline(in, line)) {
            ++lineNo;
            // 윈도우에서 쓴 파일을 다른 데서 읽을 때 남는 \r 을 떼어낸다
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);
            if (line.empty()) continue;

            std::vector<std::string> f = split(line);
            const std::string tag = f[0];

            if (!sawHeader) {
                if (tag != "IGNITION")
                    fail(lineNo, "IGNITION 으로 시작하는 파일이 아닙니다");
                needFields(f, 2, lineNo, "IGNITION");
                fileVersion = toInt(f[1], lineNo);
                // v1 은 MEAL 에 출처 칸이 없다. 옛 기록도 계속 읽을 수 있어야 한다.
                if (fileVersion < 1 || fileVersion > kFormatVersion) {
                    std::ostringstream o;
                    o << "형식 버전 " << fileVersion << " 은 읽을 수 없습니다 (이 프로그램은 "
                      << kFormatVersion << " 까지)";
                    fail(lineNo, o.str());
                }
                sawHeader = true;
                continue;
            }

            if (tag == "BOUNDARY") {
                needFields(f, 3, lineNo, "BOUNDARY");
                calendar.setBoundary(DayBoundary(
                    TimeOfDay(toInt(f[1], lineNo), toInt(f[2], lineNo))));

            } else if (tag == "DEFAULTGOAL") {
                needFields(f, 4, lineNo, "DEFAULTGOAL");
                calendar.setDefaultGoal(NutritionGoal(Macros(
                    toDouble(f[1], lineNo),
                    toDouble(f[2], lineNo),
                    toDouble(f[3], lineNo))));

            } else if (tag == "DAY") {
                needFields(f, 7, lineNo, "DAY");
                current = Date(toInt(f[1], lineNo),
                               toInt(f[2], lineNo),
                               toInt(f[3], lineNo));
                haveDay = true;
                // 저장된 날짜에 그대로 넣는다. 하루 경계는 저장할 때 이미 적용됐으므로
                // 여기서 Calendar::addMeal 을 쓰면 새벽 끼니가 하루씩 더 밀린다.
                calendar.day(current).setGoal(NutritionGoal(Macros(
                    toDouble(f[4], lineNo),
                    toDouble(f[5], lineNo),
                    toDouble(f[6], lineNo))));

            } else if (tag == "MEAL") {
                if (!haveDay) fail(lineNo, "DAY 없이 MEAL 이 나왔습니다");
                needFields(f, 10, lineNo, "MEAL");
                std::string name = unesc(f[1]);
                MealTime slot    = toMealTime(toInt(f[2], lineNo), lineNo);
                TimeOfDay clock(toInt(f[3], lineNo), toInt(f[4], lineNo));
                bool exact       = toInt(f[5], lineNo) != 0;
                Macros per(toDouble(f[6], lineNo),
                           toDouble(f[7], lineNo),
                           toDouble(f[8], lineNo));
                double servings  = toDouble(f[9], lineNo);

                // v1 파일에는 출처 칸이 없다 -> Unknown 으로 둔다.
                // 지어내지 않고 "모른다" 고 남기는 편이 정직하다.
                MacroSource src = MacroSource::Unknown;
                bool confirmed = false;
                if (fileVersion >= 2) {
                    needFields(f, 11, lineNo, "MEAL");
                    src = toSource(toInt(f[10], lineNo));
                }
                if (fileVersion >= 3) {
                    needFields(f, 12, lineNo, "MEAL");
                    confirmed = toInt(f[11], lineNo) != 0;
                } else {
                    // v2 이하에는 확인 여부가 없다. 추정치가 아닌 것만 확인된 것으로 본다.
                    confirmed = !isEstimate(src);
                }

                Meal meal = exact ? Meal(name, slot, clock, per, servings)
                                  : Meal(name, slot, per, servings);
                meal.setSource(src);
                meal.setConfirmed(confirmed);
                calendar.day(current).addMeal(meal);

            } else if (tag == "PHOTO") {
                if (!haveDay) fail(lineNo, "DAY 없이 PHOTO 가 나왔습니다");
                needFields(f, 8, lineNo, "PHOTO");
                Photo p(unesc(f[1]),
                        Date(toInt(f[2], lineNo), toInt(f[3], lineNo),
                             toInt(f[4], lineNo)),
                        TimeOfDay(toInt(f[5], lineNo), toInt(f[6], lineNo)));
                p.setNote(unesc(f[7]));
                calendar.day(current).addPhoto(p);

            } else {
                fail(lineNo, "알 수 없는 레코드입니다: " + tag);
            }
        }

        if (!sawHeader) throw std::runtime_error("빈 파일입니다");
    }

    // ---------- 파일 ----------

    bool save(const Calendar& calendar, const std::string& path,
              std::string* error) {
        std::ofstream out(path.c_str());
        if (!out) {
            if (error) *error = "파일을 쓸 수 없습니다: " + path;
            return false;
        }
        try {
            write(calendar, out);
        } catch (const std::exception& e) {
            if (error) *error = e.what();
            return false;
        }
        out.flush();
        if (!out) {
            if (error) *error = "쓰는 도중 실패했습니다: " + path;
            return false;
        }
        return true;
    }

    bool load(Calendar& calendar, const std::string& path,
              std::string* error) {
        std::ifstream in(path.c_str());
        if (!in) {
            if (error) *error = "파일을 열 수 없습니다: " + path;
            return false;
        }
        try {
            read(calendar, in);
        } catch (const std::exception& e) {
            if (error) *error = path + " - " + e.what();
            return false;
        }
        return true;
    }

}
}
