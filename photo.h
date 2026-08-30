#ifndef PHOTO
#define PHOTO
#include <string>
#include "datetime.h"

// 음식 사진 한 장.
//
// 이미지 자체는 들고 있지 않고 파일 경로만 가리킨다. 프로그램이 아는 것은
// "언제 찍혔는가" 뿐이고, 그 시각으로 어느 날 어느 끼니에 붙일지가 정해진다.
//
// 나중에 사진에서 음식을 알아보는 단계가 붙으면 그 결과가 note_ 자리에 들어가고,
// 거기서 Meal 을 만들어 같은 날에 넣으면 된다.
namespace domains {

    class Photo {
    public:
        // date / time 은 사진이 찍힌 달력상 날짜와 시각 (EXIF 촬영시각).
        // 파일이 실제로 있는지는 확인하지 않는다.
        Photo(std::string path, Date date, TimeOfDay time);

        const std::string& path() const { return path_; }
        const Date& date() const { return date_; }        // 달력상 찍은 날
        const TimeOfDay& time() const { return time_; }   // 찍은 시각

        // 사진에 대한 메모. 음식 인식 결과가 들어갈 자리이기도 하다.
        const std::string& note() const { return note_; }
        void setNote(std::string note) { note_ = std::move(note); }

        // 이 사진이 속하는 하루 (하루 경계 적용. 새벽에 찍은 건 전날로 간다)
        Date belongsTo(const DayBoundary& boundary = DayBoundary{}) const;

        // 두 시각이 windowMinutes 안쪽인가 (사진과 끼니를 짝지을 때 쓴다)
        bool isNear(const TimeOfDay& t, int windowMinutes) const;

    private:
        std::string path_;
        Date date_;
        TimeOfDay time_;
        std::string note_;
    };

}

#endif
