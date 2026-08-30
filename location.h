#ifndef LOCATION
#define LOCATION
#include <string>

// 위치. 배달할 곳을 가리키는 데 쓴다.
// 영양이나 사용자에 대해서는 아무것도 모르는 순수 좌표 타입이다.
namespace domains {

    struct Location {
        double latitude  = 0.0;    // 위도 -90..90
        double longitude = 0.0;    // 경도 -180..180
        std::string address;       // 사람이 읽는 주소 (계산에는 쓰지 않는다)

        Location() = default;
        Location(double latitude, double longitude, std::string address = "");

        // 좌표가 들어 있는가.
        // (0, 0) 은 아프리카 앞바다 한가운데라서 실제 주소일 수 없다.
        // 그래서 "아직 안 넣음" 의 표시로 쓴다.
        bool isSet() const;
    };

    // 두 지점 사이의 대권 거리 (미터).
    //
    // 직선 거리이므로 실제 도로 거리보다 항상 짧다. 도심에서는 도로 거리가
    // 직선 거리의 1.2~1.4배쯤 된다. 어느 집이 더 가까운지 순서를 매기는 데는
    // 충분하지만, 화면에 "1.2km" 라고 찍을 값으로는 부족하다.
    // 실제 서비스에서는 DistanceProvider 를 상속해 도로 경로 API 를 꽂는다.
    double haversineMeters(const Location& a, const Location& b);

}

#endif
