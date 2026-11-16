#pragma once
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <cstdint>
namespace PreviewSkinState {
struct Finish { int paint{}, seed{}; float wear{}; std::uint64_t revision{}; };
inline std::mutex mutex;
inline std::map<std::string,Finish> teams[2];
inline std::uint64_t revision{};
inline void Set(bool ct,std::string_view item,int paint,int seed,float wear) {
    if(item.empty() || paint<0 || seed<0 || seed>1000 || !(wear>=0.f && wear<=1.f)) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto& value=teams[ct ? 0 : 1][std::string(item)];
    if(value.revision && value.paint==paint && value.seed==seed && value.wear==wear) return;
    value={paint,seed,wear,++revision};
}
inline Finish Get(bool ct,std::string_view item) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto& values=teams[ct ? 0 : 1]; const auto it=values.find(std::string(item));
    return it==values.end() ? Finish{} : it->second;
}
}
