#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <map>
#include <optional>
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
struct Metadata {
    std::map<std::string, std::string> rarity;
    // Schema paint kit IDs are required by the Source material generator.
    std::map<std::string, int> paint_ids;
    std::vector<Decal> decals;
    // Paint kit name -> default 0..1 RGBA color of each customizable element,
    // in file order. Paint kits with a non-empty list are paintable.
    std::map<std::string, std::vector<std::array<float, 4>>> customization;
};
// "r g b [a]" / "r;g;b;a" / "r, g, b, a"; floats 0..1 or bytes 0..255.
inline std::optional<std::array<float, 4>> ParseColor(std::string_view text) {
    std::array<float, 4> color{};
    int count = 0;
    auto flush = [&](std::string_view token) {
        if (token.empty() || count < 0 || count >= 4) return;
        float parsed = 0.f;
        const std::string copy(token); // sscanf needs NUL termination
        if (std::sscanf(copy.c_str(), "%f", &parsed) != 1) { count = -1; return; }
        color[count++] = parsed;
    };
    while (!text.empty()) {
        const auto end = text.find_first_of(" ;,");
        const auto token = end == std::string_view::npos ? text : text.substr(0, end);
        flush(token);
        text.remove_prefix(token.size());
        if (!text.empty()) text.remove_prefix(1); // the delimiter
    }
    if (count < 3) return std::nullopt;
    float limit = 0.f;
    for (int i = 0; i < count; ++i) limit = std::max(limit, color[i]);
    for (int i = 0; i < 4; ++i) {
        if (i >= count) { color[i] = 1.f; continue; } // missing alpha stays 0..1 scale
        color[i] = limit > 1.5f ? color[i] / 255.f : std::clamp(color[i], 0.f, 1.f);
    }
    return color;
}
// Consume one "customization" block body; entries are indexed in file order.
// Returns false when the text ran out inside the block.
inline bool ParseCustomizationBlock(Tokenizer& t, std::vector<std::array<float, 4>>& colors) {
    int depth = 1;
    while (depth > 0) {
        const auto field = t.next();
        if (field.empty()) return false;
        if (field == "{") { ++depth; continue; }
        if (field == "}") { --depth; continue; }
        const auto value = t.next();
        if (value.empty()) return false;
        if (value == "{") { ++depth; continue; }
        if (value == "}") { --depth; continue; }
        if (depth == 1) {
            if (const auto color = ParseColor(value)) colors.push_back(*color);
        }
    }
    return true;
}
inline int ParseId(std::string_view text) {
    if (text.empty() || text.size() > 9) return -1;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}
inline bool SafePath(std::string_view v) {
    if (v.empty() || v.size() > 180 || v.front() == '/' || v.find("..") != std::string_view::npos) return false;
    for (auto c : v) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '-')) return false;
    return true;
}
struct InventoryMetadata {
    std::map<std::string,int> items, music;
};
inline InventoryMetadata ParseInventory(std::string_view schema) {
    InventoryMetadata out; Tokenizer t{schema};
    for (;;) {
        auto section=t.next(); if(section.empty()) break;
        if(section!="items" && section!="music_definitions") continue;
        if(t.next()!="{") continue;
        const bool music=section=="music_definitions";
        for (;;) {
            auto key=t.next(); if(key.empty()||key=="}") break;
            auto value=t.next(); if(value!="{") continue;
            const int id=ParseId(key); int depth=1;
            std::string name,image,model;
            while(depth>0) {
                auto field=t.next();if(field.empty()) break;
                if(field=="{"){++depth;continue;}if(field=="}"){--depth;continue;}
                auto val=t.next();if(val=="{"){++depth;continue;}
                if(field=="name" && depth==1) name=std::string(val);
                if(field=="image_inventory") image=std::string(val);
                if(field=="model_player") model=std::string(val);
            }
            if(id<=0) continue;
            auto& target=music?out.music:out.items;
            if(!name.empty()) target[name]=id;
            if(!image.empty()) target["resource/flash/"+image+".png"]=id;
            if(!model.empty()) target[model]=id;
        }
    }
    return out;
}
inline Metadata Parse(std::string_view schema) {
    Metadata out; Tokenizer t{schema};
    for (;;) {
        const auto token = t.next(); if (token.empty()) break;
        if (token == "customization") {
            // Alternative layout: a top-level block keyed by paint kit name or ID.
            if (t.next() != "{") continue;
            for (;;) {
                const auto key = t.next(); if (key.empty() || key == "}") break;
                const auto value = t.next(); if (value.empty() || value != "{") continue;
                std::vector<std::array<float, 4>> colors;
                if (ParseCustomizationBlock(t, colors) && !colors.empty()) out.customization[std::string(key)] = std::move(colors);
            }
            continue;
        }
        if (token != "paint_kits_rarity" && token != "sticker_kits" && token != "paint_kits") continue;
        const bool rarity = token == "paint_kits_rarity";
        const bool paint = token == "paint_kits";
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
            std::vector<std::array<float, 4>> custom_colors;
            while (depth > 0) {
                const auto field = t.next(); if (field.empty()) break;
                if (field == "{") { ++depth; continue; }
                if (field == "}") { --depth; continue; }
                if (depth != 1) continue;
                const auto val = t.next(); if (val.empty()) break;
                if (field == "customization" && val == "{") {
                    // Consumes the whole block including its closing brace.
                    if (!ParseCustomizationBlock(t, custom_colors)) break;
                    continue;
                }
                if (val == "{") { ++depth; continue; }
                if (field == "name") name = std::string(val);
                else if (field == "sticker_material" || field == "patch_material") { material = std::string(val); patch = field == "patch_material"; }
            }
            if (paint) {
                const int id = ParseId(key);
                if (id >= 0 && SafePath(name)) out.paint_ids[name] = id;
                if (!name.empty() && !custom_colors.empty()) out.customization[name] = std::move(custom_colors);
            } else if (SafePath(name) && SafePath(material)) {
                out.decals.push_back({std::move(name), std::move(material), patch});
            }
        }
    }
    // Top-level entries may be keyed by numeric paint kit ID; remap to names.
    for (auto it = out.customization.begin(); it != out.customization.end(); ) {
        const int id = ParseId(it->first);
        if (id < 0 || out.paint_ids.count(it->first)) { ++it; continue; }
        bool renamed = false;
        for (auto& pair : out.paint_ids) {
            if (pair.second != id) continue;
            if (out.customization.count(pair.first)) { ++it; renamed = true; break; } // keep the name entry
            auto colors = std::move(it->second);
            it = out.customization.erase(it);
            out.customization[pair.first] = std::move(colors);
            renamed = true;
            break;
        }
        if (!renamed) ++it;
    }
    std::sort(out.decals.begin(), out.decals.end(), [](const Decal& a, const Decal& b){return a.name < b.name;});
    return out;
}
}
