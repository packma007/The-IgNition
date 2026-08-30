#include "dispatch.h"
#include <stdexcept>
#include <utility>

namespace domains {

    namespace {
        TimeOfDay fromMinutes(double minutesOfDay) {
            long long m = static_cast<long long>(minutesOfDay + 0.5);
            m %= 24 * 60;
            if (m < 0) m += 24 * 60;
            return TimeOfDay(static_cast<int>(m / 60), static_cast<int>(m % 60));
        }
    }

    // ---------- Wave ----------

    std::size_t Wave::orderCount() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < routes.size(); ++i) n += routes[i].size();
        return n;
    }

    double Wave::totalMeters() const {
        double m = 0.0;
        for (std::size_t i = 0; i < routes.size(); ++i) m += routes[i].meters;
        return m;
    }

    double Wave::worstMinutesOnRoad() const {
        double worst = 0.0;
        for (std::size_t i = 0; i < routes.size(); ++i) {
            const std::vector<StopVisit>& v = routes[i].visits;
            for (std::size_t k = 0; k < v.size(); ++k)
                if (v[k].minutesSinceDeparture > worst) worst = v[k].minutesSinceDeparture;
        }
        return worst;
    }

    TimeOfDay Wave::lastArrival() const {
        double best = -1.0;
        for (std::size_t i = 0; i < routes.size(); ++i) {
            const std::vector<StopVisit>& v = routes[i].visits;
            for (std::size_t k = 0; k < v.size(); ++k)
                if (v[k].minutesSinceDeparture > best) best = v[k].minutesSinceDeparture;
        }
        if (best < 0.0) return departure;
        return fromMinutes(departure.minutesOfDay() + best);
    }

    TimeOfDay Wave::lastReturn() const {
        double longest = 0.0;
        for (std::size_t i = 0; i < routes.size(); ++i)
            if (routes[i].totalMinutes > longest) longest = routes[i].totalMinutes;
        return fromMinutes(departure.minutesOfDay() + longest);
    }

    std::size_t Wave::staleCount(double maxMinutesOnRoad) const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < routes.size(); ++i) {
            const std::vector<StopVisit>& v = routes[i].visits;
            for (std::size_t k = 0; k < v.size(); ++k)
                if (v[k].minutesSinceDeparture > maxMinutesOnRoad) ++n;
        }
        return n;
    }

    // ---------- WaveDispatcher ----------

    WaveDispatcher::WaveDispatcher() {}

    void WaveDispatcher::setDepot(Location depot) {
        if (!depot.isSet())
            throw std::invalid_argument("depot location must be set");
        depot_ = depot;
    }

    std::size_t WaveDispatcher::addWave(TimeOfDay departure) {
        WaveInput w;
        w.departure = departure;
        waves_.push_back(w);
        return waves_.size() - 1;
    }

    void WaveDispatcher::checkWave(std::size_t waveIndex) const {
        if (waveIndex >= waves_.size())
            throw std::out_of_range("wave index is out of range");
    }

    const TimeOfDay& WaveDispatcher::waveDeparture(std::size_t waveIndex) const {
        checkWave(waveIndex);
        return waves_[waveIndex].departure;
    }

    void WaveDispatcher::addOrder(std::size_t waveIndex, Stop stop) {
        checkWave(waveIndex);
        if (!stop.location.isSet())
            throw std::invalid_argument("stop location must be set");
        waves_[waveIndex].stops.push_back(std::move(stop));
    }

    std::size_t WaveDispatcher::orderCount(std::size_t waveIndex) const {
        checkWave(waveIndex);
        return waves_[waveIndex].stops.size();
    }

    const std::vector<Stop>& WaveDispatcher::orders(std::size_t waveIndex) const {
        checkWave(waveIndex);
        return waves_[waveIndex].stops;
    }

    void WaveDispatcher::clear() { waves_.clear(); }

    void WaveDispatcher::setMaxStopsPerRider(int n) {
        if (n < 1) throw std::invalid_argument("maxStopsPerRider must be >= 1");
        maxStops_ = n;
    }

    void WaveDispatcher::setMaxMinutesOnRoad(double minutes) {
        if (minutes <= 0.0) throw std::invalid_argument("maxMinutesOnRoad must be > 0");
        maxOnRoad_ = minutes;
    }

    void WaveDispatcher::setMaxRiders(int n) {
        if (n < 0) throw std::invalid_argument("maxRiders must be >= 0");
        maxRiders_ = n;
    }

    void WaveDispatcher::setAverageSpeedKmh(double kmh) {
        if (kmh <= 0.0) throw std::invalid_argument("averageSpeedKmh must be > 0");
        speedKmh_ = kmh;
    }

    void WaveDispatcher::setDistanceProvider(const DistanceProvider* provider) {
        distance_ = provider;
    }

    // 라이더를 정확히 riders 명 쓴다고 보고 그 회차를 짠다
    Wave WaveDispatcher::planWith(std::size_t waveIndex, std::size_t riders) const {
        const WaveInput& in = waves_[waveIndex];

        Wave w;
        w.departure = in.departure;
        if (in.stops.empty() || riders == 0) return w;

        RoutePlanner rp;
        rp.setDepot(depot_);
        rp.setDepartureTime(in.departure);
        rp.setAverageSpeedKmh(speedKmh_);
        rp.setReturnToDepot(returnToDepot_);
        rp.setDistanceProvider(distance_);

        for (std::size_t i = 0; i < in.stops.size(); ++i) rp.addStop(in.stops[i]);

        // riders 명으로 나누려면 한 명이 최대 몇 건을 들어야 하는지
        std::size_t cap = (in.stops.size() + riders - 1) / riders;
        rp.setMaxStopsPerRoute(static_cast<int>(cap));

        w.routes = rp.plan();
        return w;
    }

    std::size_t WaveDispatcher::ridersNeeded(std::size_t waveIndex) const {
        checkWave(waveIndex);
        const std::size_t n = waves_[waveIndex].stops.size();
        if (n == 0) return 0;

        // 적재 한도에서 오는 최소 인원부터 시작한다
        std::size_t start = (n + static_cast<std::size_t>(maxStops_) - 1)
                          / static_cast<std::size_t>(maxStops_);
        if (start == 0) start = 1;

        std::size_t cap = n;
        if (maxRiders_ > 0 && static_cast<std::size_t>(maxRiders_) < cap)
            cap = static_cast<std::size_t>(maxRiders_);
        if (start > cap) start = cap;

        // 음식이 식지 않을 때까지 한 명씩 늘려 본다.
        // 사람을 더 붙이면 한 바퀴가 짧아지고, 짧아지면 음식이 덜 식는다.
        for (std::size_t r = start; r <= cap; ++r) {
            Wave w = planWith(waveIndex, r);
            if (w.staleCount(maxOnRoad_) == 0) return r;
        }
        return cap;   // 여기까지 왔으면 인원 상한으로도 못 맞춘다
    }

    Wave WaveDispatcher::planWave(std::size_t waveIndex) const {
        checkWave(waveIndex);
        if (!depot_.isSet())
            throw std::runtime_error("depot 을 먼저 설정해야 합니다");
        return planWith(waveIndex, ridersNeeded(waveIndex));
    }

    std::vector<Wave> WaveDispatcher::plan() const {
        std::vector<Wave> out;
        out.reserve(waves_.size());
        for (std::size_t i = 0; i < waves_.size(); ++i)
            out.push_back(planWave(i));
        return out;
    }

    bool WaveDispatcher::fitsBeforeNextWave(const Wave& wave, std::size_t waveIndex) const {
        checkWave(waveIndex);
        if (waveIndex + 1 >= waves_.size()) return true;   // 마지막 회차
        if (wave.empty()) return true;

        double back = wave.departure.minutesOfDay();
        double longest = 0.0;
        for (std::size_t i = 0; i < wave.routes.size(); ++i)
            if (wave.routes[i].totalMinutes > longest) longest = wave.routes[i].totalMinutes;
        back += longest;

        return back <= static_cast<double>(waves_[waveIndex + 1].departure.minutesOfDay());
    }

}
