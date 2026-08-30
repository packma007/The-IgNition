#ifndef DOMAINS
#define DOMAINS
#include <string>
#include <array>
#include <vector>
#include <memory>
#include <utility>

namespace domains {

    // ---------- 영양소 ----------

    // 영양소 한 종류. 새 영양소를 추가하려면 이 클래스를 상속만 하면 된다.
    class Nutrient {
    public:
        explicit Nutrient(double amountPerUnit);
        virtual ~Nutrient() = default;

        // 파생 클래스가 채우는 부분
        virtual std::string name() const = 0;          // "탄수화물"
        virtual double kcalPerGram() const = 0;        // 1g당 열량
        virtual std::string unit() const { return "g"; }

        double amountPerUnit() const { return amountPerUnit_; }   // 메뉴 1단위당 함량
        double amountFor(double menuAmount) const;                // 주문량 기준 함량
        double caloriesFor(double menuAmount) const;              // 주문량 기준 열량
        std::string describe(double menuAmount) const;            // "탄수화물 24g (96kcal)"

    protected:
        double amountPerUnit_;
    };

    class Carbohydrate : public Nutrient {
    public:
        explicit Carbohydrate(double amountPerUnit) : Nutrient(amountPerUnit) {}
        std::string name() const override { return "탄수화물"; }
        double kcalPerGram() const override { return 4.0; }
    };

    class Protein : public Nutrient {
    public:
        explicit Protein(double amountPerUnit) : Nutrient(amountPerUnit) {}
        std::string name() const override { return "단백질"; }
        double kcalPerGram() const override { return 4.0; }
    };

    class Fat : public Nutrient {
    public:
        explicit Fat(double amountPerUnit) : Nutrient(amountPerUnit) {}
        std::string name() const override { return "지방"; }
        double kcalPerGram() const override { return 9.0; }
    };

    using NutrientPtr = std::shared_ptr<Nutrient>;

    // ---------- 메뉴 ----------

    // 메뉴가 어떤 방식으로 나뉘는지
    enum class Divisibility {
        Discrete,    // 이산적: 1잔, 2개 처럼 낱개로만 나뉨
        Continuous   // 연속적: 100g, 250ml 처럼 임의의 양으로 나뉨
    };

    // 모든 메뉴의 공통 기반 클래스
    class Menu {
    public:
        Menu(std::string name, std::string unit, long long unitPrice);
        virtual ~Menu() = default;

        const std::string& name() const { return name_; }
        const std::string& unit() const { return unit_; }      // "잔", "개", "g", "ml"
        long long unitPrice() const { return unitPrice_; }     // 단위 1개(1g)당 가격

        // 영양소: 종류가 늘어나도 이 배열에 넣기만 하면 된다
        void addNutrient(NutrientPtr nutrient);                // 같은 이름이면 교체

        // 영양소 클래스를 직접 만들어 넣는 간편 함수
        // 예: menu.addNutrient<Carbohydrate>(24.0);
        template <typename N, typename... Args>
        void addNutrient(Args&&... args) {
            addNutrient(std::make_shared<N>(std::forward<Args>(args)...));
        }

        const std::vector<NutrientPtr>& nutrients() const { return nutrients_; }
        const Nutrient* findNutrient(const std::string& name) const;
        double caloriesFor(double amount) const;               // 보정된 양 기준 총 열량
        std::string describeNutrition(double amount) const;

        // 파생 클래스가 채우는 부분
        virtual Divisibility divisibility() const = 0;
        virtual bool isValidAmount(double amount) const = 0;   // 판매 가능한 양인가
        virtual double normalize(double amount) const = 0;     // 판매 가능한 양으로 보정
        virtual long long priceFor(double amount) const;       // 보정된 양 기준 가격
        virtual std::string describe(double amount) const;     // "아메리카노 2잔 - 6000원"

    protected:
        std::string name_;
        std::string unit_;
        long long unitPrice_;
        std::vector<NutrientPtr> nutrients_;   // 메뉴 1단위당 영양소 목록
    };

    // 이산적으로 나눠떨어지는 메뉴 (커피 잔, 빵 개수 ...)
    class DiscreteMenu : public Menu {
    public:
        DiscreteMenu(std::string name,
                     std::string unit,
                     long long unitPrice,
                     int minCount = 1,
                     int step = 1);

        int minCount() const { return minCount_; }
        int step() const { return step_; }

        Divisibility divisibility() const override { return Divisibility::Discrete; }
        bool isValidAmount(double amount) const override;
        double normalize(double amount) const override;

    private:
        int minCount_;   // 최소 주문 수량
        int step_;       // 묶음 단위 (2개씩만 판다면 2)
    };

    // 연속적으로 나눠떨어지는 메뉴 (원두 무게, 음료 용량 ...)
    class ContinuousMenu : public Menu {
    public:
        ContinuousMenu(std::string name,
                       std::string unit,
                       long long unitPrice,
                       double minAmount = 0.0,
                       double maxAmount = 0.0,   // 0이면 상한 없음
                       double step = 0.0);       // 0이면 완전 연속

        double minAmount() const { return minAmount_; }
        double maxAmount() const { return maxAmount_; }
        double step() const { return step_; }

        Divisibility divisibility() const override { return Divisibility::Continuous; }
        bool isValidAmount(double amount) const override;
        double normalize(double amount) const override;

    private:
        double minAmount_;
        double maxAmount_;
        double step_;    // 계량 단위 (10g 단위로만 판다면 10)
    };

    using MenuPtr = std::shared_ptr<Menu>;

}

#endif
