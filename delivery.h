#ifndef DELIVERY
#define DELIVERY
#include <cstddef>
#include <string>
#include <vector>
#include "datetime.h"
#include "location.h"

// 여러 집을 최대한 짧게, 그리고 늦지 않게 도는 순서를 찾는 곳.
//
// 시간 창이 없으면 외판원 문제(TSP), 있으면 시간 창이 있는 차량 경로 문제(VRPTW)다.
// 둘 다 정답을 정확히 구하는 것은 집이 스무 곳만 넘어가도 현실적으로 불가능하다.
// 배달 앱들도 정답을 구하지 않고 아래 두 단계를 쓴다. 여기서도 같은 방법을 쓴다.
//
//   1단계  가까우면서 시간에 쫓기는 곳부터 이어 붙인다   (시간을 보는 nearest neighbor)
//   2단계  꼬인 곳을 풀어 짧게 만든다                    (2-opt, Or-opt)
//
// 시간 창이 붙으면 "짧은 것" 이 곧 "좋은 것" 이 아니게 된다.
// 1분 늦는 것과 1km 더 도는 것 중 무엇이 나은지를 정해야 하고,
// 그게 setLatePenaltyMetersPerMinute() 다.
namespace domains {

    // ---------- 시간 창 ----------

    // "12:00 ~ 12:30 사이에 와 주세요".
    // 일찍 도착하면 기다리고, 늦으면 지각이다.
    struct TimeWindow {
        TimeOfDay earliest;
        TimeOfDay latest;
        bool used = false;      // false 면 시간 제약이 없는 배달

        TimeWindow() = default;
        TimeWindow(TimeOfDay earliest, TimeOfDay latest);   // latest < earliest 면 예외

        bool isSet() const { return used; }
    };

    // 배달 한 건
    struct Stop {
        std::string label;              // 화면에 보일 이름 (사용자명, 주문번호 등)
        Location location;
        TimeWindow window;              // 안 넣으면 시간 제약 없음
        double serviceMinutes = 3.0;    // 도착해서 건네주고 떠나기까지 걸리는 시간

        Stop() = default;
        Stop(std::string label, Location location);
        Stop(std::string label, Location location, TimeWindow window);
    };

    // ---------- 거리와 소요 시간 ----------

    // 실제 서비스에서는 이걸 상속해서 도로 경로 API 를 꽂는다.
    // FoodSource 와 같은 방식으로, 코어는 네트워크 없이 그대로 테스트된다.
    class DistanceProvider {
    public:
        virtual ~DistanceProvider() {}
        virtual std::string providerName() const = 0;
        virtual double meters(const Location& a, const Location& b) const = 0;

        // 걸리는 시간(분). 기본값은 거리 / 평균속도다.
        // 도로 API 를 꽂으면 교통량을 반영한 실제 소요시간을 돌려주면 된다.
        // 거리보다 시간이 진짜 비용이므로, 실제 서비스에서는 이쪽이 더 중요하다.
        virtual double minutes(const Location& a, const Location& b,
                               double averageSpeedKmh) const;
    };

    class StraightLineDistance : public DistanceProvider {
    public:
        std::string providerName() const override { return "직선 거리"; }
        double meters(const Location& a, const Location& b) const override;
    };

    // ---------- 경로 ----------

    // 한 집에 언제 도착해서 어떻게 됐는가
    struct StopVisit {
        std::size_t stopIndex = 0;
        TimeOfDay arriveAt;             // 도착 시각
        TimeOfDay leaveAt;              // 건네주고 떠난 시각
        double waitMinutes = 0.0;       // 너무 일찍 와서 기다린 시간
        double lateMinutes = 0.0;       // 늦은 시간 (0 이면 정시)

        // 가게를 떠나고 몇 분 만에 이 집에 닿았나.
        // 회차 배송에서는 이게 곧 "음식이 얼마나 식었나" 다.
        double minutesSinceDeparture = 0.0;

        bool isLate() const { return lateMinutes > 0.0; }
    };

    struct Route {
        std::vector<std::size_t> stopIndices;   // RoutePlanner::stops() 안에서의 순서
        std::vector<StopVisit> visits;          // stopIndices 와 같은 순서

        double meters = 0.0;            // 총 이동 거리
        double totalMinutes = 0.0;      // 출발부터 마지막까지 걸린 시간 (대기 포함)
        double waitMinutes = 0.0;       // 기다린 시간의 합
        double lateMinutes = 0.0;       // 늦은 시간의 합
        std::size_t lateCount = 0;      // 지각한 집의 수

        // 이 경로에서 가장 오래 들고 있던 음식이 몇 분이었나 (= 마지막 집)
        double worstMinutesOnRoad = 0.0;

        std::size_t size() const { return stopIndices.size(); }
        bool empty() const { return stopIndices.empty(); }

        // 시간 창을 하나도 어기지 않았는가
        bool isFeasible() const { return lateCount == 0; }
    };

    class RoutePlanner {
    public:
        RoutePlanner();

        // ---- 넣기 ----
        void setDepot(Location depot);          // 출발지 (가게). 좌표가 없으면 예외
        const Location& depot() const { return depot_; }

        void addStop(Stop stop);                // 좌표가 없는 곳은 예외
        void clearStops();
        const std::vector<Stop>& stops() const { return stops_; }
        bool hasTimeWindows() const;

        // ---- 설정 ----
        bool returnToDepot() const { return returnToDepot_; }
        void setReturnToDepot(bool value) { returnToDepot_ = value; }

        // 라이더 한 명이 한 번에 드는 최대 건수 (0 이면 제한 없음)
        int maxStopsPerRoute() const { return maxStopsPerRoute_; }
        void setMaxStopsPerRoute(int n);

        // 라이더가 가게를 떠나는 시각 (기본 11:30)
        const TimeOfDay& departureTime() const { return departure_; }
        void setDepartureTime(TimeOfDay t) { departure_ = t; }

        // 평균 이동 속도. 도심 이륜차 기준 20km/h 정도가 현실적이다
        // (신호와 정차까지 포함한 문앞-문앞 속도).
        double averageSpeedKmh() const { return speedKmh_; }
        void setAverageSpeedKmh(double kmh);

        // 1분 늦는 것을 몇 미터 더 도는 것과 같게 볼지 (기본 1000m/분).
        // 크게 잡을수록 돌아가더라도 정시 도착을 지키려 한다.
        double latePenaltyMetersPerMinute() const { return latePenalty_; }
        void setLatePenaltyMetersPerMinute(double meters);

        // 널이면 다시 직선 거리로 돌아간다. 수명은 꽂은 쪽이 관리한다.
        void setDistanceProvider(const DistanceProvider* provider);
        const DistanceProvider& distanceProvider() const { return *distance_; }

        // ---- 계산 ----
        Route planOne() const;                  // 한 명이 전부 도는 경로
        std::vector<Route> plan() const;        // 건수 제한에 맞춰 나눈 경로들
        Route naiveOrder() const;               // 넣은 순서 그대로 (개선 전후 비교용)

        // 임의의 순서를 그대로 평가한다 (거리, 도착 시각, 지각 등)
        Route evaluate(const std::vector<std::size_t>& order) const;

        double lengthOf(const std::vector<std::size_t>& order) const;

    private:
        Location depot_;
        std::vector<Stop> stops_;
        bool returnToDepot_ = true;
        int maxStopsPerRoute_ = 0;
        TimeOfDay departure_ = TimeOfDay(11, 30);
        double speedKmh_ = 20.0;
        double latePenalty_ = 1000.0;

        StraightLineDistance fallback_;
        const DistanceProvider* distance_;
    };

}

#endif
