#pragma once
#include <string_view>
namespace WeaponGlyph {
inline bool EqualName(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){
        const auto lower=[](char c){return c>='A' && c<='Z' ? static_cast<char>(c+('a'-'A')) : c;};
        if(lower(a[i])!=lower(b[i]))return false;
    }
    return true;
}
inline const char* Get(std::string_view name) {
    if(name.substr(0,7)=="weapon_")name.remove_prefix(7);
    struct Pair{std::string_view name;const char* glyph;};
    static constexpr Pair map[]={
        {"deagle","A"},{"elite","B"},{"fiveseven","C"},{"glock","D"},{"hkp2000","E"},{"p250","F"},{"usp_silencer","G"},{"tec9","H"},{"cz75a","I"},{"revolver","J"},
        {"mac10","K"},{"ump45","L"},{"bizon","M"},{"mp7","N"},{"mp9","O"},{"p90","P"},{"galilar","Q"},{"famas","R"},{"m4a1","S"},{"m4a1_silencer","T"},{"aug","U"},{"sg556","V"},{"ak47","W"},{"g3sg1","X"},{"scar20","Y"},{"awp","Z"},
        {"ssg08","a"},{"xm1014","b"},{"sawedoff","c"},{"mag7","d"},{"nova","e"},{"negev","f"},{"m249","g"},{"taser","h"},
        {"flashbang","i"},{"hegrenade","j"},{"smokegrenade","k"},{"molotov","l"},{"decoy","m"},{"incgrenade","n"},{"c4","o"}};
    for(const auto& p:map)if(EqualName(name,p.name))return p.glyph;
    if(name.find("Flash")!=name.npos)return "i";
    if(name.find("Smoke")!=name.npos)return "k";
    if(name.find("Molotov")!=name.npos || name=="Inferno")return "l";
    if(name.find("Incendiary")!=name.npos)return "n";
    if(name.find("Decoy")!=name.npos)return "m";
    if(name.find("Grenade")!=name.npos)return "j";
    if(name=="C4" || name=="PlantedC4")return "o";
    if(name=="AK47")return "W";
    if(name=="AWP")return "Z";
    return "";
}
}
