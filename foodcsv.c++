#include "foodcsv.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <istream>
#include <stdexcept>
#include <sstream>

namespace domains {

    namespace {

        // ---------- 잔손 ----------

        std::string trim(const std::string& s) {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
            return s.substr(a, b - a);
        }

        std::string lower(const std::string& s) {
            std::string o;
            o.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                o += (c < 128) ? static_cast<char>(std::tolower(c)) : s[i];
            }
            return o;
        }

        // 머리줄 칸 이름을 맞춰 보기 좋게. "탄수화물(g)" 과 "탄수화물 (g)" 을 같게 본다.
        std::string headerKey(const std::string& s) {
            std::string o;
            for (std::size_t i = 0; i < s.size(); ++i) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if (c == ' ' || c == '\t' || c == '_' || c == '-'
                    || c == '(' || c == ')' || c == '[' || c == ']') continue;
                o += (c < 128) ? static_cast<char>(std::tolower(c)) : s[i];
            }
            return o;
        }

        std::size_t npos() { return std::string::npos; }

        // 칸 이름 후보로 칸 번호를 찾는다. 정확히 같은 것을 먼저 보고,
        // 없으면 앞이 같은 것을 본다 ("단백질" 로 "단백질g" 를 찾는다).
        std::size_t findColumn(const std::vector<std::string>& headers,
                               const std::vector<std::string>& aliases) {
            for (std::size_t a = 0; a < aliases.size(); ++a) {
                std::string want = headerKey(aliases[a]);
                if (want.empty()) continue;
                for (std::size_t i = 0; i < headers.size(); ++i)
                    if (headers[i] == want) return i;
            }
            for (std::size_t a = 0; a < aliases.size(); ++a) {
                std::string want = headerKey(aliases[a]);
                if (want.empty()) continue;
                for (std::size_t i = 0; i < headers.size(); ++i)
                    if (headers[i].size() > want.size()
                        && headers[i].compare(0, want.size(), want) == 0) return i;
            }
            return npos();
        }

        std::string cellAt(const std::vector<std::string>& cells, std::size_t i) {
            if (i == npos() || i >= cells.size()) return std::string();
            return trim(cells[i]);
        }

        // 표 파일이 UTF-8 인가. 엑셀이 기본으로 뱉는 CP949 파일이면 여기서 걸린다.
        bool isUtf8(const std::string& s) {
            std::size_t i = 0;
            while (i < s.size()) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                std::size_t extra = 0;
                if (c < 0x80)                  extra = 0;
                else if ((c & 0xE0) == 0xC0)   extra = 1;
                else if ((c & 0xF0) == 0xE0)   extra = 2;
                else if ((c & 0xF8) == 0xF0)   extra = 3;
                else return false;
                if (i + extra >= s.size()) return false;
                for (std::size_t k = 1; k <= extra; ++k)
                    if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
                i += extra + 1;
            }
            return true;
        }

        bool hasVolumeUnit(const std::string& s) {
            std::string k = lower(s);
            return k.find("ml") != npos() || k.find("리터") != npos()
                || k.find("\xEF\xBD\x8D\xEF\xBD\x8C") != npos();   // 전각 ｍｌ
        }

        // 숫자 하나와 그 뒤의 단위를 읽어 그램으로. 단위가 없거나 모르면 다음 숫자로 넘어간다.
        double scanGrams(const std::string& s) {
            std::size_t i = 0;
            while (i < s.size()) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if (!std::isdigit(c)) { ++i; continue; }

                std::size_t start = i;
                while (i < s.size()
                       && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'
                           || s[i] == ',')) ++i;

                std::string numText;
                for (std::size_t k = start; k < i; ++k)
                    if (s[k] != ',') numText += s[k];

                std::istringstream in(numText);
                double v = 0.0;
                if (!(in >> v)) continue;

                std::size_t u = i;
                while (u < s.size() && s[u] == ' ') ++u;
                std::string unit;
                while (u < s.size() && std::isalpha(static_cast<unsigned char>(s[u])))
                    unit += static_cast<char>(std::tolower(static_cast<unsigned char>(s[u++])));

                if (unit == "g")                     return v;
                if (unit == "kg")                    return v * 1000.0;
                if (unit == "ml" || unit == "cc")    return v;          // 1mL 을 1g 으로 본다
                if (unit == "l")                     return v * 1000.0;
                // 단위가 없거나("1회분") 모르는 단위면("1개") 이 숫자는 무게가 아니다
            }
            return 0.0;
        }

    }

    // ---------- 칸 이름 기본값 ----------

    CsvColumns CsvColumns::mfds() {
        CsvColumns c;
        c.name.push_back("식품명");
        c.name.push_back("식품이름");
        c.name.push_back("식품명(국문)");
        c.name.push_back("name");
        c.name.push_back("food name");

        c.carb.push_back("탄수화물(g)");
        c.carb.push_back("탄수화물");
        c.carb.push_back("carbohydrate(g)");
        c.carb.push_back("carbohydrate");

        c.protein.push_back("단백질(g)");
        c.protein.push_back("단백질");
        c.protein.push_back("protein(g)");
        c.protein.push_back("protein");

        c.fat.push_back("지방(g)");
        c.fat.push_back("지방");
        c.fat.push_back("fat(g)");
        c.fat.push_back("fat");

        c.basis.push_back("영양성분함량기준량");
        c.basis.push_back("기준량");
        c.basis.push_back("serving basis");

        c.serving.push_back("식품중량");
        c.serving.push_back("1회제공량");
        c.serving.push_back("총내용량(g)");
        c.serving.push_back("serving size");
        return c;
    }

    // ---------- 읽은 결과 ----------

    CsvReport::CsvReport()
        : dataRows(0), loaded(0), skipped(0), duplicates(0), mlRows(0),
          delimiter(','), encodingSuspect(false) {}

    std::string CsvReport::summary() const {
        std::ostringstream o;
        o << loaded << "개 담음 / 데이터 " << dataRows << "줄";
        if (skipped)    o << ", 버린 줄 " << skipped;
        if (duplicates) o << ", 이름 겹침 " << duplicates;
        if (mlRows)     o << ", mL 기준 " << mlRows;
        o << ", 구분자 " << (delimiter == '\t' ? "탭" : "쉼표");
        if (encodingSuspect) o << " [UTF-8 이 아닌 것 같습니다]";
        return o.str();
    }

    // ---------- 표 파일 다루는 잔손 (공개) ----------

    // 칸 안에 줄바꿈이 든 경우는 다루지 않는다. 영양성분표에는 그런 칸이 없고,
    // 다루기 시작하면 스트리밍으로 읽던 것을 통째로 메모리에 올려야 한다.
    std::vector<std::string> parseCsvLine(const std::string& line, char delimiter) {
        std::vector<std::string> out;
        std::string cur;
        bool quoted = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (quoted) {
                if (c != '"') { cur += c; continue; }
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else quoted = false;
            } else {
                if (c == '"')            quoted = true;
                else if (c == delimiter) { out.push_back(cur); cur.clear(); }
                else                     cur += c;
            }
        }
        out.push_back(cur);
        return out;
    }

    double gramsFromAmount(const std::string& text) {
        // "1회분(200g)" 처럼 괄호가 있으면 괄호 안이 진짜 무게다
        std::size_t open = text.find('(');
        if (open != npos()) {
            std::size_t close = text.find(')', open);
            if (close != npos() && close > open + 1) {
                double inner = scanGrams(text.substr(open + 1, close - open - 1));
                if (inner > 0.0) return inner;
            }
        }
        return scanGrams(text);
    }

    double nutrientNumber(const std::string& cell, bool& ok) {
        ok = false;
        std::string s;
        for (std::size_t i = 0; i < cell.size(); ++i) {
            char c = cell[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == ',') continue;
            s += c;
        }
        if (s.empty()) return 0.0;
        if (s == "-" || s == "." || s == "*" || lower(s) == "n/a" || lower(s) == "na")
            return 0.0;                                   // 모르는 값이지 0 이 아니다
        if (lower(s) == "tr" || s == "미량") { ok = true; return 0.0; }   // 미량 = 0 으로 본다

        std::istringstream in(s);
        double v = 0.0;
        if (!(in >> v)) return 0.0;
        ok = true;
        return v;
    }

    std::string foodBaseKey(const std::string& name) {
        std::string k = foodKey(name);
        static const char* cuts[] = {
            "_", ",", "(", "[", "/",
            "\xEF\xBC\x88",   // 전각 (
            "\xEF\xBC\x8C"    // 전각 ,
        };
        std::size_t cut = npos();
        for (std::size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
            std::size_t p = k.find(cuts[i]);
            if (p != npos() && (cut == npos() || p < cut)) cut = p;
        }
        if (cut == npos() || cut == 0) return k;
        return k.substr(0, cut);
    }

    // ---------- FoodCsvSource ----------

    FoodCsvSource::FoodCsvSource()
        : cols_(CsvColumns::mfds()), label_("식약처 식품영양성분DB") {}

    FoodCsvSource::FoodCsvSource(CsvColumns columns)
        : cols_(columns), label_("식약처 식품영양성분DB") {}

    void FoodCsvSource::setSourceLabel(std::string label) {
        if (label.empty()) return;
        label_ = label;
    }

    void FoodCsvSource::clear() {
        rows_.clear();
        byKey_.clear();
        byBase_.clear();
        report_ = CsvReport();
    }

    void FoodCsvSource::buildIndex() {
        byKey_.clear();
        byBase_.clear();
        byKey_.reserve(rows_.size());
        byBase_.reserve(rows_.size());
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            byKey_.push_back(i);
            byBase_.push_back(i);
        }
        const std::vector<Row>& rows = rows_;
        std::stable_sort(byKey_.begin(), byKey_.end(),
            [&rows](std::size_t a, std::size_t b) { return rows[a].key < rows[b].key; });
        std::stable_sort(byBase_.begin(), byBase_.end(),
            [&rows](std::size_t a, std::size_t b) { return rows[a].base < rows[b].base; });

        report_.duplicates = 0;
        for (std::size_t i = 1; i < byKey_.size(); ++i)
            if (rows_[byKey_[i]].key == rows_[byKey_[i - 1]].key) ++report_.duplicates;
    }

    CsvReport FoodCsvSource::read(std::istream& in) {
        clear();

        CsvColumns& C = cols_;
        std::size_t suppressed = 0;
        std::size_t encodingChecked = 0;

        // 머리줄을 찾는다. 앞에 설명 줄이 몇 개 붙어 있는 파일이 있어서
        // 무조건 첫 줄이라고 보지 않는다.
        std::vector<std::string> head;
        std::string line;
        std::size_t lineNo = 0;
        std::size_t iName = npos(), iCarb = npos(), iPro = npos(), iFat = npos();
        std::size_t iBasis = npos(), iServing = npos();
        bool found = false;

        while (head.size() < 10 && std::getline(in, line)) {
            ++lineNo;
            if (lineNo == 1 && line.size() >= 3
                && static_cast<unsigned char>(line[0]) == 0xEF
                && static_cast<unsigned char>(line[1]) == 0xBB
                && static_cast<unsigned char>(line[2]) == 0xBF)
                line = line.substr(3);                       // UTF-8 BOM
            if (trim(line).empty()) continue;

            const char delims[2] = { ',', '\t' };
            for (int d = 0; d < 2 && !found; ++d) {
                std::vector<std::string> raw = parseCsvLine(line, delims[d]);
                if (raw.size() < 2) continue;
                std::vector<std::string> keys;
                keys.reserve(raw.size());
                for (std::size_t i = 0; i < raw.size(); ++i)
                    keys.push_back(headerKey(trim(raw[i])));

                std::size_t n = findColumn(keys, C.name);
                std::size_t c = findColumn(keys, C.carb);
                std::size_t p = findColumn(keys, C.protein);
                std::size_t f = findColumn(keys, C.fat);
                if (n == npos() || (c == npos() && p == npos() && f == npos())) continue;

                iName = n; iCarb = c; iPro = p; iFat = f;
                iBasis   = findColumn(keys, C.basis);
                iServing = findColumn(keys, C.serving);
                report_.delimiter = delims[d];
                found = true;
            }
            if (found) break;
            head.push_back(line);
        }

        if (!found) {
            report_.warnings.push_back(
                "식품명/영양성분 칸이 있는 머리줄을 찾지 못했습니다 (앞 10줄을 봤습니다)");
            return report_;
        }

        while (std::getline(in, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);
            if (trim(line).empty()) continue;
            ++report_.dataRows;

            std::vector<std::string> cells = parseCsvLine(line, report_.delimiter);
            std::string name = cellAt(cells, iName);
            if (name.empty()) {
                ++report_.skipped;
                if (report_.warnings.size() < maxWarnings()) {
                    std::ostringstream o;
                    o << report_.dataRows << "번째 데이터 줄: 식품명이 비어 있습니다";
                    report_.warnings.push_back(o.str());
                } else ++suppressed;
                continue;
            }

            if (encodingChecked < 200) {
                ++encodingChecked;
                if (!isUtf8(name) && !report_.encodingSuspect) {
                    report_.encodingSuspect = true;
                    report_.warnings.push_back(
                        "식품명이 UTF-8 이 아닙니다. 엑셀에서 'CSV UTF-8' 로 다시 저장해 주세요");
                }
            }

            bool okC = false, okP = false, okF = false;
            double carb = nutrientNumber(cellAt(cells, iCarb), okC);
            double pro  = nutrientNumber(cellAt(cells, iPro),  okP);
            double fat  = nutrientNumber(cellAt(cells, iFat),  okF);

            // 셋 다 모르는 줄은 담을 값이 없다. 하나라도 알면 나머지는 0 으로 본다.
            if (!okC && !okP && !okF) {
                ++report_.skipped;
                if (report_.warnings.size() < maxWarnings())
                    report_.warnings.push_back(name + ": 탄단지 값이 하나도 없습니다");
                else ++suppressed;
                continue;
            }
            if (carb < 0.0 || pro < 0.0 || fat < 0.0) {
                ++report_.skipped;
                if (report_.warnings.size() < maxWarnings())
                    report_.warnings.push_back(name + ": 영양성분이 음수입니다");
                else ++suppressed;
                continue;
            }

            // 값이 몇 g 기준인가. 대부분 100g 이지만 "1회분(200g)" 인 줄이 섞여 있다.
            // 여기서 100g 으로 맞춰 두지 않으면 두 배 먹은 것으로 기록된다.
            std::string basisText = cellAt(cells, iBasis);
            double basisGrams = gramsFromAmount(basisText);
            if (basisGrams <= 0.0) {
                basisGrams = 100.0;                       // 기준량이 없으면 100g 으로 본다
                if (!basisText.empty() && report_.warnings.size() < maxWarnings())
                    report_.warnings.push_back(
                        name + ": 기준량 '" + basisText + "' 을 못 읽어 100g 으로 봤습니다");
            } else if (hasVolumeUnit(basisText)) {
                ++report_.mlRows;
            }
            double factor = 100.0 / basisGrams;

            Row row;
            try {
                row.info = FoodInfo(name,
                                    Macros(carb * factor, pro * factor, fat * factor),
                                    MacroSource::Official);
            } catch (const std::exception&) {
                ++report_.skipped;
                continue;
            }
            row.info.servingGrams = gramsFromAmount(cellAt(cells, iServing));
            row.info.confidence   = 1.0;
            row.info.origin       = label_;
            row.key  = foodKey(name);
            row.base = foodBaseKey(name);
            rows_.push_back(row);
            ++report_.loaded;
        }

        if (suppressed) {
            std::ostringstream o;
            o << "경고 " << suppressed << "건 더 있습니다 (생략)";
            report_.warnings.push_back(o.str());
        }

        buildIndex();
        return report_;
    }

    bool FoodCsvSource::load(const std::string& path, CsvReport* report, std::string* error) {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in) {
            if (error) *error = "파일을 열 수 없습니다: " + path;
            return false;
        }
        CsvReport r = read(in);
        if (report) *report = r;
        if (r.loaded == 0) {
            if (error) *error = path + " 에서 읽어낸 음식이 없습니다 - " + r.summary();
            return false;
        }
        return true;
    }

    // 후보 중 가장 짧은 이름을 고른다. 긴 이름은 대개 더 특수한 것이라
    // ("김치찌개_즉석조리식품") 사용자가 그냥 "김치찌개" 라고 쳤을 때 답이 아니다.
    bool FoodCsvSource::pick(const std::vector<std::size_t>& index,
                             bool byBaseKey,
                             const std::string& k,
                             std::size_t& best,
                             std::size_t& hits) const {
        const std::vector<Row>& rows = rows_;
        auto field = [&rows, byBaseKey](std::size_t i) -> const std::string& {
            return byBaseKey ? rows[i].base : rows[i].key;
        };
        std::vector<std::size_t>::const_iterator lo =
            std::lower_bound(index.begin(), index.end(), k,
                [&field](std::size_t i, const std::string& v) { return field(i) < v; });
        std::vector<std::size_t>::const_iterator hi =
            std::upper_bound(index.begin(), index.end(), k,
                [&field](const std::string& v, std::size_t i) { return v < field(i); });
        if (lo == hi) return false;

        hits = static_cast<std::size_t>(hi - lo);
        best = *lo;
        for (std::vector<std::size_t>::const_iterator it = lo; it != hi; ++it)
            if (rows_[*it].info.name.size() < rows_[best].info.name.size()) best = *it;
        return true;
    }

    bool FoodCsvSource::lookup(const std::string& foodName, FoodInfo& out) const {
        std::string k = foodKey(foodName);
        if (k.empty() || rows_.empty()) return false;

        std::size_t best = 0, hits = 0;

        // 1. 이름 그대로 일치
        if (pick(byKey_, false, k, best, hits)) {
            out = rows_[best].info;
            out.origin = label_ + " (" + out.name + ")";
            if (hits > 1) {
                std::ostringstream o;
                o << label_ << " (" << out.name << ", 같은 이름 " << hits << "건 중 첫 줄)";
                out.origin = o.str();
                out.confidence = 0.95;
            }
            return true;
        }

        // 2. 분류 접미를 뗀 뒤 일치. 값은 공공 DB 것이지만 "그 음식이 맞나" 는
        //    우리가 맞춘 것이므로 확신을 낮춰 둔다.
        if (pick(byBase_, true, k, best, hits)) {
            out = rows_[best].info;
            std::ostringstream o;
            o << label_ << " (" << out.name;
            if (hits > 1) o << ", 비슷한 이름 " << hits << "건 중 하나";
            o << ")";
            out.origin = o.str();
            out.confidence = (hits > 1) ? 0.85 : 0.95;
            return true;
        }

        return false;
    }

    std::vector<FoodInfo> FoodCsvSource::search(const std::string& part,
                                                std::size_t limit) const {
        std::vector<FoodInfo> out;
        std::string k = foodKey(part);
        if (k.empty() || limit == 0) return out;

        std::vector<std::size_t> hit;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].key.find(k) != npos()) hit.push_back(i);

        const std::vector<Row>& rows = rows_;
        std::stable_sort(hit.begin(), hit.end(),
            [&rows, &k](std::size_t a, std::size_t b) {
                bool pa = rows[a].key.compare(0, k.size(), k) == 0;
                bool pb = rows[b].key.compare(0, k.size(), k) == 0;
                if (pa != pb) return pa;                       // 앞에서부터 일치하는 것 먼저
                return rows[a].info.name.size() < rows[b].info.name.size();
            });

        for (std::size_t i = 0; i < hit.size() && out.size() < limit; ++i)
            out.push_back(rows_[hit[i]].info);
        return out;
    }

    std::size_t FoodCsvSource::exportTo(LocalFoodDatabase& db,
                                        const std::vector<std::string>& foodNames) const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < foodNames.size(); ++i) {
            FoodInfo got;
            if (!lookup(foodNames[i], got)) continue;
            db.add(got);
            ++n;
        }
        return n;
    }

}
