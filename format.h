#ifndef FORMAT
#define FORMAT
#include <string>

namespace domains {

    // 숫자를 사람이 읽기 좋게 다듬는 헬퍼들
    // (domains.c++ / user.c++ 양쪽에서 공용으로 쓴다)

    // 소수점 뒤 불필요한 0 제거 ("70.500000" -> "70.5", "70.000000" -> "70")
    std::string trimZeros(double v);

    // 소수점 아래 digits 자리에서 반올림한 뒤 trimZeros 적용 (roundTo(22.68, 1) -> "22.7")
    std::string roundTo(double v, int digits);

}

#endif
