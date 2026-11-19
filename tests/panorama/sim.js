// Fake Panorama, just enough for the scoreboard script: panels with ids, classes, children,
// attributes, styles and BLoadLayoutFromString. Asserts what the script is supposed to do.
const fs = require('fs');
const path = require('path');

const dir = process.argv[2];
const split = fs.readFileSync(path.join(dir, 'out.txt'), 'utf8').split('/*SPLIT*/');
const engine = split[0];
const feed = split[1];

class Panel {
    constructor(type, id, parent, props) {
        this.type = type; this.id = id; this.parent = parent || null;
        this.children = []; this.classes = new Set(); this.style = {}; this.attrs = {};
        this.text = ''; this.visible = true; this.image = null; this.deleted = false;
        if (props) {
            if (props.style) this.style.css = props.style;
            if (props.textureheight) this.style.textureheight = props.textureheight;
        }
        if (parent) parent.children.push(this);
    }
    IsValid() { return !this.deleted; }
    DeleteAsync() { this.deleted = true; }
    GetParent() { return this.parent; }
    Children() { return this.children.slice(); }
    GetChild(index) { return this.children[index] || null; }
    GetAttributeString(name, fallback) { return this.attrs[name] !== undefined ? this.attrs[name] : fallback; }
    SetImage(image) { this.image = image; }
    AddClass(name) { this.classes.add(name); }
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
    BLoadLayoutFromString(xml) {
        // Only the image ids matter: that is what the script switches between.
        const ids = [...xml.matchAll(/<Image id="([a-z]+)"/g)].map((m) => m[1]);
        if (ids.length !== 4) failures.push('slot layout must declare four size classes');
        for (const id of ids) {
            const image = new Panel('Image', id, this);
            image.style.opacity = '0.01';
        }
        return true;
    }
}

const failures = [];
function assert(condition, message) {
    if (!condition) { failures.push(message); }
    else console.log('  ok  ' + message);
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
global.GameStateAPI = {
    GetPlayerXuidStringFromEntIndex: (index) => ({ 3: '76561198000000003', 7: '76561198000000007' }[index] || '0'),
    GetPlayerTeamName: (xuid) => (xuid.endsWith('3') ? 'TERRORIST' : 'CT'),
};
let created = [];
global.__events = {};
global.$ = {
    GetContextPanel: () => hud,
    CreatePanel: (type, parent, id, props) => { const panel = new Panel(type, id, parent, props); created.push(panel); return panel; },
    RegisterForUnhandledEvent: (name, fn) => { (global.__events[name] = global.__events[name] || []).push(fn); return fn; },
    UnregisterForUnhandledEvent: () => {},
    Msg: (...args) => messages.push(args.join(' ')),
};

addRow('76561198000000003', 'enemy');
addRow('76561198000000007', 'mate');

eval(engine);
assert(global.__sb, 'the engine installs itself');
assert(messages.some((m) => m.includes('equipment engine installed')), 'install is reported');
assert(created.length === 0, 'nothing is created before data arrives');

eval(feed);
const enemyPanel = global.__sb.panels['x76561198000000003'];
assert(!!enemyPanel, 'the enemy row gets a panel (keyed by xuid)');
assert(!!global.__sb.panels['x76561198000000007'], 'the teammate row gets a panel too');

const slots = enemyPanel.children.filter((c) => /^weapon-\d+$/.test(c.id));
assert(slots.length === 25, 'twenty-five slot parents, as in the reference');
// The slots are created back to front, so they are ordered by their index before checking.
const shown = slots.filter((s) => s.visible)
    .sort((a, b) => Number(a.id.split('-')[1]) - Number(b.id.split('-')[1]));
assert(shown.length === 3, 'exactly the three carried items are visible');
const images = shown.map((slot) => slot.children.find((c) => c.visible));
assert(images[0] && images[0].image.endsWith('/ak47.svg'), 'the rifle uses the ak47 icon');
assert(images[1] && images[1].image.endsWith('/knife_karambit.svg'), 'the knife uses its own icon');
assert(images[2] && images[2].image.endsWith('/flashbang.svg'), 'the grenade uses its own icon');
assert(images[0].style.opacity === '0.85', 'the selected weapon is drawn brighter');
assert(images[1].style.opacity === '0.35', 'the rest are dimmed');
assert(images[1].id === 'smaller', 'the knife uses the wide size class');
assert(images[0].id === 'small', 'the rifle uses the default size class');
assert(images[0].style.washColorFast === '#EAD18A61', 'the icon is washed with the team colour');

const nameCell = scoreboard.FindChildrenWithClassTraverse('sb-row')[0].children[0];
assert(nameCell.children[1] === enemyPanel, 'the panel sits in the first column, not appended');
const flair = nameCell.FindChildTraverse('id-sb-name__commendations__friendly');
assert(flair.style.maxWidth === '0px' && flair.style.margin === '0px', 'unused flair is hidden');

const armor = enemyPanel.FindChild('armor');
const helmet = enemyPanel.FindChild('helmet');
const money = enemyPanel.FindChild('money');
assert(armor.visible && armor.image.endsWith('/kevlar.svg'), 'kevlar is drawn with its own icon');
assert(helmet.visible && helmet.image.endsWith('/helmet.svg'), 'the helmet is a separate image');
assert(money.visible && money.text === '$4321', 'the money label carries the value');

// A second feed without the enemy hides his panel instead of leaving stale icons.
eval(engine + '\n__sb.feed({"7":{"w":["m4a1"],"s":0,"a":1,"d":0}});');
assert(global.__sb.panels['x76561198000000003'].visible === false, 'a player without equipment is hidden');
assert(global.__sb.panels['x76561198000000007'].visible === true, 'the remaining player stays visible');

// Without GameStateAPI the script falls back to nickname keys, so a HUD context without the
// API still works (the payload carries the nickname for exactly this case).
delete global.GameStateAPI;
addRow(null, 'enemy');
eval(engine + '\n__sb.feed({"3":{"w":["ak47"],"s":0,"a":0,"d":0,"n":"enemy"}});');
assert(!!global.__sb.panels['nenemy'], 'a row without m_xuid is matched by its nickname');

if (failures.length) {
    console.error('FAIL scoreboard_sim');
    for (const failure of failures) console.error('  - ' + failure);
    process.exit(1);
}
console.log('PASS scoreboard_sim');
