#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Small KeyValues scanner for repeated schema sections. Tokens reference the
// input; metadata copied out does not retain the input buffer.
namespace SkinSchema {
struct Tokenizer {
    std::string_view text; size_t pos{};
    std::string_view next() {
        for (;;) {
            while (pos < text.size() && static_cast<unsigned char>(text[pos]) <= ' ') ++pos;
            if (pos + 1 < text.size() && text[pos] == '/' && text[pos+1] == '/') {
                while (pos < text.size() && text[pos] != '\n') ++pos;
            } else break;
        }
        if (pos >= text.size()) return {};
        if (text[pos] == '{' || text[pos] == '}') return text.substr(pos++, 1);
        if (text[pos] == '"') {
            const auto start = ++pos;
            while (pos < text.size() && text[pos] != '"') {
                if (text[pos] == '\\' && pos+1 < text.size()) ++pos;
                ++pos;
            }
            auto value = text.substr(start, pos-start);
            if (pos < text.size()) ++pos;
            return value;
        }
        const auto start = pos;
        while (pos < text.size() && static_cast<unsigned char>(text[pos]) > ' ' && text[pos] != '{' && text[pos] != '}') ++pos;
        return text.substr(start, pos-start);
    }
};
struct Decal { std::string name, path; bool patch{}; };
struct Metadata { std::map<std::string,std::string> rarity; std::vector<Decal> decals; };
inline bool SafePath(std::string_view v) {
    if (v.empty() || v.size() > 180 || v.front() == '/' || v.find("..") != std::string_view::npos) return false;
    for (auto c : v) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '-')) return false;
    return true;
}
inline Metadata Parse(std::string_view schema) {
    Metadata out; Tokenizer t{schema};
    for (;;) {
        const auto token = t.next(); if (token.empty()) break;
        if (token != "paint_kits_rarity" && token != "sticker_kits") continue;
        const bool rarity = token == "paint_kits_rarity";
        if (t.next() != "{") continue;
        for (;;) {
            const auto key = t.next(); if (key.empty() || key == "}") break;
            const auto value = t.next(); if (value.empty()) break;
            if (rarity) {
                if (value != "{" && value != "}" && SafePath(key) && SafePath(value)) out.rarity[std::string(key)] = std::string(value);
                continue;
            }
            if (value != "{") continue;
            int depth = 1; std::string name, material; bool patch = false;
            while (depth > 0) {
                const auto field = t.next(); if (field.empty()) break;
                if (field == "{") { ++depth; continue; }
                if (field == "}") { --depth; continue; }
                if (depth != 1) continue;
                const auto val = t.next(); if (val.empty()) break;
                if (val == "{") { ++depth; continue; }
                if (field == "name") name = std::string(val);
                else if (field == "sticker_material" || field == "patch_material") { material = std::string(val); patch = field == "patch_material"; }
            }
            if (SafePath(name) && SafePath(material)) out.decals.push_back({std::move(name), std::move(material), patch});
        }
    }
    std::sort(out.decals.begin(), out.decals.end(), [](const Decal& a, const Decal& b){return a.name < b.name;});
    return out;
}
}
