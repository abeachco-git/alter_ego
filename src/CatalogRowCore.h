#pragma once
#include <algorithm>
#include <cmath>
#include <string>
namespace AlterEgo::UI {
// ImGui 1.80 floors the cursor after ItemSize. Use an integral pitch
// consistently for drawing, hit testing, dummy rows and clipper seeks.
inline float CatalogRowPitch(float textHeight,float iconSize=16.f){
    return std::ceil(std::max(22.f,std::max(textHeight,iconSize)+6.f));
}
inline int CatalogGridColumns(float width,float size,float gap){
    if(width<=0||size<=0||gap<0)return 1;
    return std::max(1,static_cast<int>((width+gap)/(size+gap)));
}
// A fixed trusted Wiki search destination, not a guessed article title.
// Encode UTF-8 bytes; untrusted metadata cannot add URL parameters or paths.
inline std::string CatalogWikiSearchUrl(const std::string& name){
    if(name.empty())return {};
    std::string url="https://wiki.guildwars2.com/wiki/Special:Search?search=";
    static constexpr char hex[]="0123456789ABCDEF";
    for(unsigned char ch:name){
        if((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='~')url+=static_cast<char>(ch);
        else {url+='%';url+=hex[ch>>4];url+=hex[ch&15];}
    }
    return url;
}
// Tile layout uses a single native line per grid row; spacing is explicit.
inline float CatalogGridPitch(float tileHeight,float gap){return std::floor(tileHeight+gap);}
inline float CatalogCenteredScroll(int row,float pitch,float viewportHeight){
    return std::max(0.f,(static_cast<float>(row)+.5f)*pitch-viewportHeight*.5f);
}
}
