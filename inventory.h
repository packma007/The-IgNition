#ifndef INVENTORY
#define INVENTORY
#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include "datetime.h"
#include "domains.h"
#include "weeklymenu.h"

// 오늘 무엇이 얼마나 남았는지를 들고 있는 곳.
//
// 메뉴판(weeklymenu.h)은 "이번 주에 파는 것" 이고, 재고는 "지금 실제로 남은 것" 이다.
// 둘은 다르다. 제육볶음은 이번 주 내내 메뉴판에 있지만 화요일 12시 반이면 다 나가고 없다.
// 메뉴판만 보고 추천을 내면 "제육볶음 200g" 을 띄워 놓고 주문만 실패한다.
//
//   WeeklyMenu 15가지 ─ 이번 주 내내 고정
//          │
//          ▼
//   DailyStock ─ 하루치. 아침에 만든 양에서 나간 만큼 깎인다
//          ├ 잡힌 양(held) : 장바구니에 담겼지만 아직 결제 전
//          └ 나간 양(sold) : 결제까지 끝나 확정된 양
//
//   남은 양(available) = 만든 양 - 잡힌 양 - 나간 양
//
// 왜 "잡힌 양" 을 따로 두는가:
//   추천 화면에서 제육볶음 200g 을 담고 결제까지 30초가 걸린다. 그 사이에 남은 150g 을
//   다른 사람이 결제해 버리면, 앞사람은 화면에 200g 을 띄운 채로 결제만 실패한다.
//   담는 순간 그 양을 잡아 두고(hold), 결제되면 확정하고(commit), 창을 닫으면 풀어 준다(release).
//   잡아 놓고 그냥 사라지는 손님이 반드시 있으므로 잡은 것은 만료 시각을 갖는다 - 안 그러면
//   장바구니에 넣고 나간 사람 때문에 팔 수 있는 음식이 매진으로 잠긴다.
//
// 재고를 안 잡아 둔 메뉴는 "무제한" 으로 본다(kUnlimited).
// 밥이나 양념처럼 떨어질 일이 없는 것까지 매일 숫자를 채워 넣게 만들면 아무도 안 채운다.
// 세는 것이 의미 있는 것만 세고, 나머지는 조용히 통과시키는 편이 실제로 지켜진다.
//
// 이 파일은 계산을 하지 않는다. 재고는 "얼마나 남았는가" 를 정확히 들고 있기만 하고,
// 그걸 추천에 물리는 일은 DailyStock 이 상속하는 StockLimits(domains.h) 가 한다.
// MealPlanner 에 그 인터페이스로 꽂으면 양을 풀 때 상한이 저절로 걸린다.
// recommend.h 를 여기서 include 하지 않는 이유다 - 재고는 추천을 모른다.
namespace domains {

    // 재고를 잡아 두지 않은 메뉴의 "남은 양". 실질적으로 무한대인 큰 값이다.
    extern const double kUnlimited;

    // ---------- 재고 상태 ----------

    // UI 가 뱃지로 그릴 재료다 (recommend.h 의 Issue 와 같은 쓰임).
    enum class StockState {
        Plenty,     // 넉넉함
        Low,        // 얼마 안 남음 (lowMark 아래)
        SoldOut     // 매진
    };

    // 화면에 그대로 띄울 수 있는 짧은 문구
    std::string describe(StockState state);

    // ---------- 메뉴 하나의 재고 ----------

    struct StockItem {
        std::string menuName;
        std::string unit;           // 화면 표시용 사본 ("g", "개"). Menu 에서 베껴 온다
        double prepared = 0.0;      // 오늘 만든 양
        double held     = 0.0;      // 장바구니에 잡혀 있는 양
        double sold     = 0.0;      // 결제까지 끝난 양
        double lowMark  = 0.0;      // 이 아래로 떨어지면 Low. 0 이면 Low 경고를 안 띄운다

        StockItem() = default;
        StockItem(std::string menuName, std::string unit, double prepared);

        // 지금 새 손님에게 팔 수 있는 양. 음수가 되지 않는다.
        double available() const;

        // 아직 주방에 남아 있는 양 (잡히기만 하고 안 나간 것은 여기 포함된다).
        // 마감 때 "몇 인분 버렸나" 를 세는 쪽은 이 값이다.
        double remaining() const;

        bool isSoldOut() const;
        StockState state() const;
    };

    // ---------- 잡아 둔 표 ----------

    // 장바구니 하나가 재고를 붙잡고 있다는 증서.
    // 양을 다시 넘겨받아 푸는 대신 이 표로 풀면, 같은 장바구니를 두 번 풀거나
    // 담은 양과 다른 양을 푸는 사고가 애초에 일어나지 않는다.
    using HoldId = long long;

    struct Hold {
        HoldId id = 0;
        std::string menuName;
        double amount = 0.0;
        TimeOfDay until;            // 이 시각이 지나면 expire() 가 풀어 준다

        Hold() = default;
        Hold(HoldId id, std::string menuName, double amount, TimeOfDay until);
    };

    // ---------- 하루치 재고 ----------

    class DailyStock : public StockLimits {
    public:
        DailyStock();                          // 오늘
        explicit DailyStock(const Date& date);

        const Date& date() const { return date_; }
        void setDate(const Date& date) { date_ = date; }

        // ---- 아침에 채우기 ----

        // 그 메뉴의 오늘치를 이 양으로 정한다. 이미 있으면 만든 양만 바꾼다
        // (잡힌 양과 나간 양은 그대로 둔다 - 이미 판 것을 없던 일로 만들 수는 없다).
        // 만든 양을 이미 나간 양보다 적게 잡으면 예외.
        void setPrepared(const std::string& menuName, const std::string& unit, double amount);
        void setPrepared(const Menu& menu, double amount);      // unit 을 메뉴에서 가져온다

        // 장사 중에 더 만들었다. 재고를 안 잡아 둔 메뉴면 새로 잡아 준다.
        void addPrepared(const std::string& menuName, double amount);

        // 이 아래로 떨어지면 Low 로 본다. 없는 메뉴면 예외.
        void setLowMark(const std::string& menuName, double amount);

        // 재고 관리 자체를 그만둔다 (= 다시 무제한으로 본다). 잡힌 표도 함께 풀린다.
        bool remove(const std::string& menuName);
        void clear();

        // ---- 들여다보기 ----

        std::size_t size() const { return items_.size(); }
        bool empty() const { return items_.empty(); }

        // 재고를 세고 있는 메뉴인가. false 면 무제한으로 취급된다.
        bool tracks(const std::string& menuName) const;

        const StockItem* find(const std::string& menuName) const;   // 없으면 널
        std::vector<StockItem> items() const;                       // 이름 오름차순
        std::vector<std::string> names() const;

        // 안 세는 메뉴면 kUnlimited
        double available(const std::string& menuName) const;
        bool has(const std::string& menuName, double amount) const;
        StockState state(const std::string& menuName) const;

        // MealPlanner 에 넘길 상한 (StockLimits 구현).
        // 남은 양을 그 메뉴가 실제로 팔 수 있는 양으로 아래쪽으로 다듬는다
        // (Menu::normalize() 는 위로 올리므로 그대로 쓰면 안 된다).
        // 재고를 안 세는 메뉴면 kUnlimited.
        //
        // !! 0 이 나오면 오늘은 그 메뉴를 못 판다는 뜻이다 (남은 게 최소 판매량보다 적다).
        //    Pick::maxAmount 에서 0 은 "상한 없음" 이므로 그대로 넣으면 정반대가 된다.
        //    이 재고를 MealPlanner::setStockLimits() 로 꽂으면 그쪽이 알아서 걸러 주므로
        //    직접 maxAmount 에 넣지 말고 꽂아 쓰는 편이 안전하다.
        double capFor(const Menu& menu) const override;

        // ---- 잡기 / 확정 / 풀기 ----

        // 남은 양보다 많이 잡으려 하면 0 을 준다. 매진은 오류가 아니라 상태이므로 예외가 아니다.
        // (amount 가 0 이하인 것은 부르는 쪽의 실수이므로 이쪽은 예외다.)
        // 안 세는 메뉴도 표를 발급한다 - 부르는 쪽이 세는 메뉴인지 알 필요가 없어야 한다.
        //
        // until 은 하루 안의 시각이다. 하루치 재고이므로 날짜를 넘는 표는 없다.
        // 23:58 에 5분짜리 창을 잡는 것처럼 자정을 넘기는 경우는 부르는 쪽이
        // 23:59 로 잘라서 넘긴다 (여기서는 now 를 모르므로 넘어간 것을 알 수 없다).
        HoldId hold(const std::string& menuName, double amount, TimeOfDay until);

        bool release(HoldId id);            // 장바구니를 비웠다. 잡힌 양이 돌아온다
        bool commit(HoldId id);             // 결제됐다. 잡힌 양이 나간 양으로 옮겨간다

        // until 이 지난 표를 모두 풀고 그 개수를 준다. 화면을 그리기 전에 한 번 부르면 된다.
        std::size_t expire(TimeOfDay now);

        const Hold* findHold(HoldId id) const;                      // 없으면 널
        std::vector<Hold> holds() const;
        std::size_t holdCount() const { return holds_.size(); }
        double heldFor(const std::string& menuName) const;

        // ---- 표 없이 바로 ----

        // 매장에서 즉석으로 팔았을 때. 남은 양보다 많으면 아무것도 깎지 않고 false.
        bool sell(const std::string& menuName, double amount);

        // 잘못 찍었거나 주문이 취소됐을 때 되돌린다. 나간 양보다 많이 되돌리면 false.
        bool refund(const std::string& menuName, double amount);

        // ---- 마감과 점검 ----

        std::vector<std::string> soldOutNames() const;
        std::vector<std::string> lowNames() const;

        // 오늘 메뉴판에는 있는데 재고를 안 잡아 둔 메뉴들.
        // 아침에 빠뜨린 것을 잡아내는 용도다 - 안 세는 메뉴는 무제한으로 팔리므로
        // 실수로 빠뜨린 것과 일부러 안 세는 것을 화면에서 구분해 줘야 한다.
        std::vector<std::string> untracked(const WeeklyMenu& menu) const;

        // 남은 양의 합 (단위가 섞이므로 숫자 자체에 큰 뜻은 없다. 다 나갔는지 보는 용도).
        double totalRemaining() const;

    private:
        Date date_;
        std::map<std::string, StockItem> items_;    // 열쇠는 메뉴 이름 (WeeklyMenu 와 같은 규칙)
        std::vector<Hold> holds_;
        HoldId nextHoldId_ = 1;

        StockItem* locate(const std::string& menuName);
        const StockItem* locate(const std::string& menuName) const;
    };

    // ---------- 날짜별 재고 ----------

    // Calendar / MenuBook 과 같은 모양이다.
    // 지난 날치를 버리지 않는 이유: 내일 얼마나 만들지는 지난 며칠에 무엇이 몇 시에
    // 매진됐고 무엇이 남아서 버려졌는지를 봐야 정할 수 있다.
    class StockBook {
    public:
        void set(const DailyStock& day);        // 같은 날짜가 있으면 통째로 바꾼다
        bool remove(const Date& date);
        void clear();

        const DailyStock* forDate(const Date& date) const;   // 없으면 널
        DailyStock* forDate(const Date& date);

        DailyStock& dayOf(const Date& date);    // 없으면 그 날의 빈 재고를 만들어 준다
        bool has(const Date& date) const;

        const std::map<Date, DailyStock>& days() const { return days_; }
        std::size_t size() const { return days_.size(); }
        bool empty() const { return days_.empty(); }
        std::vector<Date> dates() const;        // 오름차순

    private:
        std::map<Date, DailyStock> days_;
    };

    // ---------- 아침에 재고 세우기 ----------

    // 그 주 메뉴판의 15가지를 모두 채운 하루치를 만든다.
    // 낱개로 파는 메뉴는 개수, 무게로 파는 메뉴는 g 이므로 같은 숫자를 넣으면 안 된다.
    // 그래서 두 값을 따로 받는다 (계란말이 20줄 / 잡곡밥 4000g 처럼).
    DailyStock openingStock(const Date& date, const WeeklyMenu& menu,
                            double gramsEach, double piecesEach);

}

#endif
