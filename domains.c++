#include "domains.h"
#include <iostream>
#include <string>

namespace domains{
    int student::getIp(){
        return ip;
    }
    std::string student::getName(){
        return name;
    }
    std::string student::getPassword(){
        return password;
    }
    void student::setId(int idVal){
        ip = idVal;
    }
    void student::setName(std::string nameVal){
        name = nameVal;
    }
    void student::setPassword(std::string passVal){
        password = passVal;
    }
}
