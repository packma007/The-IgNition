#include "domains.h"
#include <iostream>
#include <string>
#include <cmath>
#include <stdexcept>

namespace domains {

    namespace {
        // 부동소수점 오차를 감안한 비교
        const double kEps = 1e-9;

        bool isMultipleOf(double amount, double step) {
            if (step <= 0.0) return true;
            double q = amount / step;
            return std::fabs(q - std::round(q)) < kEps;
        }

        std::string trimZeros(double v) {
            std::string s = std::to_string(v);
            std::size_t dot = s.find('.');
            if (dot == std::string::npos) return s;
            std::size_t last = s.find_last_not_of('0');
            if (last == dot) last = dot - 1;
            return s.substr(0, last + 1);
        }
    }

    // ---------- Nutrient ----------

    Nutrient::Nutrient(double amountPerUnit)
        : amountPerUnit_(amountPerUnit) {
        if (amountPerUnit_ < 0.0) throw std::invalid_argument("amountPerUnit must be >= 0");
    }

    double Nutrient::amountFor(double menuAmount) const {
        return amountPerUnit_ * menuAmount;
    }

    double Nutrient::caloriesFor(double menuAmount) const {
        return amountFor(menuAmount) * kcalPerGram();
    }

    std::string Nutrient::describe(double menuAmount) const {
        return name() + " " + trimZeros(amountFor(menuAmount)) + unit()
             + " (" + trimZeros(caloriesFor(menuAmount)) + "kcal)";
    }

    // ---------- Menu ----------

    Menu::Menu(std::string name, std::string unit, long long unitPrice)
        : name_(std::move(name)), unit_(std::move(unit)), unitPrice_(unitPrice) {
        if (unitPrice_ < 0) throw std::invalid_argument("unitPrice must be >= 0");
    }

    long long Menu::priceFor(double amount) const {
        double a = normalize(amount);
        return static_cast<long long>(std::llround(a * static_cast<double>(unitPrice_)));
    }

    std::string Menu::describe(double amount) const {
        double a = normalize(amount);
        return name_ + " " + trimZeros(a) + unit_
             + " - " + std::to_string(priceFor(a)) + "원";
    }

    void Menu::addNutrient(NutrientPtr nutrient) {
        if (!nutrient) throw std::invalid_argument("nutrient must not be null");
        for (auto& n : nutrients_) {
            if (n->name() == nutrient->name()) {   // 같은 영양소는 덮어쓴다
                n = std::move(nutrient);
                return;
            }
        }
        nutrients_.push_back(std::move(nutrient));
    }

    const Nutrient* Menu::findNutrient(const std::string& name) const {
        for (const auto& n : nutrients_) {
            if (n->name() == name) return n.get();
        }
        return nullptr;
    }

    double Menu::caloriesFor(double amount) const {
        double a = normalize(amount);
        double total = 0.0;
        for (const auto& n : nutrients_) total += n->caloriesFor(a);
        return total;
    }

    std::string Menu::describeNutrition(double amount) const {
        double a = normalize(amount);
        if (nutrients_.empty()) return name_ + " - 영양 정보 없음";

        std::string s = name_ + " " + trimZeros(a) + unit_ + " : ";
        for (std::size_t i = 0; i < nutrients_.size(); ++i) {
            if (i > 0) s += ", ";
            s += nutrients_[i]->describe(a);
        }
        return s + " / 합계 " + trimZeros(caloriesFor(a)) + "kcal";
    }

    // ---------- DiscreteMenu ----------

    DiscreteMenu::DiscreteMenu(std::string name,
                               std::string unit,
                               long long unitPrice,
                               int minCount,
                               int step)
        : Menu(std::move(name), std::move(unit), unitPrice),
          minCount_(minCount), step_(step) {
        if (step_ <= 0) throw std::invalid_argument("step must be > 0");
        if (minCount_ < 0) throw std::invalid_argument("minCount must be >= 0");
    }

    bool DiscreteMenu::isValidAmount(double amount) const {
        // 정수여야 하고, 최소 수량 이상이며, 묶음 단위로 나눠떨어져야 함
        if (std::fabs(amount - std::round(amount)) > kEps) return false;
        long long n = std::llround(amount);
        if (n < minCount_) return false;
        return (n - minCount_) % step_ == 0;
    }

    double DiscreteMenu::normalize(double amount) const {
        long long n = std::llround(std::ceil(amount - kEps));
        if (n < minCount_) n = minCount_;
        long long over = (n - minCount_) % step_;
        if (over != 0) n += (step_ - over);   // 위쪽 묶음 단위로 올림
        return static_cast<double>(n);
    }

    // ---------- ContinuousMenu ----------

    ContinuousMenu::ContinuousMenu(std::string name,
                                   std::string unit,
                                   long long unitPrice,
                                   double minAmount,
                                   double maxAmount,
                                   double step)
        : Menu(std::move(name), std::move(unit), unitPrice),
          minAmount_(minAmount), maxAmount_(maxAmount), step_(step) {
        if (minAmount_ < 0.0) throw std::invalid_argument("minAmount must be >= 0");
        if (step_ < 0.0) throw std::invalid_argument("step must be >= 0");
        if (maxAmount_ > 0.0 && maxAmount_ < minAmount_)
            throw std::invalid_argument("maxAmount must be >= minAmount");
    }

    bool ContinuousMenu::isValidAmount(double amount) const {
        if (amount < minAmount_ - kEps) return false;
        if (maxAmount_ > 0.0 && amount > maxAmount_ + kEps) return false;
        return isMultipleOf(amount, step_);
    }

    double ContinuousMenu::normalize(double amount) const {
        double a = amount;
        if (a < minAmount_) a = minAmount_;
        if (step_ > 0.0) a = std::ceil(a / step_ - kEps) * step_;   // 계량 단위로 올림
        if (maxAmount_ > 0.0 && a > maxAmount_) a = maxAmount_;
        return a;
    }

}
