#include "location.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace domains {

    namespace {
        const double kEarthRadiusM = 6371000.0;
        const double kPi = 3.14159265358979323846;
        double toRad(double deg) { return deg * kPi / 180.0; }
    }

    Location::Location(double latitude, double longitude, std::string address)
        : latitude(latitude), longitude(longitude), address(std::move(address)) {
        if (latitude < -90.0 || latitude > 90.0)
            throw std::invalid_argument("latitude must be -90..90");
        if (longitude < -180.0 || longitude > 180.0)
            throw std::invalid_argument("longitude must be -180..180");
    }

    bool Location::isSet() const {
        return std::fabs(latitude) > 1e-9 || std::fabs(longitude) > 1e-9;
    }

    double haversineMeters(const Location& a, const Location& b) {
        double dLat = toRad(b.latitude - a.latitude);
        double dLon = toRad(b.longitude - a.longitude);
        double la1 = toRad(a.latitude);
        double la2 = toRad(b.latitude);

        double h = std::sin(dLat / 2) * std::sin(dLat / 2)
                 + std::cos(la1) * std::cos(la2)
                 * std::sin(dLon / 2) * std::sin(dLon / 2);
        return 2.0 * kEarthRadiusM * std::asin(std::sqrt(h < 1.0 ? h : 1.0));
    }

}
