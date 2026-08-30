#ifndef STORAGE
#define STORAGE
#include <iosfwd>
#include <string>
#include "calendar.h"
#include "weeklymenu.h"

// 달력과 주간 메뉴판을 파일로 보관하고 다시 읽어오는 곳.
//
// 형식은 한 줄에 한 레코드인 텍스트다. 탭으로 칸을 나누고, 맨 앞 칸이 종류를 말한다.
// 사람이 열어서 읽을 수 있고, 필요하면 손으로 고칠 수도 있다.
//
//   IGNITION     6
//   USER         김창업 27 0 a@b.com 72 178 18 2 37.5 127.03 사무실
//   BOUNDARY     4   0
//   DEFAULTGOAL  250 187.5   66.6
//   WEEKMENU     2026  8  30                       <- 그 주의 일요일
//   WMENU        제육덮밥  인분  9000  0  1  1  0    <- 낱개로 파는 메뉴
//   WNUTRIENT    0   95
//   WNUTRIENT    1   30
//   WMENU        현미밥    g     10    1  50  400 10 <- 무게로 파는 메뉴
//   WNUTRIENT    0   0.35
//   DAY          2026  6  14   250   187.5   66.6
//   MEAL         제육덮밥  1  12  30  1  95  30  22  1
//   PHOTO        IMG_0421.jpg   2026  6  14  12  41   점심
//   DAY          2026  6  15   ...
//
// WNUTRIENT 는 바로 앞의 WMENU 에, WMENU 는 바로 앞의 WEEKMENU 에 붙는다.
// MEAL / PHOTO 는 바로 앞의 DAY 에 붙는다.
// USER 는 파일 전체에 하나이며, 칸 순서는
//   이름 나이 성별(0남/1여/2기타) 이메일 몸무게 키 체지방률
//   활동량(0~4) 위도 경도 주소
// 이다. 체지방률의 0 은 "모름" 이고, 좌표 (0,0) 은 "배달지 없음" 이다.
//
// 메뉴판을 왜 같이 넣는가: 지난 주 기록의 "제육덮밥 1.5인분" 이 무엇이었는지는
// 그때의 메뉴판이 있어야 알 수 있다. 기록만 남기고 메뉴판을 버리면
// 다음 주에 메뉴가 바뀌는 순간 지난 기록을 해석할 수 없게 된다.
//
// input.h 와 같은 이유로 스트림을 받는 함수를 따로 둔다.
// 테스트할 때 std::stringstream 을 넘기면 파일 없이 검증할 수 있다.
namespace domains {
namespace storage {

    // 형식 버전. 저장 형식을 바꾸면 올린다.
    extern const int kFormatVersion;

    // ---- 스트림 ----
    // 사용자까지 함께 다루는 쪽이 가장 온전한 형태다.
    //
    // 사용자를 왜 같이 넣는가: 활동량과 몸 정보가 있어야 다시 켰을 때
    // 목표를 새로 계산할 수 있다. 목표를 g 으로만 저장해 두면 몸무게가 바뀌어도
    // 지난주에 계산해 둔 숫자가 그대로 따라다닌다.
    //
    // user 가 널이면 USER 줄을 적지 않는다.
    void write(const Calendar& calendar, const MenuBook& menus,
               const User* user, std::ostream& out);

    // calendar / menus 를 비우고 다시 채운다. 형식이 틀리면 몇 번째 줄인지 알려주며 예외.
    //
    // user 가 널이 아니면 USER 줄을 읽어 담는다. 파일에 USER 줄이 없으면 널이 된다
    // (v4 이하의 옛 파일). 널을 넘기면 USER 줄은 검사만 하고 버린다 -
    // 형식이 틀린 줄은 누가 읽든 예외가 되어야 하기 때문이다.
    void read(Calendar& calendar, MenuBook& menus,
              UserPtr* user, std::istream& in);

    // 사용자가 필요 없을 때
    void write(const Calendar& calendar, const MenuBook& menus, std::ostream& out);
    void read(Calendar& calendar, MenuBook& menus, std::istream& in);

    // 메뉴판도 필요 없을 때. 쓸 때는 안 적고, 읽을 때는 WEEKMENU 를 건너뛴다.
    void write(const Calendar& calendar, std::ostream& out);
    void read(Calendar& calendar, std::istream& in);

    // ---- 파일 ----
    // 성공하면 true. 실패하면 false 를 주고 error 에 이유를 담는다(널이면 안 담음).
    // 경로는 ASCII 로 두는 편이 안전하다 (좁은 문자열 경로는 MSVC 에서
    // UTF-8 이 아니라 시스템 코드페이지로 해석된다).
    bool save(const Calendar& calendar, const MenuBook& menus, const User* user,
              const std::string& path, std::string* error = 0);

    bool load(Calendar& calendar, MenuBook& menus, UserPtr* user,
              const std::string& path, std::string* error = 0);

    bool save(const Calendar& calendar, const MenuBook& menus,
              const std::string& path, std::string* error = 0);

    bool load(Calendar& calendar, MenuBook& menus,
              const std::string& path, std::string* error = 0);

    bool save(const Calendar& calendar, const std::string& path,
              std::string* error = 0);

    bool load(Calendar& calendar, const std::string& path,
              std::string* error = 0);

}
}

#endif
