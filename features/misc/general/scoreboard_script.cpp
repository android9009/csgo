#include "general.hpp"
#include <cstdio>

namespace Misc::Scoreboard {
namespace {
// Icon name per item definition index, plus the width class and the weapon class the
// injected script sizes it with. Names are the client's own equipment icons - the console
// name without its "item_"/"weapon_" prefix - and the classes follow the reference
// implementation (velocity, CS2): a pistol is drawn narrow, a rifle wide, a grenade narrow.
struct IconEntry {
    int definition;
    const char* name;
    int type;   // WeaponTypeOf() values
    int width;  // minimum width in unscaled pixels, 0 = let the icon size itself
};

constexpr int kTypeKnife = 0;
constexpr int kTypePistol = 1;
constexpr int kTypeSmg = 2;
constexpr int kTypeRifle = 3;
constexpr int kTypeShotgun = 4;
constexpr int kTypeSniper = 5;
constexpr int kTypeMachineGun = 6;
constexpr int kTypeBomb = 7;
constexpr int kTypeTaser = 8;
constexpr int kTypeGrenade = 9;
constexpr int kTypeKit = 10;

constexpr IconEntry kIcons[] = {
    {1, "deagle", kTypePistol, 31}, {2, "elite", kTypePistol, 31},
    {3, "fiveseven", kTypePistol, 31}, {4, "glock", kTypePistol, 31},
    {7, "ak47", kTypeRifle, 0}, {8, "aug", kTypeRifle, 0}, {9, "awp", kTypeSniper, 0},
    {10, "famas", kTypeRifle, 0}, {11, "g3sg1", kTypeSniper, 0},
    {13, "galilar", kTypeRifle, 0}, {14, "m249", kTypeMachineGun, 0},
    {16, "m4a1", kTypeRifle, 0}, {17, "mac10", kTypeSmg, 0},
    {19, "p90", kTypeSmg, 0}, {23, "mp5sd", kTypeSmg, 0}, {24, "ump45", kTypeSmg, 0},
    {25, "xm1014", kTypeShotgun, 0}, {26, "bizon", kTypeSmg, 0}, {27, "mag7", kTypeShotgun, 0},
    {28, "negev", kTypeMachineGun, 0}, {29, "sawedoff", kTypeShotgun, 0},
    {30, "tec9", kTypePistol, 31}, {31, "taser", kTypeTaser, 0},
    {32, "hkp2000", kTypePistol, 31}, {33, "mp7", kTypeSmg, 0},
    {34, "mp9", kTypeSmg, 0}, {35, "nova", kTypeShotgun, 0},
    {36, "p250", kTypePistol, 31}, {38, "scar20", kTypeSniper, 0},
    {39, "sg556", kTypeRifle, 0}, {40, "ssg08", kTypeSniper, 0},
    {42, "knife", kTypeKnife, 45}, {43, "flashbang", kTypeGrenade, 9},
    {44, "hegrenade", kTypeGrenade, 0}, {45, "smokegrenade", kTypeGrenade, 9},
    {46, "molotov", kTypeGrenade, 0}, {47, "decoy", kTypeGrenade, 0},
    {48, "incgrenade", kTypeGrenade, 12}, {49, "c4", kTypeBomb, 0},
    {59, "knife_t", kTypeKnife, 45}, {60, "m4a1_silencer", kTypeRifle, 0},
    {61, "usp_silencer", kTypePistol, 31}, {63, "cz75a", kTypePistol, 31},
    {64, "revolver", kTypePistol, 31},
    {500, "knife_bayonet", kTypeKnife, 45}, {503, "knife_css", kTypeKnife, 45},
    {505, "knife_flip", kTypeKnife, 45}, {506, "knife_gut", kTypeKnife, 45},
    {507, "knife_karambit", kTypeKnife, 45}, {508, "knife_m9_bayonet", kTypeKnife, 45},
    {509, "knife_tactical", kTypeKnife, 45}, {512, "knife_falchion", kTypeKnife, 45},
    {514, "knife_survival_bowie", kTypeKnife, 45}, {515, "knife_butterfly", kTypeKnife, 45},
    {516, "knife_push", kTypeKnife, 45}, {519, "knife_ursus", kTypeKnife, 45},
    {520, "knife_gypsy_jackknife", kTypeKnife, 45}, {522, "knife_stiletto", kTypeKnife, 45},
    {523, "knife_widowmaker", kTypeKnife, 45}, {525, "knife_skeleton", kTypeKnife, 45},
};

// Unscaled geometry. The icon height and every width go through the sliders; the container
// is sized "fit-children" exactly like the reference implementation's.
constexpr int kIconHeight = 16;
constexpr int kArmorHeight = 17;
constexpr int kHelmetHeight = 22;
constexpr int kLabelSize = 13;
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

// Bounded on purpose: the input is game memory, and a runaway loop here would build a
// string until the allocator threw.
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

// The installer is a port of velocity's `k_setup_script` (cs2/.../scoreboard_weapons.cpp):
// one script that defines a tiny message-passing client and a weapon manager, so every
// following frame only has to send a message instead of re-injecting the whole feature.
// The row lookup is CS:GO's own (sb-row + m_xuid, as in the gamesense Lua), because the
// CS2 ids ("player-<xuid>") do not exist in this client.
constexpr const char* kEngineTemplate = R"JS((function () {
	var layout = "@TAG@";

	// Already installed with these sliders: do nothing. The native side re-sends the engine
	// every few seconds so a HUD reload recovers, and in a context that never reloaded the
	// re-send must not throw away the state the scoreboard is drawn from.
	if (typeof SWeaponManager !== "undefined" && SWeaponManager && SWeaponManager.layout === layout &&
		typeof SWeaponManager.redraw === "function") {
		return;
	}

	// The sliders changed (or a HUD reload left a partial engine): carry the players over so
	// the rows are redrawn at the new size instead of staying empty until a loadout changes.
	var previous = null;
	if (typeof SWeaponManager !== "undefined" && SWeaponManager &&
		typeof SWeaponManager.snapshot === "function") {
		previous = SWeaponManager.snapshot();
	}

	if (typeof SClient !== "undefined") { SClient = undefined; }

	SClient = (function () {
		var handlers = {};
		return {
			register_handler: function (type, callback) { handlers[type] = callback; },
			receive: function (msg) {
				if (msg && handlers[msg.type]) handlers[msg.type](msg);
			}
		};
	})();

	SWeaponManager = (function () {
		var HIDDEN = ["id-sb-name__commendations__leader", "id-sb-name__commendations__teacher",
			"id-sb-name__commendations__friendly", "id-sb-name__musickit"];
		var TEAM_COLORS = { CT: "#B5D4EE40", TERRORIST: "#EAD18A61" };
		var CFG = {
			layout: layout,
			horizontal: @HORIZ@,
			iconHeight: @ICONH@,
			armorHeight: @ARMORH@,
			helmetHeight: @HELMETH@,
			labelSize: @LABELS@,
			minWidth: @PANELW@
		};
		var panels = {};        // key -> container panel
		var entries = {};       // key -> last content, so a rebuilt scoreboard can redraw
		var reportedMissing = {}; // key -> already complained about the row
		var reportedAttach = false;
		var reportedContext = false;

		var px = function (value) { return Math.round(value) + "px"; };

		// The scoreboard panel. When the script is injected with the scoreboard as its
		// context (what the reference does) this returns immediately; otherwise the CS:GO
		// layout path is followed, exactly like the gamesense Lua does.
		var scoreboard = function () {
			var root = $.GetContextPanel();
			if (!root) { return null; }
			if (root.id === "Scoreboard") { return root; }
			var container = root.FindChildTraverse("ScoreboardContainer");
			if (!container) { container = root.FindChildTraverse("id-eom-scoreboard-container"); }
			if (container) {
				var inside = container.FindChildTraverse("Scoreboard");
				if (inside) { return inside; }
			}
			return root.FindChildTraverse("Scoreboard");
		};

		var rows = function (sb) { return sb.FindChildrenWithClassTraverse("sb-row"); };

		// Names arrive from the client with colour codes, padding and mixed case, so they are
		// compared after the same normalisation on both sides.
		var normalize = function (value) {
			if (value === null || typeof value === "undefined") { return ""; }
			return String(value).replace(/[\u0000-\u001f]/g, " ").replace(/\s+/g, " ")
				.replace(/^\s+|\s+$/g, "").toLowerCase();
		};

		// Every string a row shows as its name: the dedicated label when the client has one,
		// the name cell itself and the cell's children otherwise. Bots in particular keep
		// their name in a child label, not in an id.
		var rowNames = function (row) {
			var out = [];
			var push = function (panel) {
				if (!panel) { return; }
				var text = panel.text;
				if (text) { out.push(text); }
			};
			var cell = nameCell(row);
			if (cell) {
				push(cell.FindChildTraverse("id-sb-name"));
				push(cell);
				var kids = cell.Children();
				for (var i = 0; i < kids.length; i++) { push(kids[i]); }
			}
			push(row.FindChildTraverse("id-sb-name"));
			return out.map(normalize).filter(function (value) { return value.length > 0; });
		};

		// CS:GO sets m_xuid on every row; the nickname covers rows whose xuid is not there
		// (and bots, which have none).
		var findRow = function (sb, xuid, nickname) {
			var list = rows(sb);
			if (xuid) {
				for (var i = 0; i < list.length; i++) {
					if (list[i].m_xuid && String(list[i].m_xuid) === String(xuid)) { return list[i]; }
				}
			}
			if (nickname) {
				var wanted = normalize(nickname);
				if (wanted) {
					for (var j = 0; j < list.length; j++) {
						if (rowNames(list[j]).indexOf(wanted) !== -1) { return list[j]; }
					}
				}
			}
			return null;
		};

		// "xuid=name, …" for the diagnostics: without the names there is no way to tell a
		// missing row from a row that is there under another name.
		var describeRows = function (sb) {
			return rows(sb).map(function (row) {
				var name = rowNames(row)[0] || "?";
				return (row.m_xuid || "?") + "=" + name;
			}).join(", ");
		};

		var nameCell = function (row) {
			var children = row.Children();
			for (var i = 0; i < children.length; i++) {
				if (children[i].GetAttributeString("data-stat", "") === "name") { return children[i]; }
			}
			return null;
		};

		var hideFlair = function (cell, hidden) {
			for (var i = 0; i < HIDDEN.length; i++) {
				var panel = cell.FindChildTraverse(HIDDEN[i]);
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

		// Icon widths per weapon class, straight from the reference table: a pistol, a
		// knife, a grenade and a rifle are drawn at different widths, not stretched to one.
		var sizeFor = function (weapon) {
			var type = weapon.type;
			var path = weapon.path || "";
			if (type === 1) {
				if (path.indexOf("usp_silencer") !== -1) { return 42; }
				if (path.indexOf("deagle") !== -1) { return 36; }
				if (path.indexOf("revolver") !== -1) { return 32; }
				if (path.indexOf("tec9") !== -1) { return 36; }
				if (path.indexOf("elite") !== -1) { return 32; }
				return 30;
			}
			if (type === 2 || type === 3 || type === 4 || type === 5 || type === 6) {
				return path.indexOf("mac10") !== -1 ? 30 : 52;
			}
			if (type === 9) {
				if (path.indexOf("incgrenade") !== -1 || path.indexOf("smokegrenade") !== -1) { return 14; }
				if (path.indexOf("molotov") !== -1 || path.indexOf("flashbang") !== -1) { return 16; }
				return 14;
			}
			if (type === 7) { return 22; }   // bomb
			if (type === 8) { return 18; }   // taser
			if (type === 10) { return 18; }  // defuse kit
			if (type === 0) { return 16; }   // knife
			return 16;
		};

		var addIcon = function (parent, path, type, active, wash) {
			var slot = $.CreatePanel("Panel", parent, "wep_" + String(path).replace(/[^a-zA-Z0-9]/g, "_"));
			slot.style.height = "fit-children";
			slot.style.width = "fit-children";
			slot.style.verticalAlign = "center";
			slot.style.margin = "0px 1px";

			var img = $.CreatePanel("Image", slot, "img");
			img.style.verticalAlign = "center";
			img.style.height = px(CFG.iconHeight);
			img.scaling = "stretch-aspect-preserve";
			var finalPath = String(path);
			if (finalPath.indexOf("file://") !== 0) {
				if (finalPath.indexOf("icons/equipment") === -1) { finalPath = "icons/equipment/" + finalPath; }
				if (finalPath.indexOf(".svg") === -1) { finalPath += ".svg"; }
				finalPath = "file://{images}/" + finalPath;
			}
			img.SetImage(finalPath);
			// The reference table is the width at 100%; the horizontal slider scales it, so a
			// wider panel really does make the row wider rather than only its minimum.
			var width = Math.round(sizeFor({ path: path, type: type }) * CFG.horizontal / 100);
			img.style.width = px(width < 1 ? 1 : width);
			slot.style.width = img.style.width;
			slot.style.opacity = active ? "1.0" : "0.35";
			if (wash) { img.style.washColorFast = wash; }
			return slot;
		};

		var addArmorIcon = function (parent, height, path, wash) {
			var img = $.CreatePanel("Image", parent, "equip_" + path);
			img.style.verticalAlign = "center";
			img.style.height = px(height);
			img.style.margin = "0px 1px";
			img.scaling = "stretch-aspect-preserve";
			img.SetImage("file://{images}/icons/equipment/" + path + ".svg");
			if (wash) { img.style.washColorFast = wash; }
			return img;
		};

		var build = function (container, entry) {
			container.RemoveAndDeleteChildren();

			var bg = $.CreatePanel("Panel", container, "box-bg");
			bg.style.width = "100%";
			bg.style.height = "100%";
			bg.style.backgroundColor = "rgba(0,0,0,0.35)";
			bg.style.borderRadius = "3px";

			var border = $.CreatePanel("Panel", container, "box-border");
			border.style.width = "100%";
			border.style.height = "100%";
			border.style.backgroundColor = "rgba(0,0,0,0)";
			border.style.borderRadius = "3px";
			border.style.border = "1px solid rgba(255,255,255,0.18)";

			var content = $.CreatePanel("Panel", container, "box-content");
			content.style.flowChildren = "left";
			content.style.height = "100%";
			content.style.width = "fit-children";
			content.style.padding = "0px 4px";
			content.style.verticalAlign = "center";
			content.style.horizontalAlign = "center";

			var wash = entry.wash || null;

			if (typeof entry.armor === "number" && entry.armor > 0) {
				addArmorIcon(content, CFG.armorHeight, "kevlar", wash);
				if (entry.armor >= 2) { addArmorIcon(content, CFG.helmetHeight, "helmet", wash); }
			}
			if (entry.defuser === 1) { addArmorIcon(content, CFG.armorHeight, "defuser", wash); }

			// Same order as the reference: primaries, then pistols, then everything else.
			var groups = [[], [], []];
			(entry.weapons || []).forEach(function (weapon) {
				var type = weapon.type;
				if (type === 2 || type === 3 || type === 4 || type === 5 || type === 6) {
					groups[0].push(weapon);
				} else if (type === 1) {
					groups[1].push(weapon);
				} else {
					groups[2].push(weapon);
				}
			});
			groups[0].concat(groups[1]).concat(groups[2]).forEach(function (weapon) {
				addIcon(content, weapon.path, weapon.type, weapon.path === entry.active, wash);
			});

			if (typeof entry.money === "number" && entry.money >= 0) {
				var money = $.CreatePanel("Label", content, "money");
				money.style.verticalAlign = "center";
				money.style.fontSize = px(CFG.labelSize);
				money.style.padding = "0px 2px";
				money.style.opacity = "0.7";
				money.text = "$" + entry.money;
			}
		};

		var containerFor = function (cell, key) {
			var id = "custom-weapons-container-" + key;
			var existing = cell.FindChildTraverse(id);
			if (existing) { return existing; }

			var container = $.CreatePanel("Panel", cell, id);
			container.AddClass("custom-weapons-container");
			container.style.flowChildren = "none";
			container.style.height = "fit-children";
			container.style.width = "fit-children";
			container.style.minWidth = px(CFG.minWidth);
			container.style.verticalAlign = "center";
			container.style.marginLeft = "3px";
			hideFlair(cell, true);
			var skillgroup = cell.FindChildTraverse("id-sb-skillgroup-image");
			if (skillgroup) { skillgroup.style.margin = "0px 0px 0px 5px"; }
			// Right after the nickname, before the flair columns: appended it would be
			// clipped by the row's own width.
			var first = cell.GetChild(1);
			if (first) { cell.MoveChildBefore(container, first); }
			return container;
		};

		var draw = function (msg) {
				var content = msg.content || {};
				var key = String(content.xuid || content.nickname);
				// The entry is remembered before the row is looked up: a row can be missing for a
				// while (the scoreboard is rebuilt on every round start) and the earlier version
				// dropped such a player for good, until his loadout happened to change again.
				entries[key] = content;

				var sb = scoreboard();
				if (!sb) {
					if (!reportedContext) {
						reportedContext = true;
						$.Msg("csgo-base: no scoreboard found from context \"" +
							($.GetContextPanel() ? $.GetContextPanel().id : "?") + "\"");
					}
					return;
				}
				var row = findRow(sb, content.xuid, content.nickname);
				if (!row) {
					if (!reportedMissing[key]) {
						reportedMissing[key] = true;
						$.Msg("csgo-base: no row for " + key + " (rows: " + describeRows(sb) + ")");
					}
					return;
				}
				var cell = nameCell(row);
				if (!cell) {
					if (!reportedMissing[key]) {
						reportedMissing[key] = true;
						$.Msg("csgo-base: row " + key + " has no name cell (cells: " +
							row.Children().map(function (child) {
								return child.GetAttributeString("data-stat", "?");
							}).join(",") + ")");
					}
					return;
				}
				var container = containerFor(cell, key);
				if (!container) { return; }
				container.style.visibility = "visible";
				build(container, content);
				panels[key] = container;
				delete reportedMissing[key];
				if (!reportedAttach) {
					reportedAttach = true;
					$.Msg("csgo-base: equipment attached to row " + key + " (" + rows(sb).length + " row(s))");
				}
			};

		return {
			update: function (msg) { draw(msg); },

			redraw: function () {
				var keys = [];
				for (var key in entries) { keys.push(key); }
				for (var i = 0; i < keys.length; i++) {
					if (entries[keys[i]]) { draw({ content: entries[keys[i]] }); }
				}
			},

			clear: function () {
				if (!sb) {
					if (!reportedContext) {
						reportedContext = true;
						$.Msg("csgo-base: no scoreboard found from context \"" +
							($.GetContextPanel() ? $.GetContextPanel().id : "?") + "\"");
					}
					return;
				}
				var row = findRow(sb, content.xuid, content.nickname);
				if (!row) {
					var missingKey = String(content.xuid || content.nickname);
					if (!reportedMissing[missingKey]) {
						reportedMissing[missingKey] = true;
						$.Msg("csgo-base: no row for " + missingKey + " (rows: " +
							rows(sb).map(function (r) { return r.m_xuid || "?"; }).join(",") + ")");
					}
					return;
				}
				var cell = nameCell(row);
				if (!cell) { return; }
				var key = String(content.xuid || content.nickname);
				var container = containerFor(cell, key);
				if (!container) { return; }
				container.style.visibility = "visible";
				build(container, content);
				panels[key] = container;
				if (!reportedAttach) {
					reportedAttach = true;
					$.Msg("csgo-base: equipment attached to row " + key + " (" + rows(sb).length + " row(s))");
				}
			},

			clear_one: function (msg) {
				var content = msg.content || {};
				var key = String(content.xuid || content.nickname);
				var panel = panels[key];
				if (panel && panel.IsValid()) {
					var parent = panel.GetParent();
					if (parent) { hideFlair(parent, false); }
					panel.DeleteAsync(0.0);
				}
				delete panels[key];
				delete entries[key];
			},

			layout: CFG.layout,

			// The state a reinstall has to carry over: everything received, drawn or not.
			snapshot: function () { return entries; },
			restore: function (state) {
				if (!state) { return; }
				for (var key in state) { entries[key] = state[key]; }
			},

			clear: function () {
				var sb = scoreboard();
				if (sb) {
					var found = sb.FindChildrenWithClassTraverse("custom-weapons-container");
					for (var i = 0; i < found.length; i++) {
						var parent = found[i].GetParent();
						if (parent) { hideFlair(parent, false); }
						found[i].DeleteAsync(0.0);
					}
				}
				panels = {};
				entries = {};
				reportedMissing = {};
				reportedAttach = false;
				reportedContext = false;
			}
		};
	})();

	SClient.register_handler("updateWeapons", function (msg) { SWeaponManager.update(msg); });
	SClient.register_handler("clearWeapons", function (msg) { SWeaponManager.clear_one(msg); });

	// A reinstalled engine must not stack handlers on the HUD. UnregisterForUnhandledEvent
	// takes the id RegisterForUnhandledEvent returned, not the callback - passing the callback
	// throws, and a throw here used to abort the rest of the installer (so after the first
	// reinstall the scoreboard events stayed dead). The ids are stored, every call is guarded,
	// and when the client hands out no ids the live handlers are kept as they are: they look
	// SWeaponManager up by name when they fire, so they already point at the new engine.
	if (typeof __sbHandlers === "undefined" || !__sbHandlers) { __sbHandlers = []; }
	if (__sbHandlers.length > 0) {
		var removable = true;
		for (var i = 0; i < __sbHandlers.length; i++) {
			if (typeof __sbHandlers[i].id !== "number") { removable = false; }
		}
		if (removable) {
			try {
				for (var j = 0; j < __sbHandlers.length; j++) {
					$.UnregisterForUnhandledEvent(__sbHandlers[j].name, __sbHandlers[j].id);
				}
				__sbHandlers = [];
			} catch (e) {
				$.Msg("csgo-base: could not unregister scoreboard handlers: " + e);
			}
		}
	}
	if (__sbHandlers.length === 0) {
		["OnOpenScoreboard", "Scoreboard_UpdateEverything", "Scoreboard_UpdateJob"].forEach(function (name) {
			var id = $.RegisterForUnhandledEvent(name, function () { SWeaponManager.redraw(); });
			__sbHandlers.push({ name: name, id: (typeof id === "number") ? id : null });
		});
	}

	if (previous) {
		SWeaponManager.restore(previous);
		SWeaponManager.redraw();
	}

	$.Msg("csgo-base: equipment engine installed (layout " + SWeaponManager.layout + ")");
})();)JS";

std::string EngineSource(const Scales& scales) {
    std::string script = kEngineTemplate;
    script = Replace(script, "@TAG@", Number(ClampScale(scales.x)) + "x" + Number(ClampScale(scales.y)) +
                                        "x" + Number(ClampScale(scales.weapon)));
    script = Replace(script, "@HORIZ@", Number(ClampScale(scales.x)));
    script = Replace(script, "@ICONH@", Number(Scale(kIconHeight, scales.weapon)));
    script = Replace(script, "@ARMORH@", Number(Scale(kArmorHeight, scales.y)));
    script = Replace(script, "@HELMETH@", Number(Scale(kHelmetHeight, scales.y)));
    script = Replace(script, "@LABELS@", Number(Scale(kLabelSize, scales.y)));
    script = Replace(script, "@PANELW@", Number(Scale(kPanelMinWidth, scales.x)));
    return script;
}

constexpr const char* kUpdatePrefix =
    "if (typeof SClient !== \"undefined\" && SClient && SClient.receive) { SClient.receive({type:\"updateWeapons\",content:";
constexpr const char* kClearPrefix =
    "if (typeof SClient !== \"undefined\" && SClient && SClient.receive) { SClient.receive({type:\"clearWeapons\",content:";
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

int WeaponTypeOf(const char* icon) {
    if (!icon) return -1;
    for (const auto& entry : kIcons) {
        const char* a = entry.name;
        const char* b = icon;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return entry.type;
    }
    return -1;
}

std::string BuildEngineScript(const Scales& scales) { return EngineSource(scales); }

// The scales are baked into the installer, so a message carries no geometry: it is just
// the state. The parameter stays for the call sites, which always have the scales at hand.
std::string BuildUpdateScript(const Equipment& player, const Scales& /*scales*/) {
    if (player.slot <= 0 || (player.xuid == 0 && player.name[0] == '\0')) return {};
    // Nothing to draw: the caller sends a clear instead, so the row does not end up with an
    // empty box around it.
    if (player.weapon_count == 0 && player.armor == 0 && !player.defuser && player.money < 0)
        return {};
    std::string script = kUpdatePrefix;
    script += "{\"xuid\":";
    script += player.xuid ? "\"" + std::to_string(player.xuid) + "\"" : "null";
    script += ",\"nickname\":";
    script += player.name[0] ? Quote(player.name) : "null";
    script += ",\"weapons\":[";
    const int count = player.weapon_count > kEquipmentSlots ? kEquipmentSlots : player.weapon_count;
    for (int i = 0; i < count; ++i) {
        if (i) script += ",";
        script += "{\"path\":";
        script += Quote(player.weapons[i]);
        script += ",\"type\":";
        script += std::to_string(player.weapon_types[i]);
        script += "}";
    }
    script += "],\"active\":";
    script += (player.selected >= 0 && player.selected < count)
                  ? Quote(player.weapons[player.selected])
                  : "null";
    script += ",\"armor\":";
    script += std::to_string(player.armor);
    script += ",\"defuser\":";
    script += player.defuser ? "1" : "0";
    if (player.money >= 0) {
        script += ",\"money\":";
        script += std::to_string(player.money);
    }
    // The team wash is applied from the client's own API when it is in scope; the script
    // leaves the icons untinted when it is not.
    script += "}}); }";
    return script;
}

std::string BuildClearScript(std::uint64_t xuid, const char* nickname) {
    if (xuid == 0 && (!nickname || !*nickname)) return {};
    std::string script = kClearPrefix;
    script += "{\"xuid\":";
    script += xuid ? "\"" + std::to_string(xuid) + "\"" : "null";
    script += ",\"nickname\":";
    script += (nickname && *nickname) ? Quote(nickname) : "null";
    script += "}}); }";
    return script;
}

std::string BuildFeedScript(const std::vector<Equipment>& players, const Scales& scales) {
    std::string script = EngineSource(scales);
    for (const auto& player : players) {
        const auto update = BuildUpdateScript(player, scales);
        if (update.empty()) continue;
        script += "\n";
        script += update;
    }
    return script;
}

std::string BuildResetScript() {
    return "if (typeof SWeaponManager !== \"undefined\" && SWeaponManager) { SWeaponManager.clear(); }\n"
           "if (typeof SClient !== \"undefined\") { SClient = undefined; }";
}
} // namespace Misc::Scoreboard
