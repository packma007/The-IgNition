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

    // v2: MEAL 에 출처, v3: 확인 여부, v4: 주간 메뉴판(WEEKMENU/WMENU/WNUTRIENT)
    // v5: USER (몸 정보 + 활동량 + 배달지), v6: USER 에서 골격근량 칸을 뺌
    const int kFormatVersion = 6;

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

        long long toLongLong(const std::string& s, std::size_t lineNo) {
            std::istringstream i(s);
            long long v;
            if (!(i >> v)) fail(lineNo, "정수가 아닙니다: " + s);
            return v;
        }

        // ---- 사용자 ----

        int fromGender(Gender g) {
            switch (g) {
                case Gender::Male:   return 0;
                case Gender::Female: return 1;
                default:             return 2;
            }
        }

        Gender toGender(int v, std::size_t lineNo) {
            switch (v) {
                case 0: return Gender::Male;
                case 1: return Gender::Female;
                case 2: return Gender::Other;
            }
            fail(lineNo, "성별 값이 0..2 가 아닙니다");
            return Gender::Other;   // fail 이 예외를 던지므로 여기까지 오지 않는다
        }

        int fromActivity(ActivityLevel l) {
            switch (l) {
                case ActivityLevel::Sedentary:  return 0;
                case ActivityLevel::Light:      return 1;
                case ActivityLevel::Moderate:   return 2;
                case ActivityLevel::Active:     return 3;
                default:                        return 4;
            }
        }

        ActivityLevel toActivity(int v, std::size_t lineNo) {
            switch (v) {
                case 0: return ActivityLevel::Sedentary;
                case 1: return ActivityLevel::Light;
                case 2: return ActivityLevel::Moderate;
                case 3: return ActivityLevel::Active;
                case 4: return ActivityLevel::VeryActive;
            }
            fail(lineNo, "활동량 값이 0..4 가 아닙니다");
            return ActivityLevel::Light;   // 여기까지 오지 않는다
        }

        // 사용자 한 명을 한 줄로 적는다.
        // 활동량이 여기 들어가는 것이 핵심이다. 이것이 없으면 다시 켰을 때
        // 몸 정보가 바뀌어도 목표를 새로 계산할 방법이 없다.
        void writeUser(const User& u, std::ostream& out) {
            const Location& loc = u.location();
            out << "USER" << kSep << esc(u.name())
                << kSep << u.age()
                << kSep << fromGender(u.gender())
                << kSep << esc(u.email())
                << kSep << num(u.weightKg())
                << kSep << num(u.heightCm())
                << kSep << num(u.bodyFatPercent())
                << kSep << fromActivity(u.activityLevel())
                << kSep << num(loc.latitude)
                << kSep << num(loc.longitude)
                << kSep << esc(loc.address) << "\n";
        }

        // ---- 주간 메뉴판 ----

        int fromDivisibility(Divisibility d) {
            return d == Divisibility::Discrete ? 0 : 1;
        }

        int fromNutrient(const std::string& name) {
            static const std::string carb    = Carbohydrate(0.0).name();
            static const std::string protein = Protein(0.0).name();
            static const std::string fat     = Fat(0.0).name();
            if (name == carb)    return 0;
            if (name == protein) return 1;
            if (name == fat)     return 2;
            return -1;
        }

        NutrientPtr makeNutrient(int code, double perUnit, std::size_t lineNo) {
            switch (code) {
                case 0: return NutrientPtr(new Carbohydrate(perUnit));
                case 1: return NutrientPtr(new Protein(perUnit));
                case 2: return NutrientPtr(new Fat(perUnit));
            }
            fail(lineNo, "영양소 값이 0..2 가 아닙니다");
            return NutrientPtr();   // fail 이 예외를 던지므로 여기까지 오지 않는다
        }

        // 메뉴 하나와 그 영양소를 적는다.
        // 판매 방식마다 지켜야 할 숫자가 다르므로 종류를 먼저 적고 세 칸을 붙인다.
        void writeMenu(const Menu& m, std::ostream& out) {
            double a = 0.0, b = 0.0, c = 0.0;
            if (const DiscreteMenu* d = dynamic_cast<const DiscreteMenu*>(&m)) {
                a = d->minCount();
                b = d->step();
            } else if (const ContinuousMenu* k = dynamic_cast<const ContinuousMenu*>(&m)) {
                a = k->minAmount();
                b = k->maxAmount();
                c = k->step();
            } else {
                // 새로 만든 Menu 파생 클래스는 어떤 숫자를 지켜야 하는지 여기서 알 수 없다.
                // 조용히 기본값으로 적으면 다시 읽었을 때 판매 조건이 달라져 있다.
                throw std::runtime_error("저장할 수 없는 메뉴 종류입니다: " + m.name());
            }

            out << "WMENU" << kSep << esc(m.name()) << kSep << esc(m.unit())
                << kSep << m.unitPrice()
                << kSep << fromDivisibility(m.divisibility())
                << kSep << num(a) << kSep << num(b) << kSep << num(c) << "\n";

            const std::vector<NutrientPtr>& ns = m.nutrients();
            for (std::size_t i = 0; i < ns.size(); ++i) {
                int code = fromNutrient(ns[i]->name());
                if (code < 0)
                    throw std::runtime_error("저장할 수 없는 영양소입니다: "
                                             + m.name() + " 의 " + ns[i]->name());
                out << "WNUTRIENT" << kSep << code
                    << kSep << num(ns[i]->amountPerUnit()) << "\n";
            }
        }

    }

    // ---------- 쓰기 ----------

    void write(const Calendar& calendar, const MenuBook& menus,
               const User* user, std::ostream& out) {
        out << "IGNITION" << kSep << kFormatVersion << "\n";

        // 누구의 기록인지를 맨 앞에 적는다.
        if (user) writeUser(*user, out);

        const DayBoundary& b = calendar.boundary();
        out << "BOUNDARY" << kSep << b.start.hour << kSep << b.start.minute << "\n";

        const Macros& dg = calendar.defaultGoal().target();
        out << "DEFAULTGOAL" << kSep << num(dg.carbG) << kSep
            << num(dg.proteinG) << kSep << num(dg.fatG) << "\n";

        // 메뉴판을 먼저 적는다. 기록보다 메뉴판이 앞서야 사람이 파일을 열어 봤을 때
        // "이번 주에 뭘 팔았고, 그중 무엇을 먹었는지" 순서로 읽힌다.
        const std::map<Date, WeeklyMenu>& weeks = menus.weeks();
        for (std::map<Date, WeeklyMenu>::const_iterator it = weeks.begin();
             it != weeks.end(); ++it) {
            const Date& w = it->first;
            out << "WEEKMENU" << kSep << w.year << kSep << w.month << kSep << w.day << "\n";

            const std::vector<MenuPtr>& list = it->second.menus();
            for (std::size_t i = 0; i < list.size(); ++i)
                writeMenu(*list[i], out);
        }

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

    void read(Calendar& calendar, MenuBook& menus, UserPtr* user, std::istream& in) {
        calendar.clear();
        menus.clear();
        // 앞서 읽어 둔 사람이 남아 있지 않게 비운다.
        // USER 줄이 없는 파일을 읽고 나면 널이어야 한다.
        if (user) user->reset();

        std::string line;
        std::size_t lineNo = 0;
        bool sawHeader = false;
        bool haveDay = false;
        bool haveWeek = false;
        int fileVersion = kFormatVersion;
        Date current;
        Date weekStart;
        MenuPtr currentMenu;

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

            if (tag == "USER") {
                // v5 까지는 체지방률 뒤에 골격근량 칸이 하나 더 있었다.
                // 어디에도 쓰이지 않던 값이라 v6 에서 뺐고, 옛 파일에서는 읽고 버린다.
                std::size_t m = fileVersion >= 6 ? 0 : 1;
                needFields(f, 12 + m, lineNo, "USER");
                try {
                    User u(unesc(f[1]),
                           toInt(f[2], lineNo),
                           toGender(toInt(f[3], lineNo), lineNo),
                           unesc(f[4]),
                           toDouble(f[5], lineNo),
                           toDouble(f[6], lineNo),
                           toDouble(f[7], lineNo),
                           toActivity(toInt(f[8 + m], lineNo), lineNo),
                           Location(toDouble(f[9 + m], lineNo),
                                    toDouble(f[10 + m], lineNo),
                                    unesc(f[11 + m])));
                    // user 가 널이면 줄은 검사하되 결과는 버린다.
                    // 형식이 틀린 줄은 누가 읽든 예외가 되어야 한다.
                    if (user) user->reset(new User(u));
                } catch (const std::runtime_error&) {
                    throw;                       // fail() 이 던진 것은 그대로 내보낸다
                } catch (const std::exception& e) {
                    fail(lineNo, e.what());      // 나머지는 몇 번째 줄인지를 붙여 준다
                }

            } else if (tag == "BOUNDARY") {
                needFields(f, 3, lineNo, "BOUNDARY");
                calendar.setBoundary(DayBoundary(
                    TimeOfDay(toInt(f[1], lineNo), toInt(f[2], lineNo))));

            } else if (tag == "DEFAULTGOAL") {
                needFields(f, 4, lineNo, "DEFAULTGOAL");
                calendar.setDefaultGoal(NutritionGoal(Macros(
                    toDouble(f[1], lineNo),
                    toDouble(f[2], lineNo),
                    toDouble(f[3], lineNo))));

            } else if (tag == "WEEKMENU") {
                needFields(f, 4, lineNo, "WEEKMENU");
                weekStart = Date(toInt(f[1], lineNo),
                                 toInt(f[2], lineNo),
                                 toInt(f[3], lineNo));
                if (weekStart != WeeklyMenu::weekStartOf(weekStart))
                    fail(lineNo, "WEEKMENU 날짜는 그 주의 일요일이어야 합니다");
                haveWeek = true;
                currentMenu = MenuPtr();
                menus.weekOf(weekStart);   // 메뉴가 하나도 없는 주도 그대로 남긴다

            } else if (tag == "WMENU") {
                if (!haveWeek) fail(lineNo, "WEEKMENU 없이 WMENU 가 나왔습니다");
                needFields(f, 8, lineNo, "WMENU");
                std::string mname = unesc(f[1]);
                std::string munit = unesc(f[2]);
                long long price   = toLongLong(f[3], lineNo);
                int kind          = toInt(f[4], lineNo);
                double a = toDouble(f[5], lineNo);
                double b = toDouble(f[6], lineNo);
                double c = toDouble(f[7], lineNo);
                if (kind != 0 && kind != 1)
                    fail(lineNo, "메뉴 종류가 0(낱개)이나 1(무게/부피)가 아닙니다");
                try {
                    currentMenu = kind == 0
                        ? MenuPtr(new DiscreteMenu(mname, munit, price,
                                                   static_cast<int>(a),
                                                   static_cast<int>(b)))
                        : MenuPtr(new ContinuousMenu(mname, munit, price, a, b, c));
                    menus.weekOf(weekStart).add(currentMenu);
                } catch (const std::runtime_error&) {
                    throw;                       // fail() 이 던진 것은 그대로 내보낸다
                } catch (const std::exception& e) {
                    fail(lineNo, e.what());      // 나머지는 몇 번째 줄인지를 붙여 준다
                }

            } else if (tag == "WNUTRIENT") {
                if (!currentMenu) fail(lineNo, "WMENU 없이 WNUTRIENT 가 나왔습니다");
                needFields(f, 3, lineNo, "WNUTRIENT");
                int code   = toInt(f[1], lineNo);
                double per = toDouble(f[2], lineNo);
                try {
                    currentMenu->addNutrient(makeNutrient(code, per, lineNo));
                } catch (const std::runtime_error&) {
                    throw;
                } catch (const std::exception& e) {
                    fail(lineNo, e.what());
                }

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

    bool save(const Calendar& calendar, const MenuBook& menus, const User* user,
              const std::string& path, std::string* error) {
        std::ofstream out(path.c_str());
        if (!out) {
            if (error) *error = "파일을 쓸 수 없습니다: " + path;
            return false;
        }
        try {
            write(calendar, menus, user, out);
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

    bool load(Calendar& calendar, MenuBook& menus, UserPtr* user,
              const std::string& path, std::string* error) {
        std::ifstream in(path.c_str());
        if (!in) {
            if (error) *error = "파일을 열 수 없습니다: " + path;
            return false;
        }
        try {
            read(calendar, menus, user, in);
        } catch (const std::exception& e) {
            if (error) *error = path + " - " + e.what();
            return false;
        }
        return true;
    }

    bool save(const Calendar& calendar, const MenuBook& menus,
              const std::string& path, std::string* error) {
        return save(calendar, menus, 0, path, error);
    }

    bool load(Calendar& calendar, MenuBook& menus,
              const std::string& path, std::string* error) {
        return load(calendar, menus, 0, path, error);
    }

    // ---------- 메뉴판이 필요 없을 때 ----------

    // 쓸 때는 빈 메뉴판을 넣고, 읽을 때는 WEEKMENU 를 읽고 버린다.
    // 버릴지언정 건너뛰지는 않는다 - 형식이 틀린 줄은 여기서도 예외가 되어야 한다.

    void write(const Calendar& calendar, const MenuBook& menus, std::ostream& out) {
        write(calendar, menus, 0, out);
    }

    void read(Calendar& calendar, MenuBook& menus, std::istream& in) {
        read(calendar, menus, 0, in);
    }

    void write(const Calendar& calendar, std::ostream& out) {
        write(calendar, MenuBook(), 0, out);
    }

    void read(Calendar& calendar, std::istream& in) {
        MenuBook discarded;
        read(calendar, discarded, 0, in);
    }

    bool save(const Calendar& calendar, const std::string& path, std::string* error) {
        return save(calendar, MenuBook(), path, error);
    }

    bool load(Calendar& calendar, const std::string& path, std::string* error) {
        MenuBook discarded;
        return load(calendar, discarded, path, error);
    }

}
}
