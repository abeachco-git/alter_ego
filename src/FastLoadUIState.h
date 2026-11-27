#pragma once
#include <string>
#include <utility>
// Presentation policy only; GameMemory owns validation and execution.
namespace AlterEgo::FastLoadUI {
enum class Mode { Disabled, Enabled, Confirm };
inline const char* ModeName(Mode mode){
    switch(mode){
        case Mode::Disabled:return "disabled";
        case Mode::Confirm:return "confirm";
        default:return "enabled";
    }
}
inline Mode ParseMode(const std::string& value){
    if(value=="disabled")return Mode::Disabled;
    if(value=="confirm")return Mode::Confirm;
    return Mode::Enabled;
}
struct Confirmation {
    std::pair<std::string,std::string> target;
    std::string active,account,scope;
    bool needsOpen=false;
    bool Pending()const{return !target.first.empty();}
    void Clear(){*this={};}
    void Begin(std::pair<std::string,std::string> request,const std::string& current,
               const std::string& owner,const std::string& browsing){
        target=std::move(request);active=current;account=owner;scope=browsing;needsOpen=true;
    }
    bool Matches(Mode mode,const std::string& current,const std::string& owner,
                 const std::string& browsing)const{
        return mode==Mode::Confirm&&Pending()&&!active.empty()&&!account.empty()&&
               active==current&&account==owner&&scope==browsing;
    }
};
inline bool IsActive(const std::string& name,const std::string& account,
                     const std::string& current,const std::string& owner){
    return !owner.empty()&&name==current&&account==owner;
}
}
