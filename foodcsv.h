#ifndef FOODCSV
#define FOODCSV
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>
#include "food.h"

// 공공 영양성분 DB 표 파일(CSV/TSV)을 FoodSource 로 바꿔 주는 곳.
// food.h 가 "2. 공공 DB (UI 가 구현해 꽂는다)" 라고 비워 둔 자리에 실제로 꽂히는 첫 구현이다.
//
//   식약처 식품영양성분DB (내려받은 csv 한 덩어리)
//         │
//         ▼
//   FoodCsvSource ──(FoodSource)──► FoodResolver ──► FoodInfo ──► Meal
//
// 왜 HTTP 가 아니라 파일인가:
//   - C++14 표준에 네트워크가 없다. 붙이는 순간 코어가 플랫폼 라이브러리에 묶이고,
//     API 키 없이는 테스트도 못 한다.
//   - 영양성분표는 실시간 값이 아니다. 김치찌개 100g 의 단백질은 오늘도 내일도 같다.
//   - 지하철에서도 돌아야 한다. 밥 먹는 자리에서 신호가 없다고 기록을 못 하면 안 된다.
// 나중에 API 를 붙이더라도 FoodSource 를 하나 더 상속하면 되고, 이 파일은 그대로 남는다.
//
// 이 파일도 인터넷을 타지 않는다. 표 파일 하나만 읽는다.
namespace domains {

    // ---------- 어느 칸을 읽을 것인가 ----------

    // 공공 DB 는 배포 회차마다 칸 이름과 순서가 조금씩 달라진다
    // ("탄수화물(g)" / "탄수화물", 앞에 설명 줄이 붙기도 한다).
    // 그래서 칸 번호를 박아 두지 않고 "이름 후보 목록"으로 찾는다.
    // 칸이 늘거나 순서가 바뀌어도 그대로 읽힌다.
    struct CsvColumns {
        std::vector<std::string> name;      // 식품명
        std::vector<std::string> carb;      // 탄수화물(g)
        std::vector<std::string> protein;   // 단백질(g)
        std::vector<std::string> fat;       // 지방(g)
        std::vector<std::string> basis;     // 영양성분함량기준량 - "100g" 인지 "1회분(200g)" 인지
        std::vector<std::string> serving;   // 식품중량 - 한 그릇이 몇 g 인지

        static CsvColumns mfds();           // 식약처 식품영양성분DB 기본값
    };

    // ---------- 읽고 나서 무슨 일이 있었는지 ----------

    // 실패를 숨기지 않는다. 4만 줄짜리 표에서 조용히 100줄이 사라지면
    // 나중에 "왜 이 음식만 안 나오지" 로 돌아온다. 몇 줄을 왜 버렸는지 남긴다.
    struct CsvReport {
        std::size_t dataRows;       // 머리줄을 뺀 줄 수
        std::size_t loaded;         // 실제로 담은 음식 수
        std::size_t skipped;        // 버린 줄 수
        std::size_t duplicates;     // 이름이 겹치는 줄 수 (버리지 않는다. 조회는 첫 줄을 쓴다)
        std::size_t mlRows;         // 기준량이 mL 여서 1mL=1g 으로 본 줄 수
        char delimiter;             // 자동으로 알아낸 구분자
        bool encodingSuspect;       // UTF-8 이 아닌 것 같다 (CP949 로 저장된 파일)
        std::vector<std::string> warnings;   // 앞쪽 몇 개만 (아래 maxWarnings)

        CsvReport();
        std::string summary() const;         // 한 줄 요약. 화면/로그에 그대로 쓴다
    };

    // ---------- 표 파일 소스 ----------

    class FoodCsvSource : public FoodSource {
    public:
        FoodCsvSource();
        explicit FoodCsvSource(CsvColumns columns);

        // 화면과 FoodInfo::origin 에 남을 출처 문구
        void setSourceLabel(std::string label);
        std::string sourceName() const override { return label_; }

        // 이름으로 찾기. 규칙은 두 단계뿐이다:
        //   1. 이름 그대로 일치          "김치찌개"  == "김치찌개"
        //   2. 분류 접미를 뗀 뒤 일치     "김치찌개"  <- "김치찌개_음식점"
        // 부분 일치로 추측하지 않는다. "밥" 이 "밥과자" 를 물어 오면
        // 사용자는 자기가 뭘 먹었는지 화면에서 알아볼 수 없다.
        // 부분 일치는 search() 로 후보를 늘어놓고 사람이 고르게 한다.
        //
        // 2번으로 찾은 경우 confidence 를 1.0 밑으로 내린다 - 값 자체는 공공 DB 것이지만
        // "그 음식이 맞나"는 우리가 맞춘 것이기 때문이다.
        bool lookup(const std::string& foodName, FoodInfo& out) const override;

        // ---- 읽기 ----
        // 예외를 던지지 않는다. 못 읽은 줄은 report 에 남기고 나머지를 담는다.
        // 표 하나가 완벽하지 않다고 4만 건을 통째로 버릴 이유가 없다.
        CsvReport read(std::istream& in);
        bool load(const std::string& path,
                  CsvReport* report = 0,
                  std::string* error = 0);

        const CsvReport& lastReport() const { return report_; }

        // ---- 들여다보기 ----
        std::size_t size() const { return rows_.size(); }
        bool empty() const { return rows_.empty(); }
        void clear();

        // 이름 일부로 후보 늘어놓기 (UI 자동완성 / 확인 화면용).
        // 앞에서부터 일치하는 것, 그다음 짧은 이름 순. 짧은 이름이 대개 더 일반적인 음식이다.
        std::vector<FoodInfo> search(const std::string& part,
                                     std::size_t limit = 20) const;

        // 자주 먹는 것만 뽑아 로컬 표로 옮긴다.
        // 4만 건을 통째로 들고 다니지 않고 앱에 작은 표만 넣고 싶을 때 쓴다.
        // 찾아서 넣은 개수를 돌려준다.
        std::size_t exportTo(LocalFoodDatabase& db,
                             const std::vector<std::string>& foodNames) const;

        static std::size_t maxWarnings() { return 20; }

    private:
        struct Row {
            FoodInfo info;
            std::string key;    // 다듬은 이름          "김치찌개_음식점"
            std::string base;   // 분류 접미를 뗀 이름   "김치찌개"
        };

        CsvColumns cols_;
        std::string label_;
        std::vector<Row> rows_;
        std::vector<std::size_t> byKey_;    // key 로 정렬한 색인 (이분 탐색용)
        std::vector<std::size_t> byBase_;   // base 로 정렬한 색인
        CsvReport report_;

        void buildIndex();
        bool pick(const std::vector<std::size_t>& index,
                  bool byBaseKey,
                  const std::string& k,
                  std::size_t& best,
                  std::size_t& hits) const;
    };

    // ---------- 표 파일을 다루는 잔손 (테스트와 다른 표에도 쓸 수 있게 공개) ----------

    // 따옴표 안의 구분자와 "" 로 쓴 따옴표를 지켜 가며 한 줄을 칸으로 자른다.
    std::vector<std::string> parseCsvLine(const std::string& line, char delimiter);

    // "100g", "1회분(200g)", "200mL", "1개(60g)" 에서 그램 수를 뽑는다.
    // 괄호 안의 무게를 먼저 본다. 못 뽑으면 0.
    double gramsFromAmount(const std::string& text);

    // 영양성분 칸 한 개를 숫자로. "1,234.5" 는 읽고, "Tr"/"미량" 은 0(ok),
    // 빈 칸과 "-", "N/A" 는 ok=false (모르는 값이지 0 이 아니다).
    double nutrientNumber(const std::string& cell, bool& ok);

    // 이름에서 분류 접미를 뗀다. "김치찌개_음식점" -> "김치찌개", "밥, 백미" -> "밥"
    std::string foodBaseKey(const std::string& name);

}

#endif
