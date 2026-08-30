#include "food.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace domains {

    namespace {

        const char kSep = '\t';
        const int kFormatVersion = 1;

        // 이름을 맞춰 보기 좋게 다듬는다.
        // 앞뒤 공백을 떼고, 가운데 공백을 모두 없애고, 영문은 소문자로.
        // "제육 덮밥" 과 "제육덮밥" 을 같은 것으로 보기 위해서다.
        std::string key(const std::string& s) {
            std::string o;
            o.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if (c == ' ' || c == '\t') continue;
                if (c < 128) o += static_cast<char>(std::tolower(c));
                else         o += s[i];
            }
            return o;
        }

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

        void fail(std::size_t lineNo, const std::string& why) {
            std::ostringstream o;
            o << lineNo << "번째 줄: " << why;
            throw std::runtime_error(o.str());
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

        bool contains(const std::string& hay, const std::string& needle) {
            return hay.find(needle) != std::string::npos;
        }

    }

    // ---------- FoodInfo ----------

    FoodInfo::FoodInfo(std::string name, Macros per100g, MacroSource source)
        : name(std::move(name)), per100g(per100g), source(source) {
        if (this->name.empty())
            throw std::invalid_argument("food name must not be empty");
        if (this->per100g.carbG < 0.0 || this->per100g.proteinG < 0.0
            || this->per100g.fatG < 0.0)
            throw std::invalid_argument("per100g must be >= 0");
    }

    Macros FoodInfo::forGrams(double grams) const {
        if (grams < 0.0) throw std::invalid_argument("grams must be >= 0");
        return per100g * (grams / 100.0);
    }

    Macros FoodInfo::forServings(double count) const {
        if (!hasServing())
            throw std::runtime_error(name + " 은 1회 제공량 정보가 없습니다");
        if (count < 0.0) throw std::invalid_argument("count must be >= 0");
        return forGrams(servingGrams * count);
    }

    // 직접 입력한 값은 사람이 친 것이므로 확인된 것으로 본다.
    // 조회해 온 값은 양이 아직 추측일 수 있으므로 확인 전 상태로 둔다.
    Meal FoodInfo::toMeal(MealTime slot, double grams) const {
        Meal m(name, slot, forGrams(grams));
        m.setSource(source);
        m.setConfirmed(source == MacroSource::Manual);
        return m;
    }

    Meal FoodInfo::toMeal(MealTime slot, TimeOfDay clock, double grams) const {
        Meal m(name, slot, clock, forGrams(grams));
        m.setSource(source);
        m.setConfirmed(source == MacroSource::Manual);
        return m;
    }

    // ---------- LocalFoodDatabase ----------

    const FoodInfo* LocalFoodDatabase::find(const std::string& k) const {
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (key(items_[i].name) == k) return &items_[i];
        return 0;
    }

    void LocalFoodDatabase::add(const FoodInfo& info) {
        if (info.name.empty())
            throw std::invalid_argument("food name must not be empty");
        std::string k = key(info.name);
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (key(items_[i].name) == k) { items_[i] = info; return; }
        }
        items_.push_back(info);
    }

    bool LocalFoodDatabase::remove(const std::string& foodName) {
        std::string k = key(foodName);
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (key(items_[i].name) == k) {
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    void LocalFoodDatabase::clear() { items_.clear(); }

    std::vector<std::string> LocalFoodDatabase::names() const {
        std::vector<std::string> out;
        out.reserve(items_.size());
        for (std::size_t i = 0; i < items_.size(); ++i)
            out.push_back(items_[i].name);
        return out;
    }

    std::vector<FoodInfo> LocalFoodDatabase::search(const std::string& part,
                                                    std::size_t limit) const {
        std::vector<FoodInfo> out;
        std::string k = key(part);
        if (k.empty()) return out;
        for (std::size_t i = 0; i < items_.size() && out.size() < limit; ++i)
            if (contains(key(items_[i].name), k)) out.push_back(items_[i]);
        return out;
    }

    bool LocalFoodDatabase::lookup(const std::string& foodName, FoodInfo& out) const {
        const FoodInfo* f = find(key(foodName));
        if (!f) return false;
        out = *f;
        return true;
    }

    void LocalFoodDatabase::write(std::ostream& out) const {
        out << "FOODDB" << kSep << kFormatVersion << "\n";
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const FoodInfo& f = items_[i];
            out << "FOOD" << kSep << esc(f.name)
                << kSep << num(f.per100g.carbG)
                << kSep << num(f.per100g.proteinG)
                << kSep << num(f.per100g.fatG)
                << kSep << num(f.servingGrams)
                << kSep << fromSource(f.source)
                << kSep << num(f.confidence)
                << kSep << esc(f.origin) << "\n";
        }
    }

    void LocalFoodDatabase::read(std::istream& in) {
        items_.clear();

        std::string line;
        std::size_t lineNo = 0;
        bool sawHeader = false;

        while (std::getline(in, line)) {
            ++lineNo;
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);
            if (line.empty()) continue;

            std::vector<std::string> f = split(line);

            if (!sawHeader) {
                if (f[0] != "FOODDB")
                    fail(lineNo, "FOODDB 로 시작하는 파일이 아닙니다");
                if (f.size() < 2) fail(lineNo, "버전이 없습니다");
                int v = toInt(f[1], lineNo);
                if (v != kFormatVersion) {
                    std::ostringstream o;
                    o << "형식 버전 " << v << " 은 읽을 수 없습니다 (이 프로그램은 "
                      << kFormatVersion << ")";
                    fail(lineNo, o.str());
                }
                sawHeader = true;
                continue;
            }

            if (f[0] != "FOOD")
                fail(lineNo, "알 수 없는 레코드입니다: " + f[0]);
            if (f.size() < 9)
                fail(lineNo, "FOOD 레코드는 칸이 9개여야 합니다");

            FoodInfo info(unesc(f[1]),
                          Macros(toDouble(f[2], lineNo),
                                 toDouble(f[3], lineNo),
                                 toDouble(f[4], lineNo)),
                          toSource(toInt(f[6], lineNo)));
            info.servingGrams = toDouble(f[5], lineNo);
            info.confidence   = toDouble(f[7], lineNo);
            info.origin       = unesc(f[8]);
            add(info);
        }

        if (!sawHeader) throw std::runtime_error("빈 파일입니다");
    }

    bool LocalFoodDatabase::save(const std::string& path, std::string* error) const {
        std::ofstream out(path.c_str());
        if (!out) {
            if (error) *error = "파일을 쓸 수 없습니다: " + path;
            return false;
        }
        write(out);
        out.flush();
        if (!out) {
            if (error) *error = "쓰는 도중 실패했습니다: " + path;
            return false;
        }
        return true;
    }

    bool LocalFoodDatabase::load(const std::string& path, std::string* error) {
        std::ifstream in(path.c_str());
        if (!in) {
            if (error) *error = "파일을 열 수 없습니다: " + path;
            return false;
        }
        try {
            read(in);
        } catch (const std::exception& e) {
            if (error) *error = path + " - " + e.what();
            return false;
        }
        return true;
    }

    // ---------- FoodResolver ----------

    void FoodResolver::addSource(const FoodSource* source) {
        if (!source) throw std::invalid_argument("source must not be null");
        sources_.push_back(source);
    }

    void FoodResolver::clearSources() { sources_.clear(); }

    bool FoodResolver::resolve(const std::string& foodName, FoodInfo& out) const {
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            FoodInfo got;
            if (!sources_[i]->lookup(foodName, got)) continue;

            // 어느 소스가 답했는지 비어 있으면 채워 준다
            if (got.origin.empty()) got.origin = sources_[i]->sourceName();

            // 캐시에 넣어 두면 다음부터는 첫 소스에서 바로 나온다.
            // 캐시 자신이 답한 경우는 다시 넣지 않는다.
            if (cache_ && sources_[i] != cache_) cache_->add(got);

            out = got;
            return true;
        }
        return false;
    }

    // ---------- 직접 입력 ----------

    FoodInfo manualEntry(std::string name, Macros per100g, double servingGrams) {
        if (servingGrams < 0.0)
            throw std::invalid_argument("servingGrams must be >= 0");
        FoodInfo f(std::move(name), per100g, MacroSource::Manual);
        f.servingGrams = servingGrams;
        f.origin = "직접 입력";
        return f;
    }

    FoodInfo manualServing(std::string name, Macros perServing, double servingGrams) {
        if (servingGrams <= 0.0)
            throw std::invalid_argument("servingGrams must be > 0");
        // 한 그릇 값을 100g 기준으로 역산해 둔다. 그래야 양을 바꿔도 계산이 된다.
        Macros per100 = perServing * (100.0 / servingGrams);
        FoodInfo f(std::move(name), per100, MacroSource::Manual);
        f.servingGrams = servingGrams;
        f.origin = "직접 입력 (1회 제공량 기준)";
        return f;
    }

}
