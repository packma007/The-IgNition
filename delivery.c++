#include "delivery.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace domains {

    namespace {
        // 자정을 넘겨도 시각을 만들 수 있게 감아 준다
        TimeOfDay fromMinutes(double minutesOfDay) {
            long long m = static_cast<long long>(minutesOfDay + 0.5);
            m %= 24 * 60;
            if (m < 0) m += 24 * 60;
            return TimeOfDay(static_cast<int>(m / 60), static_cast<int>(m % 60));
        }
    }

    // ---------- TimeWindow ----------

    TimeWindow::TimeWindow(TimeOfDay earliest, TimeOfDay latest)
        : earliest(earliest), latest(latest), used(true) {
        if (latest < earliest)
            throw std::invalid_argument("latest must not be before earliest");
    }

    // ---------- Stop ----------

    Stop::Stop(std::string label, Location location)
        : label(std::move(label)), location(location) {
        if (!this->location.isSet())
            throw std::invalid_argument("stop location must be set");
    }

    Stop::Stop(std::string label, Location location, TimeWindow window)
        : label(std::move(label)), location(location), window(window) {
        if (!this->location.isSet())
            throw std::invalid_argument("stop location must be set");
    }

    // ---------- DistanceProvider ----------

    double DistanceProvider::minutes(const Location& a, const Location& b,
                                     double averageSpeedKmh) const {
        if (averageSpeedKmh <= 0.0)
            throw std::invalid_argument("averageSpeedKmh must be > 0");
        double km = meters(a, b) / 1000.0;
        return km / averageSpeedKmh * 60.0;
    }

    double StraightLineDistance::meters(const Location& a, const Location& b) const {
        return haversineMeters(a, b);
    }

    // ---------- RoutePlanner ----------

    RoutePlanner::RoutePlanner() : distance_(&fallback_) {}

    void RoutePlanner::setDepot(Location depot) {
        if (!depot.isSet())
            throw std::invalid_argument("depot location must be set");
        depot_ = depot;
    }

    void RoutePlanner::addStop(Stop stop) {
        if (!stop.location.isSet())
            throw std::invalid_argument("stop location must be set");
        if (stop.serviceMinutes < 0.0)
            throw std::invalid_argument("serviceMinutes must be >= 0");
        stops_.push_back(std::move(stop));
    }

    void RoutePlanner::clearStops() { stops_.clear(); }

    bool RoutePlanner::hasTimeWindows() const {
        for (std::size_t i = 0; i < stops_.size(); ++i)
            if (stops_[i].window.isSet()) return true;
        return false;
    }

    void RoutePlanner::setMaxStopsPerRoute(int n) {
        if (n < 0) throw std::invalid_argument("maxStopsPerRoute must be >= 0");
        maxStopsPerRoute_ = n;
    }

    void RoutePlanner::setAverageSpeedKmh(double kmh) {
        if (kmh <= 0.0) throw std::invalid_argument("averageSpeedKmh must be > 0");
        speedKmh_ = kmh;
    }

    void RoutePlanner::setLatePenaltyMetersPerMinute(double meters) {
        if (meters < 0.0) throw std::invalid_argument("late penalty must be >= 0");
        latePenalty_ = meters;
    }

    void RoutePlanner::setDistanceProvider(const DistanceProvider* provider) {
        distance_ = provider ? provider : static_cast<const DistanceProvider*>(&fallback_);
    }

    namespace {

        // 거리와 소요시간 표. 0번은 가게, 1..n 은 집.
        // 한 번만 재 둔다 - 도로 API 를 꽂으면 호출이 비싸지므로 이 캐시가 중요해진다.
        struct Matrix {
            std::size_t n = 0;
            std::vector<double> dist;      // (n+1) x (n+1) 미터
            std::vector<double> mins;      // (n+1) x (n+1) 분

            double d(std::size_t a, std::size_t b) const { return dist[a * (n + 1) + b]; }
            double t(std::size_t a, std::size_t b) const { return mins[a * (n + 1) + b]; }
        };

        Matrix buildMatrix(const Location& depot,
                           const std::vector<Stop>& stops,
                           const DistanceProvider& dp,
                           double speedKmh) {
            Matrix m;
            m.n = stops.size();
            std::size_t side = m.n + 1;
            m.dist.assign(side * side, 0.0);
            m.mins.assign(side * side, 0.0);

            std::vector<const Location*> pts;
            pts.reserve(side);
            pts.push_back(&depot);
            for (std::size_t i = 0; i < stops.size(); ++i) pts.push_back(&stops[i].location);

            for (std::size_t a = 0; a < side; ++a) {
                for (std::size_t b = 0; b < side; ++b) {
                    if (a == b) continue;
                    m.dist[a * side + b] = dp.meters(*pts[a], *pts[b]);
                    m.mins[a * side + b] = dp.minutes(*pts[a], *pts[b], speedKmh);
                }
            }
            return m;
        }

        struct Eval {
            double meters = 0.0;
            double totalMinutes = 0.0;
            double waitMinutes = 0.0;
            double lateMinutes = 0.0;
            std::size_t lateCount = 0;
            std::vector<StopVisit> visits;
        };

        // 순서를 그대로 따라가며 거리와 시각을 계산한다.
        // 시간 창이 붙으면 이게 핵심이다 - 순서를 바꾸면 뒤쪽 도착 시각이 전부 밀리므로
        // 시간 창이 없을 때처럼 바뀐 변 두 개만 보고 판단할 수 없다. 매번 끝까지 따라가야 한다.
        Eval run(const Matrix& m,
                 const std::vector<Stop>& stops,
                 const std::vector<std::size_t>& order,
                 bool returnToDepot,
                 double departMinutes,
                 bool wantVisits) {
            Eval e;
            if (order.empty()) return e;
            if (wantVisits) e.visits.reserve(order.size());

            std::size_t cur = 0;               // 가게
            double clock = departMinutes;

            for (std::size_t i = 0; i < order.size(); ++i) {
                std::size_t s = order[i];
                e.meters += m.d(cur, s + 1);
                clock    += m.t(cur, s + 1);

                const Stop& st = stops[s];
                double wait = 0.0, late = 0.0;

                if (st.window.isSet()) {
                    double early = static_cast<double>(st.window.earliest.minutesOfDay());
                    double last  = static_cast<double>(st.window.latest.minutesOfDay());
                    if (clock < early) { wait = early - clock; clock = early; }
                    if (clock > last)  { late = clock - last; }
                }

                StopVisit v;
                if (wantVisits) {
                    v.stopIndex   = s;
                    v.arriveAt    = fromMinutes(clock);
                    v.waitMinutes = wait;
                    v.lateMinutes = late;
                    v.minutesSinceDeparture = clock - departMinutes;
                }

                e.waitMinutes += wait;
                e.lateMinutes += late;
                if (late > 0.0) ++e.lateCount;

                clock += st.serviceMinutes;
                if (wantVisits) {
                    v.leaveAt = fromMinutes(clock);
                    e.visits.push_back(v);
                }
                cur = s + 1;
            }

            if (returnToDepot) {
                e.meters += m.d(cur, 0);
                clock    += m.t(cur, 0);
            }
            e.totalMinutes = clock - departMinutes;
            return e;
        }

        // 무엇이 "좋은 경로" 인가. 거리와 지각을 하나의 값으로 합친다.
        // 지각 1분을 몇 미터로 볼지는 사업이 정할 문제라 설정으로 뺐다.
        double costOf(const Eval& e, double latePenalty) {
            return e.meters + latePenalty * e.lateMinutes;
        }

        // 1단계: 가게에서 시작해, 가장 싸게 갈 수 있는 다음 집으로 옮겨 간다.
        // 시간 창이 있으면 "가까운 곳" 이 아니라 "가깝고 늦지 않을 곳" 을 고른다.
        std::vector<std::size_t> nearestNeighbor(const Matrix& m,
                                                 const std::vector<Stop>& stops,
                                                 double departMinutes,
                                                 double latePenalty,
                                                 double speedMetersPerMinute) {
            std::vector<std::size_t> order;
            if (m.n == 0) return order;

            std::vector<bool> used(m.n, false);
            std::size_t cur = 0;
            double clock = departMinutes;

            for (std::size_t step = 0; step < m.n; ++step) {
                std::size_t best = m.n;
                double bestCost = 0.0;

                for (std::size_t i = 0; i < m.n; ++i) {
                    if (used[i]) continue;
                    double arrive = clock + m.t(cur, i + 1);
                    double wait = 0.0, late = 0.0;

                    if (stops[i].window.isSet()) {
                        double early = static_cast<double>(stops[i].window.earliest.minutesOfDay());
                        double last  = static_cast<double>(stops[i].window.latest.minutesOfDay());
                        if (arrive < early) wait = early - arrive;
                        if (arrive > last)  late = arrive - last;
                    }

                    // 기다리는 시간도 손해이므로 거리로 환산해 더한다
                    double c = m.d(cur, i + 1)
                             + latePenalty * late
                             + wait * speedMetersPerMinute * 0.5;

                    if (best == m.n || c < bestCost) { best = i; bestCost = c; }
                }

                used[best] = true;
                order.push_back(best);

                double arrive = clock + m.t(cur, best + 1);
                if (stops[best].window.isSet()) {
                    double early = static_cast<double>(stops[best].window.earliest.minutesOfDay());
                    if (arrive < early) arrive = early;
                }
                clock = arrive + stops[best].serviceMinutes;
                cur = best + 1;
            }
            return order;
        }

        struct Opt {
            const Matrix* m;
            const std::vector<Stop>* stops;
            bool returnToDepot;
            double departMinutes;
            double latePenalty;

            double cost(const std::vector<std::size_t>& order) const {
                return costOf(run(*m, *stops, order, returnToDepot, departMinutes, false),
                              latePenalty);
            }
        };

        // 2단계-A: 2-opt. 경로가 스스로 교차하면 그 구간을 뒤집어 푼다.
        void twoOpt(const Opt& o, std::vector<std::size_t>& order) {
            if (order.size() < 3) return;
            const std::size_t n = order.size();
            double best = o.cost(order);
            bool improved = true;
            int guard = 0;

            while (improved && guard++ < 50) {
                improved = false;
                for (std::size_t i = 0; i < n - 1 && !improved; ++i) {
                    for (std::size_t j = i + 1; j < n && !improved; ++j) {
                        std::vector<std::size_t> cand = order;
                        for (std::size_t a = i, b = j; a < b; ++a, --b)
                            std::swap(cand[a], cand[b]);
                        double c = o.cost(cand);
                        if (c < best - 1e-9) { order = cand; best = c; improved = true; }
                    }
                }
            }
        }

        // 2단계-B: Or-opt. 연속한 1~3집을 통째로 다른 자리로 옮겨 본다.
        // 2-opt 가 못 푸는 "한 집만 엉뚱한 데 끼어 있는" 경우를 잡는다.
        void orOpt(const Opt& o, std::vector<std::size_t>& order) {
            if (order.size() < 3) return;
            double best = o.cost(order);
            bool improved = true;
            int guard = 0;

            while (improved && guard++ < 50) {
                improved = false;
                for (std::size_t len = 1; len <= 3 && !improved; ++len) {
                    for (std::size_t i = 0; i + len <= order.size() && !improved; ++i) {
                        std::vector<std::size_t> seg(
                            order.begin() + static_cast<std::ptrdiff_t>(i),
                            order.begin() + static_cast<std::ptrdiff_t>(i + len));
                        std::vector<std::size_t> rest = order;
                        rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(i),
                                   rest.begin() + static_cast<std::ptrdiff_t>(i + len));

                        for (std::size_t j = 0; j <= rest.size() && !improved; ++j) {
                            if (j == i) continue;
                            std::vector<std::size_t> cand = rest;
                            cand.insert(cand.begin() + static_cast<std::ptrdiff_t>(j),
                                        seg.begin(), seg.end());
                            double c = o.cost(cand);
                            if (c < best - 1e-9) { order = cand; best = c; improved = true; }
                        }
                    }
                }
            }
        }

        Route toRoute(const Matrix& m,
                      const std::vector<Stop>& stops,
                      const std::vector<std::size_t>& order,
                      bool returnToDepot,
                      double departMinutes) {
            Route r;
            r.stopIndices = order;
            Eval e = run(m, stops, order, returnToDepot, departMinutes, true);
            r.visits       = e.visits;
            r.meters       = e.meters;
            r.totalMinutes = e.totalMinutes;
            r.waitMinutes  = e.waitMinutes;
            r.lateMinutes  = e.lateMinutes;
            r.lateCount    = e.lateCount;
            for (std::size_t i = 0; i < r.visits.size(); ++i)
                if (r.visits[i].minutesSinceDeparture > r.worstMinutesOnRoad)
                    r.worstMinutesOnRoad = r.visits[i].minutesSinceDeparture;
            return r;
        }

    }

    Route RoutePlanner::evaluate(const std::vector<std::size_t>& order) const {
        for (std::size_t i = 0; i < order.size(); ++i)
            if (order[i] >= stops_.size())
                throw std::out_of_range("stop index is out of range");
        Matrix m = buildMatrix(depot_, stops_, *distance_, speedKmh_);
        return toRoute(m, stops_, order, returnToDepot_,
                       static_cast<double>(departure_.minutesOfDay()));
    }

    double RoutePlanner::lengthOf(const std::vector<std::size_t>& order) const {
        return evaluate(order).meters;
    }

    Route RoutePlanner::naiveOrder() const {
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < stops_.size(); ++i) order.push_back(i);
        return evaluate(order);
    }

    Route RoutePlanner::planOne() const {
        if (stops_.empty()) return Route();
        if (!depot_.isSet())
            throw std::runtime_error("depot 을 먼저 설정해야 합니다");

        Matrix m = buildMatrix(depot_, stops_, *distance_, speedKmh_);
        double depart = static_cast<double>(departure_.minutesOfDay());
        double metersPerMinute = speedKmh_ * 1000.0 / 60.0;

        Opt o;
        o.m = &m; o.stops = &stops_;
        o.returnToDepot = returnToDepot_;
        o.departMinutes = depart;
        o.latePenalty = latePenalty_;

        std::vector<std::size_t> order =
            nearestNeighbor(m, stops_, depart, latePenalty_, metersPerMinute);
        twoOpt(o, order);
        orOpt(o, order);
        twoOpt(o, order);          // Or-opt 로 자리가 바뀐 뒤 다시 한 번

        return toRoute(m, stops_, order, returnToDepot_, depart);
    }

    std::vector<Route> RoutePlanner::plan() const {
        std::vector<Route> out;
        if (stops_.empty()) return out;

        Route whole = planOne();
        if (maxStopsPerRoute_ <= 0 ||
            whole.stopIndices.size() <= static_cast<std::size_t>(maxStopsPerRoute_)) {
            out.push_back(whole);
            return out;
        }

        // 완성된 경로를 건수에 맞춰 잘라 나눈다 (route-first cluster-second).
        // 이미 가까운 집끼리 이어져 있으므로 자르면 자연스럽게 구역이 나뉜다.
        // 라이더가 여럿이면 각자 가게에서 같은 시각에 출발한다고 본다.
        Matrix m = buildMatrix(depot_, stops_, *distance_, speedKmh_);
        double depart = static_cast<double>(departure_.minutesOfDay());
        const std::size_t cap = static_cast<std::size_t>(maxStopsPerRoute_);

        Opt o;
        o.m = &m; o.stops = &stops_;
        o.returnToDepot = returnToDepot_;
        o.departMinutes = depart;
        o.latePenalty = latePenalty_;

        for (std::size_t i = 0; i < whole.stopIndices.size(); i += cap) {
            std::size_t end = i + cap;
            if (end > whole.stopIndices.size()) end = whole.stopIndices.size();
            std::vector<std::size_t> part(
                whole.stopIndices.begin() + static_cast<std::ptrdiff_t>(i),
                whole.stopIndices.begin() + static_cast<std::ptrdiff_t>(end));

            // 잘라낸 조각은 가게에서 새로 출발하므로 순서를 다시 다듬는다
            twoOpt(o, part);
            orOpt(o, part);
            out.push_back(toRoute(m, stops_, part, returnToDepot_, depart));
        }
        return out;
    }

}
