#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <limits>
#include <algorithm>

namespace PreviewSequence {
inline std::string Family(std::string_view id) {
    if (id.find("knife") != id.npos || id == "weapon_bayonet") return "knife";
    for (auto name : {"deagle","elite","fiveseven","glock","hkp2000","p250","tec9","usp_silencer","cz75a","revolver"})
        if (id == std::string("weapon_")+name) return "pistol";
    for (auto name : {"nova","xm1014","mag7","sawedoff"}) if (id == std::string("weapon_")+name) return "shotgun";
    for (auto name : {"awp","ssg08","scar20","g3sg1"}) if (id == std::string("weapon_")+name) return "sniper";
    for (auto name : {"mac10","mp9","mp7","mp5sd","ump45","p90","bizon"}) if (id == std::string("weapon_")+name) return "smg";
    if (id == "weapon_m249" || id == "weapon_negev") return "mg";
    return "rifle";
}
inline int Score(std::string name,std::string_view weapon) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(c >= 'A' && c <= 'Z' ? c+32 : c);});
    for (auto bad : {"crouch","duck","sit","intro","outro","walk","run","jump","death","inspect","arrival"})
        if (name.find(bad)!=name.npos) return -1;
    if (name.find("idle")==name.npos && name.find("stand")==name.npos && name.find("aim")==name.npos) return -1;
    const auto family=Family(weapon);
    int score=0;
    if (name.find(family)!=name.npos) score+=100;
    else {
        for (auto other : {"pistol","rifle","sniper","shotgun","smg","knife"})
            if (family!=other && name.find(other)!=name.npos) return -1;
    }
    auto short_name=weapon.substr(weapon.find('_')==weapon.npos ? 0 : weapon.find('_')+1);
    if (name.find(short_name)!=name.npos) score+=200;
    if (name.find("stand")!=name.npos) score+=30;
    if (name.find("idle")!=name.npos) score+=20;
    return score;
}
// Raw Source MDL header: local sequence count/index at 188/192. Each descriptor
// is 212 bytes; label offset is descriptor-relative at +4. No engine pointers.
inline int Select(const std::vector<std::uint8_t>& bytes,std::string_view weapon) {
    auto read=[&](size_t at){ std::int32_t value{}; if(at<=bytes.size() && bytes.size()-at>=4) std::memcpy(&value,bytes.data()+at,4); return value; };
    if (bytes.size()<196 || read(0)!=0x54534449) return -1;
    const int count=read(188),offset=read(192);
    if(count<=0 || count>8192 || offset<196 || static_cast<size_t>(offset)>bytes.size() || static_cast<size_t>(count)>(bytes.size()-static_cast<size_t>(offset))/212) return -1;
    int best=-1,best_score=-1;
    for(int i=0;i<count;++i){
        const size_t desc=static_cast<size_t>(offset)+static_cast<size_t>(i)*212;
        const int relative=read(desc+4);
        if(relative<=0 || static_cast<size_t>(relative)>=bytes.size()-desc) continue;
        const size_t label=desc+static_cast<size_t>(relative);
        const auto limit=(std::min)(bytes.size()-label,size_t{160});
        const auto* end=static_cast<const char*>(std::memchr(bytes.data()+label,0,limit));
        if(!end) continue;
        const auto* start=reinterpret_cast<const char*>(bytes.data()+label);
        const int score=Score(std::string(start,end),weapon);
        if(score>best_score){best_score=score;best=i;}
    }
    return best;
}
}
