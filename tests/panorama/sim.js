// Fake Panorama, just enough for the scoreboard script: panels with ids, classes, children,
// attributes and styles. Asserts what the script is supposed to do, including the parts
// that only the game would show: rows matched by xuid and by nickname, a rebuilt scoreboard
// redrawing itself, and the clear message removing a row again.
const fs = require('fs');
const path = require('path');

const dir = process.argv[2];
const dump = fs.readFileSync(path.join(dir, 'out.txt'), 'utf8');
function section(name) {
    const start = dump.indexOf('/*' + name + '*/');
    if (start < 0) throw new Error('missing section ' + name);
    const from = start + name.length + 4;
    const next = dump.indexOf('/*', from);
    return dump.slice(from, next < 0 ? dump.length : next).trim();
}
const engine = section('ENGINE');
const updates = section('UPDATES');
const clear = section('CLEAR');
const reset = section('RESET');

const failures = [];
function assert(condition, message) {
    if (condition) console.log('  ok  ' + message);
    else failures.push(message);
}

class Panel {
    constructor(type, id, parent, props) {
        this.type = type; this.id = id; this.parent = parent || null;
        this.children = []; this.classes = new Set(); this.style = {}; this.attrs = {};
        this.text = ''; this.visible = true; this.image = null; this.deleted = false;
        if (props && props.style) this.style.css = props.style;
        if (parent) parent.children.push(this);
    }
    IsValid() { return !this.deleted; }
    DeleteAsync() { this.deleted = true; if (this.parent) this.parent.children = this.parent.children.filter((c) => c !== this); }
    GetParent() { return this.parent; }
    Children() { return this.children.slice(); }
    GetChild(index) { return this.children[index] || null; }
    GetAttributeString(name, fallback) { return this.attrs[name] !== undefined ? this.attrs[name] : fallback; }
    SetImage(image) { this.image = image; }
    AddClass(name) { this.classes.add(name); }
    RemoveAndDeleteChildren() { this.children = []; }
    MoveChildBefore(child, before) {
        const from = this.children.indexOf(child);
        if (from < 0) throw new Error('MoveChildBefore: not a child of this panel');
        this.children.splice(from, 1);
        const to = this.children.indexOf(before);
        if (to < 0) this.children.push(child); else this.children.splice(to, 0, child);
        return true;
    }
    FindChild(id) { return this.children.find((c) => c.id === id) || null; }
    FindChildTraverse(id) {
        for (const child of this.children) {
            if (child.id === id) return child;
            const deep = child.FindChildTraverse(id);
            if (deep) return deep;
        }
        return null;
    }
    FindChildrenWithClassTraverse(cls) {
        let found = [];
        for (const child of this.children) {
            if (child.classes.has(cls)) found.push(child);
            found = found.concat(child.FindChildrenWithClassTraverse(cls));
        }
        return found;
    }
}

const hud = new Panel('Panel', 'CSGOHud');
const container = new Panel('Panel', 'ScoreboardContainer', hud);
const scoreboard = new Panel('Panel', 'Scoreboard', container);

function addRow(xuid, nickname) {
    const row = new Panel('Panel', 'sb-row', scoreboard);
    row.AddClass('sb-row');
    row.m_xuid = xuid;
    const nameCell = new Panel('Panel', 'name', row);
    nameCell.attrs['data-stat'] = 'name';
    const label = new Panel('Label', 'id-sb-name', nameCell);
    label.text = nickname;
    new Panel('Panel', 'id-sb-name__commendations__friendly', nameCell);
    new Panel('Image', 'id-sb-skillgroup-image', nameCell);
    const stat = new Panel('Panel', 'stat-kd', row);
    stat.attrs['data-stat'] = 'kd';
    return row;
}

const messages = [];
let created = 0;
global.__events = {};
global.$ = {
    GetContextPanel: () => hud,
    CreatePanel: (type, parent, id, props) => { created++; return new Panel(type, id, parent, props); },
    RegisterForUnhandledEvent: (name, fn) => { (global.__events[name] = global.__events[name] || []).push(fn); return fn; },
    UnregisterForUnhandledEvent: (name, fn) => {
        const list = global.__events[name] || [];
        const at = list.indexOf(fn);
        if (at >= 0) list.splice(at, 1);
    },
    Msg: (...args) => messages.push(args.join(' ')),
};
function fire(name) { for (const fn of (global.__events[name] || []).slice()) fn(); }

// ---- install ---------------------------------------------------------------------------
eval(engine);
assert(typeof SClient !== 'undefined', 'the engine defines SClient');
assert(typeof SWeaponManager !== 'undefined', 'the engine defines SWeaponManager');
assert(messages.some((m) => m.includes('equipment engine installed')), 'install is reported');
assert(created === 0, 'nothing is drawn before an update arrives');
assert(global.__events['OnOpenScoreboard'] && global.__events['OnOpenScoreboard'].length === 1,
    'the scoreboard events are registered once');

// A reinstall must not stack handlers.
eval(engine);
assert(global.__events['OnOpenScoreboard'].length === 1, 'reinstalling does not stack handlers');

// ---- updates ---------------------------------------------------------------------------
addRow('76561198000000003', 'enemy');
addRow('76561198000000007', 'mate');
eval(updates);

const enemy = scoreboard.FindChildTraverse('custom-weapons-container-76561198000000003');
const mate = scoreboard.FindChildTraverse('custom-weapons-container-76561198000000007');
assert(!!enemy && !!mate, 'both rows get a container');

function content(panel) { return panel.FindChild('box-content'); }
function icons(panel) { return content(panel).children.filter((c) => c.id && c.id.startsWith('wep_')); }
function imageOf(panel, name) { return content(panel).FindChild('wep_' + name).FindChild('img'); }

const enemyIcons = icons(enemy);
assert(enemyIcons.length === 3, 'three icons for three carried items');
assert(enemy.FindChild('box-bg') && enemy.FindChild('box-border'), 'the reference background and border are drawn');
assert(imageOf(enemy, 'ak47').image.endsWith('/ak47.svg'), 'the rifle uses its own icon');
assert(imageOf(enemy, 'knife_karambit').image.endsWith('/knife_karambit.svg'), 'the knife uses its own icon');
assert(imageOf(enemy, 'flashbang').image.endsWith('/flashbang.svg'), 'the grenade uses its own icon');
// Type-based sizing: a rifle is wide, a grenade narrow, a knife in between.
assert(imageOf(enemy, 'ak47').style.width === '52px', 'the rifle takes the wide icon width');
assert(imageOf(enemy, 'flashbang').style.width === '16px', 'the grenade takes the narrow width');
assert(imageOf(enemy, 'knife_karambit').style.width === '16px', 'the knife takes the narrow width');
assert(enemyIcons[0].style.opacity === '1.0', 'the active weapon is drawn at full opacity');
assert(enemyIcons[1].style.opacity === '0.35', 'the rest are dimmed');
assert(enemyIcons[0].FindChild('img').scaling === 'stretch-aspect-preserve', 'icons keep their aspect ratio');
// Order: primaries, then pistols, then everything else.
assert(enemyIcons.map((c) => c.id).join(',') === 'wep_ak47,wep_knife_karambit,wep_flashbang',
    'the primary is drawn before the knife and the grenade');
const armor = content(enemy).FindChild('equip_kevlar');
const helmet = content(enemy).FindChild('equip_helmet');
const defuser = content(enemy).FindChild('equip_defuser');
assert(armor && armor.image.endsWith('/kevlar.svg'), 'kevlar is drawn for the enemy');
assert(helmet && helmet.image.endsWith('/helmet.svg'), 'the helmet is a separate image');
assert(defuser && defuser.image.endsWith('/defuser.svg'), 'the defuse kit is drawn');
const money = content(enemy).FindChild('money');
assert(money && money.text === '$4321', 'the money label carries the value');

const nameCell = scoreboard.FindChildrenWithClassTraverse('sb-row')[0].children[0];
assert(nameCell.children[1] === enemy, 'the container sits right after the nickname');
assert(nameCell.FindChildTraverse('id-sb-name__commendations__friendly').style.maxWidth === '0px',
    'unused flair is hidden');
assert(messages.some((m) => m.includes('equipment attached to row')), 'the attach is reported');

// ---- a rebuilt scoreboard redraws from the stored state --------------------------------
scoreboard.RemoveAndDeleteChildren();
addRow('76561198000000003', 'enemy');
addRow('76561198000000007', 'mate');
fire('OnOpenScoreboard');
const redrawn = scoreboard.FindChildTraverse('custom-weapons-container-76561198000000003');
assert(!!redrawn && icons(redrawn).length === 3, 'a rebuilt scoreboard is redrawn without a new message');

// ---- clear -----------------------------------------------------------------------------
eval(clear);
assert(!scoreboard.FindChildTraverse('custom-weapons-container-76561198000000003'),
    'the clear message removes that row only');
assert(!!scoreboard.FindChildTraverse('custom-weapons-container-76561198000000007'),
    'the other row is untouched');

// ---- rows matched by nickname (bots have no xuid) --------------------------------------
addRow(null, 'bot_alice');
eval('SClient.receive({type:"updateWeapons",content:{xuid:null,nickname:"bot_alice",weapons:[{path:"glock",type:1}],active:"glock",armor:0,defuser:0}});');
assert(!!scoreboard.FindChildTraverse('custom-weapons-container-bot_alice'),
    'a row without m_xuid is matched by its nickname');

// ---- reset -----------------------------------------------------------------------------
eval(reset);
assert(scoreboard.FindChildrenWithClassTraverse('custom-weapons-container').length === 0,
    'the reset removes every container');
assert(typeof SClient === 'undefined', 'the reset drops the client so a re-enable reinstalls');

if (failures.length) {
    console.error('FAIL scoreboard_sim');
    for (const failure of failures) console.error('  - ' + failure);
    process.exit(1);
}
console.log('PASS scoreboard_sim');
