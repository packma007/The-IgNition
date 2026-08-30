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

        // 파생 클래스가 채우는 부분
        virtual Divisibility divisibility() const = 0;
        virtual bool isValidAmount(double amount) const = 0;   // 판매 가능한 양인가
        virtual double normalize(double amount) const = 0;     // 판매 가능한 양으로 보정
        virtual long long priceFor(double amount) const;       // 보정된 양 기준 가격

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

    // ---------- 담을 수 있는 양의 한계 ----------

    // "이 메뉴를 지금 최대 얼마나 담을 수 있는가" 하나만 묻는 인터페이스.
    //
    // 재고(inventory.h)가 이걸 상속하고, 추천(recommend.h)이 이걸 들여다본다.
    // 둘 사이에 직접 의존이 생기지 않도록 여기 둔다 - 재고는 추천을 모르고,
    // 추천은 그 한계가 재고에서 왔는지 다른 무엇에서 왔는지 모른다.
    // FoodSource(food.h)나 DistanceProvider(delivery.h)와 같은 방식이다.
    class StockLimits {
    public:
        virtual ~StockLimits() {}

        // 0 이면 지금은 그 메뉴를 담을 수 없다
        // (매진이거나, 남은 것이 최소 판매량보다 적다).
        // 한계가 없으면 아주 큰 값을 준다.
        //
        // 돌려주는 값은 그 메뉴가 실제로 팔 수 있는 양이어야 한다.
        // Menu::normalize() 는 위로 올리므로 그대로 쓰면 안 된다 - 아래로 내림 보정한
        // 값을 줘야 남은 것보다 많이 파는 일이 없다.
        virtual double capFor(const Menu& menu) const = 0;
    };

}

#endif
