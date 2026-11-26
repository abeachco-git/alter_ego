#pragma once
#include "UntrustedInput.h"
#include "ExternalResourceSafety.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
namespace AlterEgo::WikiArtwork {
using Json=nlohmann::json;
// Only a complete, validated API response may establish absence.
inline Json Query(int status,const std::string& body){
    if(status!=200)throw std::runtime_error(status?"Wiki HTTP "+std::to_string(status):"Wiki request timed out or failed");
    auto j=Untrusted::Parse(body,Untrusted::Wiki);
    if(!j.is_object()||j.contains("error")||!j.contains("query")||!j["query"].is_object())
        throw std::runtime_error("Invalid Wiki response");
    return j;
}
inline const Json& SinglePage(const Json& j){
    const auto& pages=j.at("query").at("pages");
    if(!pages.is_object()||pages.size()!=1||!pages.begin()->is_object())
        throw std::runtime_error("Unexpected Wiki page response");
    const auto& page=*pages.begin();
    if(page.contains("invalid")||(!page.contains("missing")&&(!page.contains("pageid")||!page.at("pageid").is_number_integer())))
        throw std::runtime_error("Unverified Wiki page");
    return page;
}
inline std::string Lower(std::string s){
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    std::replace(s.begin(),s.end(),'_',' ');return s;
}
inline std::string ImageUrl(const Json& j){
    const auto& page=SinglePage(j);
    if(page.contains("missing"))return {};
    const auto& info=page.at("imageinfo");
    if(!info.is_array()||info.empty())throw std::runtime_error("Missing Wiki image information");
    auto url=info.at(0).at("url").get<std::string>();
    if(!ExternalResource::ArtworkUrl(url))throw std::runtime_error("Unapproved Wiki artwork URL");
    return url;
}
inline std::string BestFile(const Json& j,const std::string& skinName){
    const auto& page=SinglePage(j);
    if(page.contains("missing"))return {};
    if(page.contains("categories")){
        if(!page.at("categories").is_array())throw std::runtime_error("Invalid Wiki categories");
        for(const auto& cat:page.at("categories")){
            const auto title=Lower(cat.at("title").get<std::string>());
            if(title.find("disambig")!=std::string::npos||title.find("container")!=std::string::npos||title.find("choice")!=std::string::npos)return {};
        }
    }
    std::string best;int bestScore=0;
    if(page.contains("images")){
        if(!page.at("images").is_array())throw std::runtime_error("Invalid Wiki images");
        for(const auto& image:page.at("images")){
            auto title=image.at("title").get<std::string>();auto name=Lower(title);
            if(name.rfind("file:",0)!=0||name.find("icon")!=std::string::npos)continue;
            auto ext=name.rfind('.');
            if(ext==std::string::npos)continue;
            const auto suffix=name.substr(ext);
            if(suffix!=".jpg"&&suffix!=".jpeg"&&suffix!=".png")continue;
            const auto stem=name.substr(5,ext-5);
            int score=suffix==".png"?1:2;
            if(stem==Lower(skinName))score+=10;
            if(score>bestScore){bestScore=score;best=title;}
        }
    }
    if(best.empty()&&j.contains("continue"))throw std::runtime_error("Incomplete Wiki image list");
    return best;
}
}
