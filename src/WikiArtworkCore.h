#pragma once
#include "UntrustedInput.h"
#include "ExternalResourceSafety.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include "WikiFreshness.h"
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
inline std::string Trim(std::string s){
    auto space=[](unsigned char c){return std::isspace(c)!=0;};
    while(!s.empty()&&space(s.back()))s.pop_back();
    auto i=std::find_if_not(s.begin(),s.end(),space);s.erase(s.begin(),i);return s;
}
inline bool PlainTitle(const std::string& s){
    return !s.empty()&&Untrusted::DisplayText(s,false,512)&&s.find_first_of("{}[]<>|#") == std::string::npos;
}
inline std::string FileTitle(std::string s){
    s=Trim(std::move(s));
    if(Lower(s).rfind("file:",0)==0)s=s.substr(5);
    if(!PlainTitle(s))return {};
    const auto name=Lower(s);const auto dot=name.rfind('.');
    if(dot==std::string::npos)return {};
    const auto ext=name.substr(dot);
    if(ext!=".jpg"&&ext!=".jpeg"&&ext!=".png")return {};
    if(name.find(" icon")!=std::string::npos||name.rfind("icon ",0)==0)return {};
    return "File:"+s;
}
inline std::string Stem(const std::string& title){
    auto s=Lower(title);
    if(s.rfind("file:",0)==0)s=s.substr(5);
    auto dot=s.rfind('.');return dot==std::string::npos?s:s.substr(0,dot);
}
inline bool EligiblePage(const Json& page){
    if(page.contains("missing"))return false;
    if(page.contains("categories")){
        if(!page.at("categories").is_array())throw std::runtime_error("Invalid Wiki categories");
        for(const auto& cat:page.at("categories")){
            const auto title=Lower(cat.at("title").get<std::string>());
            if(title.find("disambig")!=std::string::npos||title.find("container")!=std::string::npos||title.find("choice")!=std::string::npos)return false;
        }
    }
    return true;
}
struct Gallery {std::vector<std::string> files;std::string set;};
// Literal gallery fields and <gallery> entries only; no template expansion,
// guessed file URLs, or arbitrary links from prose. Armor-set names must come
// from the skin page's infobox/gallery parameter.
inline Gallery ReadGallery(const std::string& text){
    Gallery g;std::istringstream in(text);std::string line;bool inGallery=false;
    bool infobox=false;int depth=0;
    auto add=[&](std::string s){auto t=FileTitle(std::move(s));if(!t.empty()&&g.files.size()<64&&std::find(g.files.begin(),g.files.end(),t)==g.files.end())g.files.push_back(std::move(t));};
    while(std::getline(in,line)){
        line=Trim(std::move(line));auto low=Lower(line);
        if(low.rfind("<gallery",0)==0){inGallery=true;continue;}
        if(low.rfind("</gallery",0)==0){inGallery=false;continue;}
        if(inGallery){add(line.substr(0,line.find('|')));continue;}
        // A compact Armor gallery invocation may keep the set on the
        // opening line. Interpret only its literal set field.
        if(low.rfind("{{armor gallery",0)==0&&g.set.empty()){
            const auto pipe=low.find('|');
            if(pipe!=std::string::npos){
                auto rest=line.substr(pipe+1);
                const auto eq=rest.find('=');
                if(eq!=std::string::npos&&Lower(Trim(rest.substr(0,eq)))=="set"){
                    auto value=Trim(rest.substr(eq+1));auto end=value.find_first_of("|}");
                    if(end!=std::string::npos)value=Trim(value.substr(0,end));
                    if(PlainTitle(value))g.set=value;
                }
            }
        }
        if(!infobox&&(low.rfind("{{skin infobox",0)==0||low.rfind("{{armor infobox",0)==0||low.rfind("{{item infobox",0)==0||low.rfind("{{weapon infobox",0)==0||low.rfind("{{armor gallery",0)==0)){
            infobox=true;depth=0;
        }
        if(infobox){
            if(!line.empty()&&line.front()=='|'&&depth==1){
                auto eq=line.find('=');
                if(eq!=std::string::npos){
                    auto key=Lower(Trim(line.substr(1,eq-1)));auto value=Trim(line.substr(eq+1));
                    if(key.rfind("gallery",0)==0&&key.size()>7&&std::all_of(key.begin()+7,key.end(),[](unsigned char c){return std::isdigit(c)!=0;}))add(value);
                    if(key=="set"&&g.set.empty()){
                        if(value.rfind("[[",0)==0&&value.size()>4&&value.substr(value.size()-2)=="]]"){
                            value=value.substr(2,value.size()-4);value=value.substr(0,value.find('|'));
                        }
                        if(PlainTitle(value))g.set=value;
                    }
                }
            }
            for(size_t i=0;i+1<line.size();++i){
                if(line.compare(i,2,"{{")==0){++depth;++i;}
                else if(line.compare(i,2,"}}")==0){--depth;++i;}
            }
            if(depth<=0)infobox=false;
        }
    }
    return g;
}
inline int RelatedScore(const std::string& stem,const std::string& base){
    if(base.empty())return 0;
    if(stem==base)return 30;
    if(stem.rfind(base+" ",0)!=0)return 0;
    const auto suffix=stem.substr(base.size()+1);
    // Documented gallery suffixes, not arbitrary similarly named items.
    const bool race=suffix.rfind("human ",0)==0||suffix.rfind("asura ",0)==0||suffix.rfind("charr ",0)==0||suffix.rfind("norn ",0)==0||suffix.rfind("sylvari ",0)==0;
    if(race&&(suffix.find(" front")!=std::string::npos||suffix.find(" back")!=std::string::npos||suffix.find(" side")!=std::string::npos)){
        return 10+(suffix.find(" front")!=std::string::npos?4:0)+(suffix.rfind("human ",0)==0?2:0);
    }
    if(suffix=="drawn"||suffix=="stowed"||suffix=="wielded"||suffix=="gallery"||suffix=="front"||suffix=="back"||suffix=="side")return 8;
    return 0;
}
inline std::vector<std::string> RankedFiles(const Json& j,const std::string& skinName,const Gallery& gallery={}){
    const auto& page=SinglePage(j);
    if(!EligiblePage(page))return {};
    std::vector<std::pair<int,std::string>> scored;
    const auto skin=Lower(skinName),set=Lower(gallery.set);
    auto add=[&](std::string title,int priority){
        title=FileTitle(std::move(title));if(title.empty()||priority<=0)return;
        if(Lower(title).size()>=4&&Lower(title).substr(Lower(title).size()-4)==".jpg")++priority;
        for(auto& e:scored)if(Lower(e.second)==Lower(title)){e.first=std::max(e.first,priority);return;}
        scored.emplace_back(priority,std::move(title));
    };
    for(const auto& file:gallery.files)add(file,100);
    if(page.contains("images")){
        if(!page.at("images").is_array())throw std::runtime_error("Invalid Wiki images");
        for(const auto& image:page.at("images")){
            auto title=image.at("title").get<std::string>();auto stem=Stem(title);
            const auto exact=RelatedScore(stem,skin),associated=RelatedScore(stem,set);
            add(title,exact?40+exact:associated?10+associated:0);
        }
    }
    if(scored.empty()&&gallery.set.empty()&&j.contains("continue"))throw std::runtime_error("Incomplete Wiki image list");
    std::stable_sort(scored.begin(),scored.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    std::vector<std::string> out;for(auto& e:scored)out.push_back(std::move(e.second));return out;
}
inline std::string BestFile(const Json& j,const std::string& skinName){
    auto files=RankedFiles(j,skinName);return files.empty()?std::string{}:files.front();
}
inline constexpr int SelectionVersion=3;
struct Source {
    std::string file,url,page,revision;
    int64_t verified=0;
    int version=0;
};
inline bool SafeRevision(const std::string& rev){
    return !rev.empty()&&rev.size()<=64&&std::all_of(rev.begin(),rev.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z');});
}
inline bool PreviewSize(uint32_t w,uint32_t h){
    return ExternalResource::Dimensions(w,h)&&std::max(w,h)>=128&&std::min(w,h)>=24;
}
inline bool ValidSource(const Source& s){
    return !FileTitle(s.file).empty()&&PlainTitle(s.page)&&SafeRevision(s.revision)&&
        ExternalResource::ArtworkUrl(s.url)&&ExternalResource::Classify(s.url)==ExternalResource::Origin::Wiki&&s.verified>0&&s.version>0;
}
inline Source ReadSource(const Json& j){
    Source s{j.at("file").get<std::string>(),j.at("url").get<std::string>(),j.at("page").get<std::string>(),
        j.at("revision").get<std::string>(),j.at("verified").get<int64_t>(),j.at("version").get<int>()};
    if(!ValidSource(s))throw std::runtime_error("Invalid preview provenance");return s;
}
inline Json SourceJson(const Source& s){
    return {{"file",s.file},{"url",s.url},{"page",s.page},{"revision",s.revision},{"verified",s.verified},{"version",s.version}};
}
inline bool Due(const Source& s,int64_t now){
    return s.version!=SelectionVersion||s.verified<=0||s.verified>now||now-s.verified>=WikiFreshness::ArtworkInterval;
}
inline std::optional<Source> ImageSource(const Json& j,const std::string& matchedPage,int64_t now){
    const auto& page=SinglePage(j);if(page.contains("missing"))return std::nullopt;
    const auto url=ImageUrl(j);const auto& i=page.at("imageinfo").at(0);
    const auto w=i.at("width").get<int64_t>(),h=i.at("height").get<int64_t>();
    if(w<=0||h<=0||w>8192||h>8192||!ExternalResource::Dimensions(static_cast<uint32_t>(w),static_cast<uint32_t>(h)))throw std::runtime_error("Invalid preview dimensions");
    if(!PreviewSize(static_cast<uint32_t>(w),static_cast<uint32_t>(h)))return std::nullopt;
    Source s{page.at("title").get<std::string>(),url,matchedPage,Lower(i.at("sha1").get<std::string>()),now,SelectionVersion};
    if(!ValidSource(s))throw std::runtime_error("Invalid Wiki preview source");
    return s;
}
inline bool SameRevision(const Source& a,const Source& b){return a.revision==b.revision&&!a.revision.empty();}
}
