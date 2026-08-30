#include "photo.h"
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace domains {

    Photo::Photo(std::string path, Date date, TimeOfDay time)
        : path_(std::move(path)), date_(date), time_(time) {
        if (path_.empty())
            throw std::invalid_argument("photo path must not be empty");
    }

    Date Photo::belongsTo(const DayBoundary& boundary) const {
        return boundary.dateFor(date_, time_);
    }

    bool Photo::isNear(const TimeOfDay& t, int windowMinutes) const {
        if (windowMinutes < 0)
            throw std::invalid_argument("windowMinutes must be >= 0");
        int diff = std::abs(time_.minutesOfDay() - t.minutesOfDay());
        return diff <= windowMinutes;
    }

}
