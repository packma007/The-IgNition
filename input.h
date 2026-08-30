#ifndef INPUT
#define INPUT
#include <string>
#include <iosfwd>
#include "domains.h"
#include "user.h"

// 사용자에게서 값을 읽어오는 곳.
// std::cin을 직접 쓰지 않고 스트림을 인자로 받으므로,
// 테스트할 때는 std::istringstream을 넘겨서 자동으로 검증할 수 있다.
//   readUser(std::cin, std::cout);                  // 실제 사용
//   std::istringstream fake("김창업\n27\n1\n...");   // 테스트
//   readUser(fake, nullStream);
//
// 잘못된 값이면 다시 묻는다. 입력이 끊기면(EOF) std::runtime_error를 던진다.
namespace domains {
namespace input {

    // ---- 기본 타입 ----
    std::string readLine(std::istream& in, std::ostream& out,
                         const std::string& prompt);

    int readInt(std::istream& in, std::ostream& out,
                const std::string& prompt, int min, int max);

    long long readLongLong(std::istream& in, std::ostream& out,
                           const std::string& prompt, long long min, long long max);

    double readDouble(std::istream& in, std::ostream& out,
                      const std::string& prompt, double min, double max);

    bool readYesNo(std::istream& in, std::ostream& out,
                   const std::string& prompt);

    // ---- 도메인 객체 ----
    Gender readGender(std::istream& in, std::ostream& out);
    User readUser(std::istream& in, std::ostream& out);
    MenuPtr readMenu(std::istream& in, std::ostream& out);

    // 탄수화물 / 단백질 / 지방을 차례로 물어 menu에 넣는다. 0을 넣으면 건너뛴다.
    void readNutrients(std::istream& in, std::ostream& out, Menu& menu);

}
}

#endif
