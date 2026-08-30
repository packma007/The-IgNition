#ifndef DISPATCH
#define DISPATCH
#include <cstddef>
#include <vector>
#include "datetime.h"
#include "delivery.h"
#include "location.h"

// 회차 배송.
//
// 주문이 들어올 때마다 한 건씩 나가는 방식이 아니다.
// 6시 땡 하면 그 회차 음식을 통째로 받아서 한 바퀴 돌고, 7시에 또 한 바퀴 돈다.
//
//   6:00 ─ 픽업 ─► A ─► B ─► C ─► 복귀
//   7:00 ─ 픽업 ─► D ─► E ─► 복귀
//   8:00 ─ 픽업 ─► F ─► G ─► H ─► 복귀
//
// 그래서 신경 쓸 숫자가 주문별 시간 창이 아니다. 두 가지다.
//
//   1. 음식을 받고 몇 분 만에 배달했나 (마지막 집 음식이 얼마나 식었나)
//      - 한 바퀴가 길어지면 뒤쪽 손님이 식은 음식을 받는다
//      - 이게 라이더를 몇 명 붙일지를 정한다
//
//   2. 다음 회차 출발 전에 돌아오나
//      - 6시 바퀴가 7시를 넘기면 7시 회차가 밀린다
//
// 라이더 수는 손으로 정하지 않고, 위 두 조건을 만족할 때까지 늘려서 찾는다.
namespace domains {

    // 배송 회차 하나
    struct Wave {
        TimeOfDay departure;              // 픽업하고 출발하는 시각
        std::vector<Route> routes;        // 라이더 한 명당 경로 하나

        std::size_t riderCount() const { return routes.size(); }
        std::size_t orderCount() const;
        double totalMeters() const;

        // 이 회차에서 가장 오래 들고 있던 음식이 몇 분이었나.
        // 라이더가 여럿이면 그중 최악을 본다.
        double worstMinutesOnRoad() const;

        // 이 회차의 마지막 배달 시각
        TimeOfDay lastArrival() const;

        // 모든 라이더가 돌아오는 시각 (복귀를 끄면 마지막 배달 시각)
        TimeOfDay lastReturn() const;

        // 기준 시간을 넘겨 식은 채로 도착한 건수
        std::size_t staleCount(double maxMinutesOnRoad) const;

        bool empty() const { return routes.empty(); }
    };

    class WaveDispatcher {
    public:
        WaveDispatcher();

        // ---- 넣기 ----
        void setDepot(Location depot);
        const Location& depot() const { return depot_; }

        // 회차를 만든다. 회차 번호를 돌려준다.
        std::size_t addWave(TimeOfDay departure);
        std::size_t waveCount() const { return waves_.size(); }
        const TimeOfDay& waveDeparture(std::size_t waveIndex) const;

        // 그 회차로 나갈 주문 하나
        void addOrder(std::size_t waveIndex, Stop stop);
        std::size_t orderCount(std::size_t waveIndex) const;
        const std::vector<Stop>& orders(std::size_t waveIndex) const;

        void clear();

        // ---- 설정 ----
        // 라이더 한 명이 한 번에 들 수 있는 건수 (기본 8건)
        int maxStopsPerRider() const { return maxStops_; }
        void setMaxStopsPerRider(int n);

        // 음식이 견디는 시간. 이걸 넘기면 식은 것으로 본다 (기본 40분).
        // 라이더를 몇 명 붙일지가 사실상 이 값으로 정해진다.
        double maxMinutesOnRoad() const { return maxOnRoad_; }
        void setMaxMinutesOnRoad(double minutes);

        // 쓸 수 있는 라이더 수의 상한 (0 이면 제한 없음).
        // 이 수로도 기준을 못 맞추면 맞출 수 있는 데까지만 하고 결과에 남긴다.
        int maxRiders() const { return maxRiders_; }
        void setMaxRiders(int n);

        double averageSpeedKmh() const { return speedKmh_; }
        void setAverageSpeedKmh(double kmh);

        bool returnToDepot() const { return returnToDepot_; }
        void setReturnToDepot(bool value) { returnToDepot_ = value; }

        void setDistanceProvider(const DistanceProvider* provider);

        // ---- 계산 ----
        Wave planWave(std::size_t waveIndex) const;
        std::vector<Wave> plan() const;

        // 그 회차에 라이더가 몇 명 필요한가 (기준을 만족하는 최소 인원)
        std::size_t ridersNeeded(std::size_t waveIndex) const;

        // 이 회차가 다음 회차 출발 전에 복귀하는가.
        // 마지막 회차이거나 회차가 하나뿐이면 항상 true.
        bool fitsBeforeNextWave(const Wave& wave, std::size_t waveIndex) const;

    private:
        struct WaveInput {
            TimeOfDay departure;
            std::vector<Stop> stops;
        };

        void checkWave(std::size_t waveIndex) const;
        Wave planWith(std::size_t waveIndex, std::size_t riders) const;

        Location depot_;
        std::vector<WaveInput> waves_;
        int maxStops_ = 8;
        double maxOnRoad_ = 40.0;
        int maxRiders_ = 0;
        double speedKmh_ = 20.0;
        bool returnToDepot_ = true;
        const DistanceProvider* distance_ = 0;
    };

}

#endif
