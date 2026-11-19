#include "general.hpp"
#include <cstdio>

namespace Misc::Scoreboard {
namespace {
// Icon name per item definition index, plus the minimum width the icon needs.
// The names are the client's own equipment icons - the console name without its
// "item_"/"weapon_" prefix - and the widths are the ones the reference Lua sets from its
// weapon table (pistols 31px, knives 45px, grenades 9-12px).
struct IconEntry {
    int definition;
    const char* name;
    int width;
    const char* slot; // size class override, nullptr means "small"
};

constexpr IconEntry kIcons[] = {
    {1, "deagle", 31, nullptr}, {2, "elite", 31, nullptr}, {3, "fiveseven", 31, nullptr},
    {4, "glock", 31, nullptr},
    {7, "ak47", 0, nullptr}, {8, "aug", 0, nullptr}, {9, "awp", 0, nullptr},
    {10, "famas", 0, nullptr}, {11, "g3sg1", 0, nullptr},
    {13, "galilar", 0, nullptr}, {14, "m249", 0, nullptr}, {16, "m4a1", 0, nullptr},
    {17, "mac10", 0, nullptr},
    {19, "p90", 0, nullptr}, {23, "mp5sd", 0, nullptr}, {24, "ump45", 0, nullptr},
    {25, "xm1014", 0, nullptr},
    {26, "bizon", 0, nullptr}, {27, "mag7", 0, nullptr}, {28, "negev", 0, nullptr},
    {29, "sawedoff", 0, nullptr},
    {30, "tec9", 31, nullptr}, {31, "taser", 0, nullptr}, {32, "hkp2000", 31, nullptr},
    {33, "mp7", 0, nullptr},
    {34, "mp9", 0, nullptr}, {35, "nova", 0, nullptr}, {36, "p250", 31, nullptr},
    {38, "scar20", 0, nullptr},
    {39, "sg556", 0, nullptr}, {40, "ssg08", 0, nullptr},
    {42, "knife", 45, "smaller"}, {43, "flashbang", 9, nullptr}, {44, "hegrenade", 0, nullptr},
    {45, "smokegrenade", 9, nullptr}, {46, "molotov", 0, nullptr}, {47, "decoy", 0, nullptr},
    {48, "incgrenade", 12, nullptr}, {49, "c4", 0, nullptr}, {59, "knife_t", 45, "smaller"},
    {60, "m4a1_silencer", 0, nullptr}, {61, "usp_silencer", 31, nullptr},
    {63, "cz75a", 31, nullptr},
    {64, "revolver", 31, nullptr},
    // Knife finishes share the same wide icon slot.
    {500, "knife_bayonet", 45, "smaller"}, {503, "knife_css", 45, "smaller"},
    {505, "knife_flip", 45, "smaller"},
    {506, "knife_gut", 45, "smaller"}, {507, "knife_karambit", 45, "smaller"},
    {508, "knife_m9_bayonet", 45, "smaller"},
    {509, "knife_tactical", 45, "smaller"}, {512, "knife_falchion", 45, "smaller"},
    {514, "knife_survival_bowie", 45, "smaller"}, {515, "knife_butterfly", 45, "smaller"},
    {516, "knife_push", 45, "smaller"}, {519, "knife_ursus", 45, "smaller"},
    {520, "knife_gypsy_jackknife", 45, "smaller"},
    {522, "knife_stiletto", 45, "smaller"}, {523, "knife_widowmaker", 45, "smaller"},
    {525, "knife_skeleton", 45, "smaller"},
};

// Unscaled icon heights of the four size classes the reference layout keeps, so a weapon
// can pick the class that makes its aspect ratio look right at the same slot width.
constexpr int kHeightSmaller = 15;
constexpr int kHeightSmall = 17;
constexpr int kHeightMedium = 18;
constexpr int kHeightLarge = 21;
constexpr int kArmorHeight = 17;
constexpr int kHelmetHeight = 22;
constexpr int kPanelMinWidth = 30;
// The reference Lays out 25 slot parents per row (weapons + grenades + kit + bomb).
constexpr int kSlotCount = 25;

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

std::string WidthsLiteral(int horizontal, bool overrides, bool maximums) {
    std::string literal = "{";
    bool first = true;
    for (const auto& entry : kIcons) {
        std::string value;
        if (overrides) {
            if (!entry.slot) continue;
            value = std::string("\"") + entry.slot + "\"";
        } else if (maximums) {
            // Only the flashbang is narrower than its icon wants to be; the reference caps
            // it at 12px so the row keeps its spacing.
            if (std::string(entry.name) != "flashbang") continue;
            value = Number(Scale(12, horizontal));
        } else {
            if (entry.width <= 0) continue;
            value = Number(Scale(entry.width, horizontal));
        }
        if (!first) literal += ",";
        first = false;
        literal += "\"";
        literal += entry.name;
        literal += "\":";
        literal += value;
    }
    literal += "}";
    return literal;
}

// Four images per slot, exactly the size classes the reference uses: one is made visible
// per weapon, the rest keep opacity 0.01 so the row's own layout does not jump.
std::string SlotLayout(int smaller, int small, int medium, int large) {
    auto image = [](const char* id, int height) {
        char buffer[320];
        std::snprintf(buffer, sizeof(buffer),
            "<Image id=\\\"%s\\\" textureheight=\\\"%d\\\" style=\\\"horizontal-align: center; "
            "opacity: 0.01; transition: opacity 0.1s ease-in-out 0.0s, img-shadow 0.12s "
            "ease-in-out 0.0s; overflow: noclip; padding: 3px 5px; margin: -3px -5px;\\\" />",
            id, height);
        return std::string(buffer);
    };
    return "<root><Panel style=\\\"min-width: 3px; padding-top: 2px; padding-left: 2px; "
           "overflow: noclip;\\\">" + image("smaller", smaller) + image("small", small) +
           image("medium", medium) + image("large", large) + "</Panel></root>";
}

std::string LayoutTag(const Scales& scales) {
    return Number(ClampScale(scales.x)) + "x" + Number(ClampScale(scales.y)) + "x" +
           Number(ClampScale(scales.weapon));
}

constexpr const char* kEngineTemplate = R"JS((function () {
	if (typeof __sb !== "undefined" && __sb && __sb.layout === "@TAG@") { return; }
	if (typeof __sb !== "undefined" && __sb && __sb.destroy) { __sb.destroy(); }

	// Panel ids the reference implementation hides to make room for the icons.
	var HIDDEN = ["id-sb-name__commendations__leader", "id-sb-name__commendations__teacher",
		"id-sb-name__commendations__friendly", "id-sb-name__musickit"];
	var TEAM_COLORS = { CT: "#B5D4EE40", TERRORIST: "#EAD18A61" };
	var SHADOW_COLORS = { CT: "#393C40", TERRORIST: "#4C4844" };
	var SLOT_OVERRIDE = @OVERRIDES@;
	var MIN_WIDTHS = @WIDTHS@;
	var MAX_WIDTHS = @MAXWIDTHS@;
	var SLOT_LAYOUT = "@LAYOUT@";
	var PANEL_WIDTH = @PANELW@;
	var ARMOR_HEIGHT = @ARMORH@;
	var HELMET_HEIGHT = @HELMETH@;
	var SLOT_COUNT = @SLOTS@;

	var state = { layout: "@TAG@", data: {}, panels: {}, handlers: [], warned: false,
		reportedEmpty: false, reportedRows: false, keyedByXuid: false };
	__sb = state;

	var px = function (value) { return Math.round(value) + "px"; };

	// The reference runs its script in the CSGOHud context and starts from
	// $.GetContextPanel(). This script runs in whatever context the last UI event left
	// behind, so it climbs to the first panel that is (or contains) the HUD and searches
	// from there; the UI root is the last resort.
	var hudRoot = function () {
		var panel = $.GetContextPanel();
		var top = panel;
		for (var guard = 0; panel && guard < 64; guard++) {
			if (panel.id === "CSGOHud") { return panel; }
			top = panel;
			panel = panel.GetParent ? panel.GetParent() : null;
		}
		return top;
	};

	var scoreboardPanel = function () {
		var root = hudRoot();
		if (!root) { return null; }
		var container = root.FindChildTraverse("ScoreboardContainer");
		if (!container) { container = root.FindChildTraverse("id-eom-scoreboard-container"); }
		if (!container) { return null; }
		return container.FindChildTraverse("Scoreboard");
	};

	var gameState = function () {
		// Inside CSGOHud the HUD's own globals are in scope; from anywhere else they are not.
		if (typeof GameStateAPI !== "undefined" && GameStateAPI) { return GameStateAPI; }
		var root = hudRoot();
		if (root && root.GameStateAPI) { return root.GameStateAPI; }
		return null;
	};

	var hideFlair = function (parent, hidden) {
		for (var i = 0; i < HIDDEN.length; i++) {
			var panel = parent.FindChildTraverse(HIDDEN[i]);
			if (!panel) { continue; }
			if (hidden) {
				panel.style.maxWidth = "0px";
				panel.style.margin = "0px";
			} else {
				panel.style.maxWidth = "28px";
				panel.style.margin = "0px 5px 0px 5px";
			}
		}
	};

	// The row a player's icons belong to. The client sets m_xuid on every scoreboard row;
	// when GameStateAPI is not in scope the row is matched by its nickname label instead.
	var rowKey = function (row) {
		if (row.m_xuid) { return "x" + row.m_xuid; }
		var label = row.FindChildTraverse("id-sb-name");
		if (label && label.text) { return "n" + label.text; }
		return null;
	};

	var ensure = function (row, key) {
		var existing = state.panels[key];
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

		var panel = $.CreatePanel("Panel", parent, "csgo-sb-equipment-" + key, {
			style: "overflow: noclip; width: fit-children; margin: 0px; padding: 1px 0px 0px 0px; height: 100%; flow-children: left;"
		});
		if (!panel) { return null; }
		panel.style.minWidth = px(PANEL_WIDTH);
		hideFlair(parent, true);

		var skillgroup = parent.FindChildTraverse("id-sb-skillgroup-image");
		if (skillgroup) { skillgroup.style.margin = "0px 0px 0px 5px"; }

		var armor = $.CreatePanel("Image", panel, "armor", {
			textureheight: "" + ARMOR_HEIGHT,
			style: "padding-left: 2px; padding-top: 3px; opacity: 0.2; padding-left: 5px;"
		});
		if (armor) { armor.visible = false; }
		var helmet = $.CreatePanel("Image", panel, "helmet", {
			textureheight: "" + HELMET_HEIGHT,
			style: "padding-left: 2px; padding-top: 0px; opacity: 0.2; padding-left: 0px; margin-left: 3px; margin-right: -3px;"
		});
		if (helmet) {
			helmet.visible = false;
			helmet.SetImage("file://{images}/icons/equipment/helmet.svg");
		}
		var defuser = $.CreatePanel("Image", panel, "defuser", {
			textureheight: "" + ARMOR_HEIGHT, style: "padding-left: 4px; padding-top: 3px; opacity: 0.2;"
		});
		if (defuser) {
			defuser.visible = false;
			defuser.SetImage("file://{images}/icons/equipment/defuser.svg");
		}
		var money = $.CreatePanel("Label", panel, "money", {
			style: "padding-left: 6px; padding-right: 2px; vertical-align: center; font-size: 13px; opacity: 0.65;"
		});
		if (money) { money.visible = false; }

		for (var slot = SLOT_COUNT - 1; slot >= 0; slot--) {
			var holder = $.CreatePanel("Panel", panel, "weapon-" + slot);
			if (!holder) { continue; }
			holder.visible = false;
			holder.BLoadLayoutFromString(SLOT_LAYOUT, false, false);
		}

		// Right after the first column, exactly where the reference puts it: appended at the
		// end of the row the panel lands behind the stat columns and is clipped away.
		var first = parent.GetChild(1);
		if (first && first !== panel) { parent.MoveChildBefore(panel, first); }

		state.panels[key] = panel;
		return panel;
	};

	var applyRow = function (row, key, entry) {
		var panel = ensure(row, key);
		if (!panel) { return false; }

		var team = null;
		var api = gameState();
		if (api && row.m_xuid) { team = api.GetPlayerTeamName(row.m_xuid); }
		var wash = TEAM_COLORS[team] || "#ffffffff";
		var shadow = SHADOW_COLORS[team] || "#58534D";

		var selected = typeof entry.s === "number" ? entry.s : -1;
		var shown = 0;
		for (var index = 0; index < SLOT_COUNT; index++) {
			var holder = panel.FindChild("weapon-" + index);
			if (!holder) { continue; }
			if (!entry.w || index >= entry.w.length) {
				if (holder.visible) { holder.visible = false; }
				continue;
			}
			var name = entry.w[index];
			holder.visible = true;
			shown++;

			var wanted = SLOT_OVERRIDE[name] || "small";
			var icon = null;
			var images = holder.Children();
			for (var i = 0; i < images.length; i++) {
				if (images[i].id === wanted) {
					images[i].visible = true;
					icon = images[i];
				} else {
					images[i].visible = false;
				}
			}
			if (!icon) { continue; }
			icon.SetImage("file://{images}/icons/equipment/" + name + ".svg");
			icon.style.opacity = index === selected ? "0.85" : "0.35";
			icon.style.imgShadow = index === selected ? (shadow + " 0px 0px 3px 3.75") : "none";
			icon.style.washColorFast = wash;
			icon.style.marginLeft = "-5px";
			icon.style.marginRight = "-5px";
			if (name === "knife_ursus") {
				icon.style.marginLeft = "-2px";
			} else if (name === "knife_widowmaker") {
				icon.style.marginLeft = "-3px";
			} else if (name === "hkp2000") {
				icon.style.marginRight = "-4px";
			} else if (name === "incgrenade") {
				icon.style.marginLeft = "-6px";
			} else if (name === "flashbang") {
				icon.style.marginLeft = "-5px";
			}
			holder.style.minWidth = px(MIN_WIDTHS[name] || 0);
			holder.style.maxWidth = px(MAX_WIDTHS[name] || 1000);
		}

		var armor = panel.FindChild("armor");
		var helmet = panel.FindChild("helmet");
		if (armor && helmet) {
			if (typeof entry.a === "number" && entry.a > 0) {
				armor.visible = true;
				armor.style.washColorFast = wash;
				armor.SetImage("file://{images}/icons/equipment/kevlar.svg");
				helmet.visible = entry.a >= 2;
				if (helmet.visible) { helmet.style.washColorFast = wash; }
			} else {
				armor.visible = false;
				helmet.visible = false;
			}
		}

		var defuser = panel.FindChild("defuser");
		if (defuser) { defuser.visible = entry.d === 1; }

		var money = panel.FindChild("money");
		if (money) {
			var value = typeof entry.m === "number" ? entry.m : -1;
			money.visible = value >= 0;
			if (value >= 0) { money.text = "$" + value; }
		}

		panel.visible = true;
		return shown > 0 || (typeof entry.a === "number" && entry.a > 0) || entry.d === 1;
	};

	var apply = function () {
		var scoreboard = scoreboardPanel();
		if (!scoreboard) {
			if (!state.reportedEmpty) {
				state.reportedEmpty = true;
				$.Msg("csgo-base: no scoreboard found from context \"" + (hudRoot() ? hudRoot().id : "?") + "\"");
			}
			return;
		}
		var rows = scoreboard.FindChildrenWithClassTraverse("sb-row");
		var attached = 0;
		var unmatched = 0;
		for (var i = 0; i < rows.length; i++) {
			var key = rowKey(rows[i]);
			if (!key) { unmatched++; continue; }
			var entry = state.data[key];
			if (entry && applyRow(rows[i], key, entry)) {
				attached++;
			} else if (state.panels[key] && state.panels[key].IsValid()) {
				state.panels[key].visible = false;
			}
		}
		if (attached > 0 && !state.reportedRows) {
			state.reportedRows = true;
			$.Msg("csgo-base: equipment attached to " + attached + " of " + rows.length +
				" scoreboard row(s), keyed by " + (state.keyedByXuid ? "xuid" : "nickname"));
		} else if (rows.length > 0 && attached === 0 && !state.reportedNone &&
			Object.keys(state.data).length > 0) {
			// Only worth a line once there is something to show: at install time the script
			// runs before any data arrived, and "nothing matched" would be a lie.
			state.reportedNone = true;
			$.Msg("csgo-base: " + rows.length + " row(s) but none matched the fed players (" +
				unmatched + " without a key; fed keys: " + Object.keys(state.data).join(",") +
				"; row keys: " + rows.map(function (r) { return rowKey(r); }).join(",") + ")");
		}
	};

	var feed = function (payload) {
		state.data = {};
		var api = gameState();
		state.keyedByXuid = !!api;
		for (var entity in payload) {
			var entry = payload[entity];
			var key = null;
			if (api) {
				var xuid = api.GetPlayerXuidStringFromEntIndex(parseInt(entity, 10));
				if (xuid && xuid !== "0") { key = "x" + xuid; }
			}
			// No GameStateAPI (or no xuid for this slot): fall back to the nickname the
			// payload carries, which is what the row label shows.
			if (!key && entry.n) { key = "n" + entry.n; }
			if (key) { state.data[key] = entry; }
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
	state.apply = apply;

	// The scoreboard rebuilds its rows every time it is opened or refreshed, so the panels
	// are re-created from those three events rather than on a timer.
	var bind = function (name) {
		var handler = function () { apply(); };
		$.RegisterForUnhandledEvent(name, handler);
		state.handlers.push({ name: name, handler: handler });
	};
	bind("OnOpenScoreboard");
	bind("Scoreboard_UpdateEverything");
	bind("Scoreboard_UpdateJob");

	$.Msg("csgo-base: equipment engine installed (layout " + state.layout + ", context \"" +
		(hudRoot() ? hudRoot().id : "?") + "\", " + SLOT_COUNT + " slots)");
	apply();
})();)JS";

std::string EngineSource(const Scales& scales) {
    std::string script = kEngineTemplate;
    script = Replace(script, "@TAG@", LayoutTag(scales));
    script = Replace(script, "@OVERRIDES@", WidthsLiteral(scales.x, true, false));
    script = Replace(script, "@WIDTHS@", WidthsLiteral(scales.x, false, false));
    script = Replace(script, "@MAXWIDTHS@", WidthsLiteral(scales.x, false, true));
    script = Replace(script, "@LAYOUT@", SlotLayout(Scale(kHeightSmaller, scales.weapon),
                                                    Scale(kHeightSmall, scales.weapon),
                                                    Scale(kHeightMedium, scales.weapon),
                                                    Scale(kHeightLarge, scales.weapon)));
    script = Replace(script, "@PANELW@", Number(Scale(kPanelMinWidth, scales.x)));
    script = Replace(script, "@ARMORH@", Number(Scale(kArmorHeight, scales.y)));
    script = Replace(script, "@HELMETH@", Number(Scale(kHelmetHeight, scales.y)));
    script = Replace(script, "@SLOTS@", Number(kSlotCount));
    return script;
}

// Bounded on purpose: the input is game memory, and a runaway loop here would build a
// string until the allocator threw (which is exactly how this feature once crashed).
std::string Quote(const char* text) {
    std::string out = "\"";
    for (int i = 0; text && i < 256 && text[i]; ++i) {
        const char c = text[i];
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

std::string FeedPayload(const std::vector<Equipment>& players) {
    std::string payload = "{";
    bool first = true;
    for (const auto& player : players) {
        if (player.slot <= 0) continue;
        const bool has_body = player.weapon_count > 0 || player.armor > 0 || player.defuser;
        if (!has_body) continue;
        if (!first) payload += ",";
        first = false;
        payload += "\"" + std::to_string(player.slot) + "\":{\"w\":[";
        const int count = player.weapon_count > kEquipmentSlots ? kEquipmentSlots
                                                               : player.weapon_count;
        for (int i = 0; i < count; ++i) {
            if (i) payload += ",";
            payload += Quote(player.weapons[i]);
        }
        payload += "],\"s\":" + std::to_string(player.selected);
        payload += ",\"a\":" + std::to_string(player.armor);
        payload += ",\"d\":" + std::to_string(player.defuser ? 1 : 0);
        if (player.money >= 0) payload += ",\"m\":" + std::to_string(player.money);
        // The nickname is only a fallback key for builds where GameStateAPI is out of reach
        // from the script's context; the row label is what it is compared against.
        if (player.name[0]) payload += ",\"n\":" + Quote(player.name);
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

const char* WeaponSlotClass(const char* icon) {
    if (!icon) return nullptr;
    for (const auto& entry : kIcons) {
        const char* a = entry.name;
        const char* b = icon;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return entry.slot;
    }
    return nullptr;
}

std::string BuildEngineScript(const Scales& scales) { return EngineSource(scales); }

std::string BuildFeedScript(const std::vector<Equipment>& players, const Scales& scales) {
    return EngineSource(scales) + "\n__sb.feed(" + FeedPayload(players) + ");";
}

std::string BuildResetScript() {
    return "if (typeof __sb !== \"undefined\" && __sb && __sb.destroy) { __sb.destroy(); }";
}
} // namespace Misc::Scoreboard
