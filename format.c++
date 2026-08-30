#include "format.h"
#include <cmath>

namespace domains {

    std::string trimZeros(double v) {
        std::string s = std::to_string(v);
        std::size_t dot = s.find('.');
        if (dot == std::string::npos) return s;
        std::size_t last = s.find_last_not_of('0');
        if (last == dot) last = dot - 1;
        return s.substr(0, last + 1);
    }

    std::string roundTo(double v, int digits) {
        double f = std::pow(10.0, digits);
        return trimZeros(std::round(v * f) / f);
    }

}
