#ifndef FOOD
#define FOOD
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>
#include "intake.h"

// 음식 이름으로 영양성분을 찾아오는 곳.
//
// 유저가 밖에서 먹은 음식은 우리 Menu 목록에 없다. 사진에서 알아낸 이름이든
// 직접 친 이름이든, 그 음식의 일반적인 영양성분을 어딘가에서 가져와야 한다.
//
//   사진 ──(AI 인식)──► 이름 ──┐
//                              ├──► FoodResolver ──► FoodInfo ──► Macros ──► Meal
//   직접 입력 ─────── 이름 ──┘         │
//                                      ├ 1. LocalFoodDatabase  (캐시 / 우리가 모은 것)
//                                      ├ 2. 공공 DB            (UI 가 구현해 꽂는다)
//                                      └ 3. AI 추정            (마지막 수단)
//
// 이 파일은 인터넷을 타지 않는다. 네트워크와 AI 는 FoodSource 를 상속해서
// UI/서버 쪽이 구현해 꽂는다. input.h 가 스트림을 주입받는 것과 같은 이유다 -
// 코어는 네트워크 없이 그대로 테스트된다.
namespace domains {

    // ---------- 음식 하나의 영양 정보 ----------

    struct FoodInfo {
        std::string name;
        Macros per100g;                          // 100g 당 탄단지
        double servingGrams = 0.0;               // 1회 제공량. 0 이면 모름
        MacroSource source = MacroSource::Unknown;
        double confidence = 1.0;                 // 0..1, 추정일 때만 의미가 있다
        std::string origin;                      // "식약처 식품영양성분DB" 같은 출처 문구

        FoodInfo() = default;
        FoodInfo(std::string name, Macros per100g, MacroSource source);

        bool hasServing() const { return servingGrams > 0.0; }

        Macros forGrams(double grams) const;     // 그램 단위로 환산
        Macros forServings(double count) const;  // 1회 제공량 기준. 모르면 예외

        // 이 정보로 Meal 을 만든다. 출처가 그대로 따라 들어간다.
        Meal toMeal(MealTime slot, double grams) const;
        Meal toMeal(MealTime slot, TimeOfDay clock, double grams) const;
    };

    // ---------- 찾아오는 곳 (인터페이스) ----------

    // 네트워크든 AI든 파일이든, 이걸 상속하면 FoodResolver 에 꽂을 수 있다.
    class FoodSource {
    public:
        virtual ~FoodSource() {}

        // 화면에 보여줄 이 소스의 이름
        virtual std::string sourceName() const = 0;

        // 찾았으면 out 을 채우고 true. 못 찾으면 false (예외를 던지지 말 것).
        virtual bool lookup(const std::string& foodName, FoodInfo& out) const = 0;
    };

    // ---------- 우리가 들고 있는 표 ----------

    // 파일로 읽고 쓸 수 있는 로컬 표. 캐시로도 쓰고, 손으로 채워 넣어도 된다.
    // 한 번 조회한 결과를 여기 넣어 두면 다음부터는 네트워크를 타지 않는다.
    class LocalFoodDatabase : public FoodSource {
    public:
        void add(const FoodInfo& info);          // 같은 이름이면 덮어쓴다
        bool remove(const std::string& foodName);
        void clear();

        std::size_t size() const { return items_.size(); }
        bool empty() const { return items_.empty(); }
        std::vector<std::string> names() const;

        // 이름 일부로 찾기 (UI 자동완성용). 최대 limit 개.
        std::vector<FoodInfo> search(const std::string& part,
                                     std::size_t limit = 20) const;

        std::string sourceName() const override { return "로컬 음식표"; }
        bool lookup(const std::string& foodName, FoodInfo& out) const override;

        // ---- 파일 ----
        void write(std::ostream& out) const;
        void read(std::istream& in);             // 형식이 틀리면 예외
        bool save(const std::string& path, std::string* error = 0) const;
        bool load(const std::string& path, std::string* error = 0);

    private:
        std::vector<FoodInfo> items_;
        const FoodInfo* find(const std::string& key) const;
    };

    // ---------- 순서대로 시도하는 곳 ----------

    // 꽂은 순서대로 물어보고, 처음 찾아낸 것을 쓴다.
    // 로컬 -> 공공 DB -> AI 순으로 꽂으면 AI 는 정말 마지막에만 불린다.
    //
    // 소스의 수명은 꽂은 쪽이 관리한다 (이 클래스가 delete 하지 않는다).
    class FoodResolver {
    public:
        void addSource(const FoodSource* source);   // 널이면 예외
        void clearSources();
        std::size_t sourceCount() const { return sources_.size(); }

        // 찾았으면 out 을 채우고 true. 어느 소스가 답했는지는 out.origin 에 남는다.
        bool resolve(const std::string& foodName, FoodInfo& out) const;

        // 찾은 결과를 이 표에 자동으로 넣어 둔다 (캐시). 널이면 안 넣는다.
        void setCache(LocalFoodDatabase* cache) { cache_ = cache; }
        LocalFoodDatabase* cache() const { return cache_; }

    private:
        std::vector<const FoodSource*> sources_;
        LocalFoodDatabase* cache_ = 0;
    };

    // ---------- 직접 입력 ----------

    // DB 에 없거나 값이 틀렸을 때 사용자가 직접 넣는 길.
    // 출처가 Manual 로 박히므로 나중에 봐도 손으로 넣은 값인 줄 알 수 있다.
    FoodInfo manualEntry(std::string name,
                         Macros per100g,
                         double servingGrams = 0.0);

    // 100g 기준이 아니라 "이 한 그릇" 으로 통째로 넣고 싶을 때.
    // 1회 제공량을 그 무게로 잡고 100g 환산값을 역산해 둔다.
    FoodInfo manualServing(std::string name,
                           Macros perServing,
                           double servingGrams);

}

#endif
