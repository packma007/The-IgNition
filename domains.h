#ifndef DOMAINS
#define DOMAINS
#include <string>
namespace domains {
    class student{
        private:
        int ip;
        std::string name;
        std::string password;
        public:
        //constructor
        student():ip(0), name("익명1"), password("password"){}
        //getters, setters
        int getIp();
        std::string getName();
        std::string getPassword();
        void setId(int idVal);
        void setName(std::string nameVal);
        void setPassword(std::string passVal);
        
    };
    class weeklyMenu{
        private:
        public:
    };
    class MenuSet{};
    class Order{};
    class PickupSpot{};

}


#endif