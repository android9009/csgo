#include "general.hpp"
#include <cstdio>

namespace Misc::Scoreboard {
namespace {
// Icon name per item definition index, plus the minimum width the icon needs.
// The widths are the ones the reference Lua sets from its weapon table (pistols 31px,
// knives 45px, grenades 9-12px); everything else is left to size itself.
struct IconEntry {
    int definition;
    const char* name;
    int width;
};

constexpr IconEntry kIcons[] = {
    {1, "deagle", 31}, {2, "elite", 31}, {3, "fiveseven", 31}, {4, "glock", 31},
    {7, "ak47", 0}, {8, "aug", 0}, {9, "awp", 0}, {10, "famas", 0}, {11, "g3sg1", 0},
    {13, "galilar", 0}, {14, "m249", 0}, {16, "m4a1", 0}, {17, "mac10", 0},
    {19, "p90", 0}, {23, "mp5sd", 0}, {24, "ump45", 0}, {25, "xm1014", 0},
    {26, "bizon", 0}, {27, "mag7", 0}, {28, "negev", 0}, {29, "sawedoff", 0},
    {30, "tec9", 31}, {31, "taser", 0}, {32, "hkp2000", 31}, {33, "mp7", 0},
    {34, "mp9", 0}, {35, "nova", 0}, {36, "p250", 31}, {38, "scar20", 0},
    {39, "sg556", 0}, {40, "ssg08", 0},
    {42, "knife", 45}, {43, "flashbang", 9}, {44, "hegrenade", 0},
    {45, "smokegrenade", 9}, {46, "molotov", 0}, {47, "decoy", 0},
    {48, "incgrenade", 12}, {49, "c4", 0}, {59, "knife_t", 45},
    {60, "m4a1_silencer", 0}, {61, "usp_silencer", 31}, {63, "cz75a", 31},
    {64, "revolver", 31},
    // Knife finishes share the same wide icon slot.
    {500, "knife_bayonet", 45}, {503, "knife_css", 45}, {505, "knife_flip", 45},
    {506, "knife_gut", 45}, {507, "knife_karambit", 45}, {508, "knife_m9_bayonet", 45},
    {509, "knife_tactical", 45}, {512, "knife_falchion", 45},
    {514, "knife_survival_bowie", 45}, {515, "knife_butterfly", 45},
    {516, "knife_push", 45}, {519, "knife_ursus", 45}, {520, "knife_gypsy_jackknife", 45},
    {522, "knife_stiletto", 45}, {523, "knife_widowmaker", 45}, {525, "knife_skeleton", 45},
};

// Unscaled icon heights used by the reference layout.
constexpr int kArmorHeight = 17;
constexpr int kHelmetHeight = 22;
constexpr int kWeaponHeight = 18;
constexpr int kPanelMinWidth = 30;

std::string Replace(std::string text, const std::string& token, const std::string& value) {
    for (auto at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size()))
        text.replace(at, token.size(), value);
    return text;
}

std::string Number(int value) { return std::to_string(value); }

// Scaled from a percentage: the result is always >= 1 so nothing collapses to zero.
int Scale(int base, int percent) {
    const int scaled = (base * ClampScale(percent) + 50) / 100;
    return scaled < 1 ? 1 : scaled;
}

std::string WidthsLiteral(int horizontal) {
    std::string literal = "{";
    bool first = true;
    for (const auto& entry : kIcons) {
        if (entry.width <= 0) continue;
        if (!first) literal += ",";
        first = false;
        literal += "\"";
        literal += entry.name;
        literal += "\":";
        literal += Number(Scale(entry.width, horizontal));
    }
    literal += "}";
    return literal;
}

std::string SlotLayout(int weapon_height) {
    // One image per slot: the reference keeps four size classes to compensate icon
    // aspect ratios, which the weapon scale slider now does directly.
    return "<root><Image id=\"icon\" textureheight=\"" + Number(weapon_height) +
           "\" style=\"horizontal-align: center; padding: 1px 3px; margin: -1px -3px;\" /></root>";
}

std::string LayoutTag(const Scales& scales) {
    return Number(ClampScale(scales.x)) + "x" + Number(ClampScale(scales.y)) + "x" +
           Number(ClampScale(scales.weapon));
}

constexpr const char* kEngineTemplate = R"JS((function () {
	if (typeof __sb !== "undefined" && __sb && __sb.layout === "@TAG@") { return; }
	if (typeof __sb !== "undefined" && __sb && __sb.destroy) { __sb.destroy(); }

	var HIDDEN = ["id-sb-name__commendations__leader", "id-sb-name__commendations__teacher",
		"id-sb-name__commendations__friendly", "id-sb-name__musickit"];
	var WIDTHS = @WIDTHS@;
	var SLOT_LAYOUT = "@LAYOUT@";
	var PANEL_WIDTH = @PANELW@;
	var ARMOR_HEIGHT = @ARMORH@;
	var HELMET_HEIGHT = @HELMETH@;
	var SLOT_COUNT = 8;

	var state = { layout: "@TAG@", data: {}, panels: {}, handlers: [], warned: false };
	__sb = state;

	var px = function (value) { return Math.round(value) + "px"; };

	var findScoreboard = function () {
		var root = $.GetContextPanel();
		if (!root) { return null; }
		var container = root.FindChildTraverse("ScoreboardContainer");
		if (container) {
			var direct = container.FindChildTraverse("Scoreboard");
			if (direct) { return direct; }
		}
		var endOfMatch = root.FindChildTraverse("id-eom-scoreboard-container");
		if (endOfMatch) { return endOfMatch.FindChildTraverse("Scoreboard"); }
		return null;
	};

	var scoreboardRows = function () {
		var scoreboard = findScoreboard();
		if (!scoreboard) { return []; }
		return scoreboard.FindChildrenWithClassTraverse("sb-row");
	};

	var hideFlair = function (parent, hidden) {
		for (var i = 0; i < HIDDEN.length; i++) {
			var panel = parent.FindChildTraverse(HIDDEN[i]);
			if (!panel) { continue; }
			if (hidden) {
				panel.style.maxWidth = "0px";
				panel.style.margin = "0px";
			} else {
				panel.style.maxWidth = "";
				panel.style.margin = "";
			}
		}
	};

	var ensure = function (row, xuid) {
		var existing = state.panels[xuid];
		if (existing && existing.IsValid()) { return existing; }

		var nameCell = null;
		var children = row.Children();
		for (var i = 0; i < children.length; i++) {
			if (children[i].GetAttributeString("data-stat", "") === "name") {
				nameCell = children[i].GetChild(0);
				break;
			}
		}
		if (!nameCell) { return null; }
		var parent = nameCell.GetParent();
		if (!parent) { return null; }

		var panel = $.CreatePanel("Panel", parent, "csgo-sb-equipment-" + xuid, {
			style: "overflow: noclip; width: fit-children; margin: 0px; padding: 0px; height: 100%; flow-children: left;"
		});
		if (!panel) { return null; }
		panel.style.minWidth = px(PANEL_WIDTH);
		hideFlair(parent, true);

		var armor = $.CreatePanel("Image", panel, "armor", {
			textureheight: "" + ARMOR_HEIGHT, style: "padding-left: 5px; padding-top: 3px; opacity: 0.25;"
		});
		if (armor) { armor.visible = false; }
		var helmet = $.CreatePanel("Image", panel, "helmet", {
			textureheight: "" + HELMET_HEIGHT, style: "padding-left: 0px; margin-left: 3px; margin-right: -3px; opacity: 0.25;"
		});
		if (helmet) {
			helmet.visible = false;
			helmet.SetImage("file://{images}/icons/equipment/helmet.svg");
		}
		var defuser = $.CreatePanel("Image", panel, "defuser", {
			textureheight: "" + ARMOR_HEIGHT, style: "padding-left: 4px; padding-top: 3px; opacity: 0.25;"
		});
		if (defuser) {
			defuser.visible = false;
			defuser.SetImage("file://{images}/icons/equipment/defuser.svg");
		}

		for (var slot = SLOT_COUNT - 1; slot >= 0; slot--) {
			var holder = $.CreatePanel("Panel", panel, "weapon-" + slot);
			if (!holder) { continue; }
			holder.visible = false;
			holder.BLoadLayoutFromString(SLOT_LAYOUT, false, false);
		}

		var first = parent.GetChild(1);
		if (first && first !== panel) { parent.MoveChildBefore(panel, first); }
		state.panels[xuid] = panel;
		return panel;
	};

	var applyRow = function (row, entry) {
		var xuid = row.m_xuid;
		if (!xuid || !entry) { return; }
		var panel = ensure(row, xuid);
		if (!panel) { return; }

		var selected = typeof entry.s === "number" ? entry.s : -1;
		for (var index = 0; index < SLOT_COUNT; index++) {
			var holder = panel.FindChild("weapon-" + index);
			if (!holder) { continue; }
			if (index >= entry.w.length) {
				if (holder.visible) { holder.visible = false; }
				continue;
			}
			var name = entry.w[index];
			holder.visible = true;
			holder.style.minWidth = px(WIDTHS[name] || 0);
			var icon = holder.FindChild("icon");
			if (icon) {
				icon.SetImage("file://{images}/icons/equipment/" + name + ".svg");
				icon.style.opacity = index === selected ? "0.85" : "0.4";
			}
		}

		var armor = panel.FindChild("armor");
		if (armor) {
			armor.visible = entry.a > 0;
			if (entry.a > 0) {
				armor.SetImage("file://{images}/icons/equipment/" +
					(entry.a >= 2 ? "assaultsuit" : "kevlar") + ".svg");
			}
		}
		var helmet = panel.FindChild("helmet");
		if (helmet) { helmet.visible = entry.a >= 2; }
		var defuser = panel.FindChild("defuser");
		if (defuser) { defuser.visible = entry.d === 1; }
	};

	var apply = function () {
		var rows = scoreboardRows();
		for (var i = 0; i < rows.length; i++) {
			var xuid = rows[i].m_xuid;
			if (!xuid) { continue; }
			if (state.data[xuid]) {
				applyRow(rows[i], state.data[xuid]);
			} else if (state.panels[xuid] && state.panels[xuid].IsValid()) {
				state.panels[xuid].visible = false;
			}
		}
	};

	var feed = function (payload) {
		state.data = {};
		if (typeof GameStateAPI === "undefined" || !GameStateAPI) {
			if (!state.warned) { state.warned = true; $.Msg("csgo: scoreboard equipment needs GameStateAPI"); }
			return;
		}
		for (var key in payload) {
			var xuid = GameStateAPI.GetPlayerXuidStringFromEntIndex(parseInt(key, 10));
			if (xuid && xuid !== "0") { state.data[xuid] = payload[key]; }
		}
		apply();
	};

	var destroy = function () {
		for (var i = 0; i < state.handlers.length; i++) {
			$.UnregisterForUnhandledEvent(state.handlers[i].name, state.handlers[i].handler);
		}
		state.handlers = [];
		for (var key in state.panels) {
			var panel = state.panels[key];
			if (!panel || !panel.IsValid()) { continue; }
			var parent = panel.GetParent();
			if (parent) { hideFlair(parent, false); }
			panel.DeleteAsync(0.0);
		}
		state.panels = {};
		state.data = {};
	};

	state.feed = feed;
	state.destroy = destroy;

	var bind = function (name) {
		var handler = function () { apply(); };
		$.RegisterForUnhandledEvent(name, handler);
		state.handlers.push({ name: name, handler: handler });
	};
	bind("OnOpenScoreboard");
	bind("Scoreboard_UpdateEverything");
	bind("Scoreboard_UpdateJob");
	apply();
})();)JS";

std::string EngineSource(const Scales& scales) {
    std::string script = kEngineTemplate;
    script = Replace(script, "@TAG@", LayoutTag(scales));
    script = Replace(script, "@WIDTHS@", WidthsLiteral(scales.x));
    script = Replace(script, "@LAYOUT@", SlotLayout(Scale(kWeaponHeight, scales.weapon)));
    script = Replace(script, "@PANELW@", Number(Scale(kPanelMinWidth, scales.x)));
    script = Replace(script, "@ARMORH@", Number(Scale(kArmorHeight, scales.y)));
    script = Replace(script, "@HELMETH@", Number(Scale(kHelmetHeight, scales.y)));
    return script;
}

std::string Quote(const char* text) {
    std::string out = "\"";
    if (text) {
        for (const char* c = text; *c; ++c) {
            if (*c == '\\' || *c == '"') {
                out += '\\';
                out += *c;
            } else {
                out += *c;
            }
        }
    }
    out += "\"";
    return out;
}

std::string FeedPayload(const std::vector<Equipment>& players) {
    std::string payload = "{";
    bool first = true;
    for (const auto& player : players) {
        if (player.slot <= 0 || player.weapon_count <= 0) continue;
        if (!first) payload += ",";
        first = false;
        payload += "\"" + std::to_string(player.slot) + "\":{\"w\":[";
        const int count = player.weapon_count > 8 ? 8 : player.weapon_count;
        for (int i = 0; i < count; ++i) {
            if (i) payload += ",";
            payload += Quote(player.weapons[i]);
        }
        payload += "],\"s\":" + std::to_string(player.selected);
        payload += ",\"a\":" + std::to_string(player.armor);
        payload += ",\"d\":" + std::to_string(player.defuser ? 1 : 0);
        payload += "}";
    }
    payload += "}";
    return payload;
}
} // namespace

int ClampScale(int percent) {
    if (percent < kScaleMin) return kScaleMin;
    if (percent > kScaleMax) return kScaleMax;
    return percent;
}

const char* WeaponIcon(int definition) {
    for (const auto& entry : kIcons)
        if (entry.definition == definition) return entry.name;
    return nullptr;
}

int WeaponBaseWidth(const char* icon) {
    if (!icon) return 0;
    for (const auto& entry : kIcons) {
        const char* a = entry.name;
        const char* b = icon;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return entry.width;
    }
    return 0;
}

std::string BuildEngineScript(const Scales& scales) { return EngineSource(scales); }

std::string BuildFeedScript(const std::vector<Equipment>& players, const Scales& scales) {
    return EngineSource(scales) + "\n__sb.feed(" + FeedPayload(players) + ");";
}

std::string BuildResetScript() {
    return "if (typeof __sb !== \"undefined\" && __sb && __sb.destroy) { __sb.destroy(); }";
}
} // namespace Misc::Scoreboard
