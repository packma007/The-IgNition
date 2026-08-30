#ifndef STORAGE
#define STORAGE
#include <iosfwd>
#include <string>
#include "calendar.h"

// 달력을 파일로 보관하고 다시 읽어오는 곳.
//
// 형식은 한 줄에 한 레코드인 텍스트다. 탭으로 칸을 나누고, 맨 앞 칸이 종류를 말한다.
// 사람이 열어서 읽을 수 있고, 필요하면 손으로 고칠 수도 있다.
//
//   IGNITION     1
//   BOUNDARY     4   0
//   DEFAULTGOAL  250 187.5   66.6
//   DAY          2026  6  14   250   187.5   66.6
//   MEAL         제육덮밥  1  12  30  1  95  30  22  1
//   PHOTO        IMG_0421.jpg   2026  6  14  12  41   점심
//   DAY          2026  6  15   ...
//
// MEAL / PHOTO 는 바로 앞의 DAY 에 붙는다.
//
// input.h 와 같은 이유로 스트림을 받는 함수를 따로 둔다.
// 테스트할 때 std::stringstream 을 넘기면 파일 없이 검증할 수 있다.
namespace domains {
namespace storage {

    // 형식 버전. 저장 형식을 바꾸면 올린다.
    extern const int kFormatVersion;

    // ---- 스트림 ----
    void write(const Calendar& calendar, std::ostream& out);

    // calendar 를 비우고 다시 채운다. 형식이 틀리면 몇 번째 줄인지 알려주며 예외.
    void read(Calendar& calendar, std::istream& in);

    // ---- 파일 ----
    // 성공하면 true. 실패하면 false 를 주고 error 에 이유를 담는다(널이면 안 담음).
    // 경로는 ASCII 로 두는 편이 안전하다 (좁은 문자열 경로는 MSVC 에서
    // UTF-8 이 아니라 시스템 코드페이지로 해석된다).
    bool save(const Calendar& calendar, const std::string& path,
              std::string* error = 0);

    bool load(Calendar& calendar, const std::string& path,
              std::string* error = 0);

}
}

#endif
