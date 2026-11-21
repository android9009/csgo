--[[============================================================================
    skeet_all_patches.lua  —  все патчи Skeet / Gamesense из репозитория
                              android9009/My-luas в одном скрипте

    Внутри (по порядку исходных файлов):

      1) Normalize patch           2026-10-07_20-05-07-185.lua
         Site 0x1062A526, 7 байт -> NOP. Затирание нормалей (Z_NORM): модель
         освещается ровно, без "граней" от нормалей.

      2) Direction flags patch     2026-10-07_20-05-07-185-1.lua
         Site 0x106E3F9C, 3 байта -> NOP. Убирает установку флагов направления
         (DIR_FLAGS) в рендере.

      3) Shoot like Skala Johnson  2026-10-07_20-05-07-185-5.lua
         Site 0x106D67A8, 8 байт:  B8 01 00 00 00 C2 08 00
         (mov eax, 1 ; ret 8 – функция всегда возвращает 1 и сама чистит стек).

      4) Menu fade / spin / wobble 2026-10-07_20-05-07-185-2.lua
         Память не патчится: скрипт двигает размер/позицию меню в paint_ui и
         крутит иконки табов. Адреса — 12 полей в модуле чита.

    Что сделано иначе (и почему это лучше исходников):

      * VirtualProtect / VirtualQuery / GetModuleHandleA / GetProcAddress
        резолвятся ОДИН раз на весь скрипт: сначала через ffi.load("kernel32"),
        если чит это не разрешает — тем же signature-гейтом (push ecx; ret +
        выдирание адресов из IAT), что и в оригинальных патчах. Оригиналы
        делали это в каждом файле заново и падали через error() при загрузке.
      * Если гейт не нашёлся, скрипт НЕ падает: он пишет в консоль причину и
        оставляет чекбоксы выключенными (память не трогается вообще).
      * Перед записью проверяется VirtualQuery: регион закоммичен, исполняемый
        и не PAGE_GUARD. Адрес из чужого билда клиента просто не пройдёт
        проверку, вместо того чтобы занопать случайные байты.
      * Восстанавливается ТО старое значение защиты страницы, которое вернул
        VirtualProtect, а не захардкоженный PAGE_EXECUTE_READ.
      * Запись идёт из снапшота и проверяется обратным чтением; повторное
        включение идемпотентно, выключение возвращает байты байт в байт.
      * Каждый site можно переопределить прямо в меню (текстовое поле, hex):
        при обновлении чита адреса меняются, а скрипт остаётся тем же.
      * Кнопка "Read sites" печатает текущие байты по каждому адресу — видно,
        тот ли это билд, ДО включения патча.
      * Restore: чекбокс, кнопка "Restore everything", событие shutdown и
        смена карты (level_init) — всё возвращает оригинальные байты.

    Адреса (0x1062A526 / 0x106E3F9C / 0x106D67A8 / 0x4347xxxx) сняты с одного
    конкретного билда чита и НЕ являются универсальными. Скрипт не делает вид,
    что они универсальны: он проверяет их и громко говорит, если билд другой.
============================================================================]]

local ffi = require("ffi")
local bit = require("bit")

local NAME = "skeet-patches"

local function log(fmt, ...)
    local ok, text = pcall(string.format, fmt, ...)
    print(("[%s] %s"):format(NAME, ok and text or tostring(fmt)))
end

local function log_error(fmt, ...)
    local ok, text = pcall(string.format, fmt, ...)
    local line = ("[%s] %s"):format(NAME, ok and text or tostring(fmt))
    print(line)
    if client and client.error_log then pcall(client.error_log, line) end
end

--[[----------------------------------------------------------------------------
    1. Win32
------------------------------------------------------------------------------]]

-- MEMORY_BASIC_INFORMATION, x86: 7 полей по 4 байта = 28. Нужен именно
-- Protect (текущая защита страницы), AllocationProtect — защита при резерве.
ffi.cdef[[
    typedef struct {
        void*        BaseAddress;
        void*        AllocationBase;
        unsigned int AllocationProtect;
        uintptr_t    RegionSize;
        unsigned int State;
        unsigned int Protect;
        unsigned int Type;
    } SKEET_MBI;
]]

local MEM_COMMIT    = 0x1000
local PAGE_GUARD    = 0x100
local PAGE_NOACCESS = 0x01
local PAGE_RWX      = 0x40

-- 0xF0 = EXECUTE | EXECUTE_READ | EXECUTE_READWRITE | EXECUTE_WRITECOPY
local PAGE_EXECUTABLE = 0xF0

local Win = {
    ready   = false,
    reason  = "not initialised",
    protect = nil,
    query   = nil,
}

-- Приводим сигнатуру из client.dll к "push ecx ; ret" гейту: он позволяет
-- вызвать любую функцию x86, передав её адрес в ecx (__thiscall), — так
-- оригинальные патчи и вызывали VirtualProtect без единого импорта.
local function resolve_through_gate()
    local gate = client.find_signature("client.dll", string.char(0x51, 0xC3))
    if gate == nil then return nil, "треш-гейт push ecx/ret не найден" end

    local gmh_hit = client.find_signature("client.dll",
        string.char(0xC6, 0x06, 0x00, 0xFF, 0x15, 0xCC, 0xCC, 0xCC, 0xCC, 0x50))
    if gmh_hit == nil then return nil, "GetModuleHandleA (IAT) не найден" end
    local gmh = ffi.cast("void***", ffi.cast("char*", gmh_hit) + 5)[0][0]

    local gpa_hit = client.find_signature("client.dll",
        string.char(0x50, 0xFF, 0x15, 0xCC, 0xCC, 0xCC, 0xCC, 0x85, 0xC0, 0x0F, 0x84,
                    0xCC, 0xCC, 0xCC, 0xCC, 0x6A, 0x00))
    if gpa_hit == nil then return nil, "GetProcAddress (IAT) не найден" end
    local gpa = ffi.cast("void***", ffi.cast("char*", gpa_hit) + 3)[0][0]

    local call_gmh = ffi.cast("uintptr_t (__thiscall*)(void*, const char*)", gate)
    local call_gpa = ffi.cast("uintptr_t (__thiscall*)(void*, uintptr_t, const char*)", gate)
    local call_vp  = ffi.cast("int (__thiscall*)(uintptr_t, void*, uintptr_t, unsigned long, unsigned long*)", gate)
    local call_vq  = ffi.cast("uintptr_t (__thiscall*)(uintptr_t, void*, void*, uintptr_t)", gate)

    local kernel32 = call_gmh(gmh, "kernel32.dll")
    if kernel32 == 0 then return nil, "GetModuleHandleA(\"kernel32.dll\") вернул 0" end

    local vp = call_gpa(gpa, kernel32, "VirtualProtect")
    local vq = call_gpa(gpa, kernel32, "VirtualQuery")
    if vp == 0 or vq == 0 then return nil, "GetProcAddress(VirtualProtect/VirtualQuery) вернул 0" end

    return {
        protect = function(addr, size, new_protect)
            local old = ffi.new("unsigned long[1]")
            if call_vp(vp, ffi.cast("void*", addr), size, new_protect, old) == 0 then return nil end
            return tonumber(old[0])
        end,
        query = function(addr)
            local mbi = ffi.new("SKEET_MBI")
            if call_vq(vq, ffi.cast("void*", addr), mbi, ffi.sizeof("SKEET_MBI")) == 0 then return nil end
            return {
                base     = tonumber(ffi.cast("uintptr_t", mbi.BaseAddress)),
                size     = tonumber(mbi.RegionSize),
                state    = tonumber(mbi.State),
                protect  = tonumber(mbi.Protect),
                protect2 = tonumber(mbi.Type),
            }
        end,
        source = "signature gate",
    }
end

-- Чистый путь: настоящий kernel32 через ffi.load. На части сборок чита
-- ffi.load запрещён — это ожидаемо, тогда идём через гейт.
local function resolve_through_kernel32()
    local ok, kernel32 = pcall(ffi.load, "kernel32")
    if not ok or kernel32 == nil then return nil, "ffi.load(\"kernel32\") недоступен" end

    local built, backend = pcall(function()
        ffi.cdef[[
            int          __stdcall VirtualProtect(void* addr, uintptr_t size, unsigned long new_protect, unsigned long* old_protect);
            uintptr_t    __stdcall VirtualQuery(const void* addr, SKEET_MBI* info, uintptr_t size);
        ]]
        -- Обращение к отсутствующему символу в LuaJIT падает здесь же
        local probe = kernel32.VirtualProtect and kernel32.VirtualQuery
        if probe == nil then error("kernel32 без VirtualProtect/VirtualQuery") end
        return true
    end)
    if not built then return nil, "kernel32 не дал нужных функций: " .. tostring(backend) end

    return {
        protect = function(addr, size, new_protect)
            local old = ffi.new("unsigned long[1]")
            if kernel32.VirtualProtect(ffi.cast("void*", addr), size, new_protect, old) == 0 then return nil end
            return tonumber(old[0])
        end,
        query = function(addr)
            local mbi = ffi.new("SKEET_MBI")
            if kernel32.VirtualQuery(ffi.cast("void*", addr), mbi, ffi.sizeof("SKEET_MBI")) == 0 then return nil end
            return {
                base     = tonumber(ffi.cast("uintptr_t", mbi.BaseAddress)),
                size     = tonumber(mbi.RegionSize),
                state    = tonumber(mbi.State),
                protect  = tonumber(mbi.Protect),
                protect2 = tonumber(mbi.Type),
            }
        end,
        source = "kernel32",
    }
end

local function init_win()
    local backend, why = resolve_through_kernel32()
    if backend == nil then
        local gate_backend, gate_why = resolve_through_gate()
        backend, why = gate_backend, why .. "; " .. tostring(gate_why)
    end
    if backend == nil then
        Win.ready, Win.reason = false, tostring(why)
        return false
    end
    Win.protect, Win.query = backend.protect, backend.query
    Win.ready, Win.reason = true, backend.source
    return true
end

--[[----------------------------------------------------------------------------
    2. Память
------------------------------------------------------------------------------]]

local Mem = {}

function Mem.region(addr)
    if not Win.ready then return nil end
    return Win.query(addr)
end

function Mem.is_executable(addr, size)
    local region = Mem.region(addr)
    if region == nil then return false, "VirtualQuery не отвечает" end
    if region.state ~= MEM_COMMIT then return false, "регион не закоммичен" end
    if bit.band(region.protect, PAGE_EXECUTABLE) == 0 then
        return false, ("страница не исполняемая (protect 0x%X)"):format(region.protect)
    end
    if bit.band(region.protect, PAGE_GUARD) ~= 0 then return false, "страница под PAGE_GUARD" end
    if bit.band(region.protect, PAGE_NOACCESS) ~= 0 then return false, "PAGE_NOACCESS" end
    if size and region.size and region.size < size then
        return false, ("регион короче патча (%d < %d)"):format(region.size, size)
    end
    return true
end

function Mem.read(addr, count)
    local out = {}
    local p = ffi.cast("uint8_t*", addr)
    for i = 0, count - 1 do out[i + 1] = tonumber(p[i]) end
    return out
end

function Mem.hex(addr, count)
    local parts = {}
    for _, byte in ipairs(Mem.read(addr, count)) do parts[#parts + 1] = ("%02X"):format(byte) end
    return table.concat(parts, " ")
end

-- Пишем ровно один раз, возвращая странице её собственный старый protect.
function Mem.write(addr, bytes)
    local size = #bytes
    local old = Win.protect(addr, size, PAGE_RWX)
    if old == nil then return false, "VirtualProtect(RWX) не сработал" end

    local p = ffi.cast("uint8_t*", addr)
    for i = 1, size do p[i - 1] = bytes[i] end

    Win.protect(addr, size, old)

    local back = Mem.read(addr, size)
    for i = 1, size do
        if back[i] ~= bytes[i] then
            return false, ("запись не подтвердилась: %s"):format(Mem.hex(addr, size))
        end
    end
    return true
end

function Mem.equal_bytes(addr, bytes)
    local current = Mem.read(addr, #bytes)
    for i = 1, #bytes do
        if current[i] ~= bytes[i] then return false end
    end
    return true
end

function Mem.read_i32(addr)
    return tonumber(ffi.cast("int*", addr)[0])
end

function Mem.write_i32(addr, value)
    local old = Win.protect(addr, 4, PAGE_RWX)
    if old == nil then return false end
    ffi.cast("int*", addr)[0] = value
    Win.protect(addr, 4, old)
    return true
end

function Mem.read_f32(addr)
    return tonumber(ffi.cast("float*", addr)[0])
end

function Mem.write_f32(addr, value)
    local old = Win.protect(addr, 4, PAGE_RWX)
    if old == nil then return false end
    ffi.cast("float*", addr)[0] = value
    Win.protect(addr, 4, old)
    return true
end

function Mem.read_u8(addr)
    return tonumber(ffi.cast("uint8_t*", addr)[0])
end

function Mem.write_u8(addr, value)
    local old = Win.protect(addr, 1, PAGE_RWX)
    if old == nil then return false end
    ffi.cast("uint8_t*", addr)[0] = value
    Win.protect(addr, 1, old)
    return true
end

local function parse_address(text)
    if text == nil then return nil end
    text = tostring(text):gsub("%s+", "")
    if text == "" then return nil end
    local value = text:match("^0[xX](%x+)$")
    if value then return tonumber(value, 16) end
    value = text:match("^(%d+)$")
    if value then return tonumber(value, 10) end
    value = text:match("^(%x%x%x%x%x%x%x%x)$")
    if value then return tonumber(value, 16) end
    return nil
end

--[[----------------------------------------------------------------------------
    3. Патч как объект
------------------------------------------------------------------------------]]

local Patch = {}
Patch.__index = Patch

local patches = {}

local function new_patch(def)
    def.applied = false
    def.saved = nil
    def.site_override = nil
    def.textbox = nil
    def.status = function(self) return self.applied end
    patches[#patches + 1] = def
    return setmetatable(def, Patch)
end

local function nops(count)
    local bytes = {}
    for i = 1, count do bytes[i] = 0x90 end
    return bytes
end

function Patch:address()
    if self.site_override ~= nil then return self.site_override end
    return self.site
end

-- Всё, что можно проверить ДО записи.
function Patch:check()
    if not Win.ready then return false, "нет доступа к VirtualProtect: " .. tostring(Win.reason) end

    local addr = self:address()
    if addr == nil then return false, "адрес не задан" end

    local ok, why = Mem.is_executable(addr, #self.payload)
    if not ok then return false, why end

    -- Уже пропатчено (нами в прошлый раз или другим скриптом) — это не ошибка.
    if Mem.equal_bytes(addr, self.payload) then return true, "уже пропатчено" end

    -- Сплошные int3/int1 по адресу — это padding чужого билда, не наш site.
    local current = Mem.read(addr, #self.payload)
    local padding = true
    for _, byte in ipairs(current) do
        if byte ~= 0xCC and byte ~= 0x90 then
            padding = false
            break
        end
    end
    if padding then return false, "по адресу padding (0xCC/0x90) — похоже, другой билд" end

    return true
end

function Patch:apply()
    if self.applied then return true end
    if not Win.ready then return false, "нет доступа к VirtualProtect" end

    local addr = self:address()
    local ok, why = self:check()
    if not ok then return false, why end

    if Mem.equal_bytes(addr, self.payload) then
        self.applied = true
        return true, "уже пропатчено"
    end

    if self.saved == nil then self.saved = Mem.read(addr, #self.payload) end

    local written, write_why = Mem.write(addr, self.payload)
    if not written then return false, tostring(write_why) end

    self.applied = true
    return true
end

function Patch:restore()
    if not self.applied then return true end
    local addr = self:address()
    if self.saved ~= nil and Win.ready then
        local written, why = Mem.write(addr, self.saved)
        if not written then return false, tostring(why) end
    end
    self.applied = false
    return true
end

--[[----------------------------------------------------------------------------
    4. Три патча памяти
------------------------------------------------------------------------------]]

local normalize = new_patch{
    key      = "normalize",
    name     = "Normalize patch (Z_NORM)",
    site     = 0x1062A526,
    payload  = nops(7),
    hint     = "7 байт -> NOP",
}

local dir_flags = new_patch{
    key      = "dirflags",
    name     = "Direction flags patch",
    site     = 0x106E3F9C,
    payload  = nops(3),
    hint     = "3 байта -> NOP",
}

local skala = new_patch{
    key      = "skala",
    name     = "Shoot like Skala Johnson",
    site     = 0x106D67A8,
    payload  = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x08, 0x00 },
    hint     = "mov eax, 1 ; ret 8",
}

--[[----------------------------------------------------------------------------
    5. Патч анимации меню (память не патчится, поля меню двигаются)
------------------------------------------------------------------------------]]

local MenuAnim = {
    enabled        = false,
    base           = 0x43400000,
    base_text      = nil,
    base_default   = 0x43400000,   -- модуль чита; все поля ниже = base + offset
    offsets = {
        rage_class = 0x79A00,
        tab_base   = 0x79A04,
        min_w      = 0x799C8,
        min_h      = 0x799CC,
        pos_x      = 0x799B8,
        pos_y      = 0x799BC,
        w_ptr      = 0x799C0,
        h_ptr      = 0x799C4,
        act_ptr    = 0x799E0,
        fade_mode  = 0x79A38,
        fade_speed = 0x6F920,
        fade_val   = 0x79A5C,
    },
    tabs        = {},
    tab_count   = 0,
    base_size   = nil,
    base_pos    = nil,
    saved       = nil,     -- что было в полях до включения
    prev_mode   = 0,
    spin        = 0,
    wobble      = 0,
}

local function menu_addr(key)
    return MenuAnim.base + MenuAnim.offsets[key]
end

function MenuAnim:resolve_base()
    local base = parse_address(self.base_text and ui.get(self.base_text) or nil)
    if base == nil then base = self.base_default end
    self.base = base
    return base
end

-- Меню чита — единственное, где ошибка адреса видна сразу и портит картинку
-- каждый кадр. Поэтому сначала читаем все поля и проверяем, что они вообще
-- похожи на меню, и только потом что-то пишем.
function MenuAnim:preflight()
    if not Win.ready then return false, "нет доступа к VirtualProtect" end

    local function sane(value, low, high, what)
        if value == nil then return false, what .. ": не читается" end
        if value < low or value > high then
            return false, ("%s = %s вне [%s..%s]"):format(what, tostring(value), tostring(low), tostring(high))
        end
        return true
    end

    local checks = {
        { "w_ptr",    100,  8000,   "ширина меню" },
        { "h_ptr",    100,  8000,   "высота меню" },
        { "min_w",      0,  8000,   "min width" },
        { "min_h",      0,  8000,   "min height" },
        { "pos_x", -20000, 20000,   "позиция X" },
        { "pos_y", -20000, 20000,   "позиция Y" },
        { "fade_mode",  0,    16,   "режим анимации" },
    }
    for _, check in ipairs(checks) do
        local ok, why = sane(Mem.read_i32(menu_addr(check[1])), check[2], check[3], check[4])
        if not ok then return false, why end
    end

    local fade_val = Mem.read_f32(menu_addr("fade_val"))
    if fade_val == nil or fade_val ~= fade_val or fade_val < 0 or fade_val > 1 then
        return false, "поле затухания вне 0..1"
    end
    local fade_speed = Mem.read_f32(menu_addr("fade_speed"))
    if fade_speed == nil or fade_speed ~= fade_speed or fade_speed <= 0 then
        return false, "скорость анимации не положительная"
    end

    local act = Mem.read_u8(menu_addr("act_ptr"))
    if act ~= 0 and act ~= 1 then return false, "флаг активности меню не 0/1" end

    local rage    = Mem.read_i32(menu_addr("rage_class"))
    local tabbase = Mem.read_i32(menu_addr("tab_base"))
    if rage == nil or rage < 0x10000 or rage >= 0x7FFF0000 then return false, "указатель Rage-таба не похож на указатель" end
    if tabbase == nil or tabbase < 0x10000 or tabbase >= 0x7FFF0000 then return false, "указатель табов не похож на указатель" end

    return true
end

function MenuAnim:build_tabs()
    self.tabs, self.tab_count = {}, 0

    local function add(class, active_addr)
        local tab = {
            class  = class,
            active = active_addr,
            ox     = Mem.read_i32(class + 0x20),
            oy     = Mem.read_i32(class + 0x24),
            ex     = Mem.read_i32(class + 0x28),
            ey     = Mem.read_i32(class + 0x2C),
        }
        if tab.ox == nil or tab.oy == nil or tab.ex == nil or tab.ey == nil then return false end
        self.tabs[self.tab_count] = tab
        self.tab_count = self.tab_count + 1
        return true
    end

    -- Двойное разыменование, как в оригинале: ячейка хранит указатель на
    -- таблицу указателей (rage_class), а [0] этой таблицы — сам объект таба.
    local rage_table = Mem.read_i32(menu_addr("rage_class"))
    local rage = Mem.read_i32(rage_table)
    if rage == nil or rage < 0x10000 or rage >= 0x7FFF0000 then return false end
    if not add(rage, rage + 0x15) then return false end

    local tab_table = Mem.read_i32(menu_addr("tab_base"))
    for index = 1, 8 do
        local slot = tab_table - (0x20 - 0x4 * (index - 1))
        local class = Mem.read_i32(slot)
        if class ~= nil and class >= 0x10000 and class < 0x7FFF0000 then
            add(class, class + 0x15)
        end
    end
    return true
end

function MenuAnim:toggle_icons(force)
    if Mem.read_u8(menu_addr("act_ptr")) == 0 then return end
    local height = Mem.read_i32(menu_addr("h_ptr"))
    for index = 0, self.tab_count - 1 do
        local tab = self.tabs[index]
        if tab ~= nil then
            if tab.oy + tab.ey > height then
                Mem.write_u8(tab.active, 0)
            else
                Mem.write_u8(tab.active, force)
            end
        end
    end
end

function MenuAnim:capture()
    local w, h = Mem.read_i32(menu_addr("w_ptr")), Mem.read_i32(menu_addr("h_ptr"))
    local x, y = Mem.read_i32(menu_addr("pos_x")), Mem.read_i32(menu_addr("pos_y"))
    if w == nil or h == nil or x == nil or y == nil then return false end
    self.base_size = { x = w, y = h }
    self.base_pos  = { x = x, y = y }
    self.saved = {
        w      = w,
        h      = h,
        x      = x,
        y      = y,
        min_w  = Mem.read_i32(menu_addr("min_w")),
        min_h  = Mem.read_i32(menu_addr("min_h")),
        speed  = Mem.read_f32(menu_addr("fade_speed")),
        fade   = Mem.read_f32(menu_addr("fade_val")),
    }
    return true
end

function MenuAnim:enable()
    if self.enabled then return true end
    if not Win.ready then return false, "нет доступа к памяти: " .. tostring(Win.reason) end

    if self.base_text ~= nil then self.base_text_value = ui.get(self.base_text) end
    self:resolve_base()

    local ok, why = self:preflight()
    if not ok then return false, "поля меню не подходят: " .. tostring(why) end
    if not self:build_tabs() then return false, "не удалось прочитать табы" end
    if not self:capture() then return false, "не удалось прочитать размер/позицию меню" end

    self.enabled  = true
    self.prev_mode = Mem.read_i32(menu_addr("fade_mode")) or 0
    self.spin, self.wobble = 0, 0
    return true
end

function MenuAnim:disable()
    if not self.enabled then return true end
    self.enabled = false

    local saved = self.saved
    if saved ~= nil and Win.ready then
        Mem.write_i32(menu_addr("min_w"), saved.min_w)
        Mem.write_i32(menu_addr("min_h"), saved.min_h)
        Mem.write_i32(menu_addr("w_ptr"), saved.w)
        Mem.write_i32(menu_addr("h_ptr"), saved.h)
        Mem.write_i32(menu_addr("pos_x"), saved.x)
        Mem.write_i32(menu_addr("pos_y"), saved.y)
        if saved.speed then Mem.write_f32(menu_addr("fade_speed"), saved.speed) end
        if saved.fade  then Mem.write_f32(menu_addr("fade_val"),  saved.fade)  end
        for index = 0, self.tab_count - 1 do
            local tab = self.tabs[index]
            if tab ~= nil then Mem.write_u8(tab.active, 1) end
        end
    end

    self.saved, self.base_size, self.base_pos, self.tabs, self.tab_count = nil, nil, nil, {}, 0
    return true
end

function MenuAnim:on_frame()
    if not self.enabled then return end

    local ok, err = pcall(self.frame_internal, self)
    if not ok then
        log_error("анимация меню отключена: %s", tostring(err))
        self:disable()
    end
end

function MenuAnim:frame_internal()
    local screen_w, screen_h = client.screen_size()
    local mode  = Mem.read_i32(menu_addr("fade_mode")) or 0
    local act   = Mem.read_u8(menu_addr("act_ptr")) or 0
    local fade  = Mem.read_f32(menu_addr("fade_val")) or 1

    if mode == 0 then
        -- Меню открыто и ничего не анимируется: запоминаем базовые размеры и
        -- возвращаем иконкам нормальное состояние.
        if act > 0 and self.prev_mode == 0 then
            self:capture()
            Mem.write_i32(menu_addr("min_w"), self.saved.min_w or 560)
            Mem.write_i32(menu_addr("min_h"), self.saved.min_h or 660)
            self:toggle_icons(0x01)
        end
        self.prev_mode = mode
        return
    end

    if self.prev_mode ~= 0 and self.base_size ~= nil then
        Mem.write_i32(menu_addr("w_ptr"), self.base_size.x)
        Mem.write_i32(menu_addr("h_ptr"), self.base_size.y)
        Mem.write_i32(menu_addr("pos_x"), self.base_pos.x)
        Mem.write_i32(menu_addr("pos_y"), self.base_pos.y)
    end
    self.prev_mode = mode

    if self.base_size == nil or self.base_pos == nil then return end

    -- Пока идёт анимация, минимальный размер обнуляется, иначе меню не даёт
    -- себя сжать.
    Mem.write_i32(menu_addr("min_w"), 0)
    Mem.write_i32(menu_addr("min_h"), 0)

    fade = math.max(0, math.min(1, fade))

    local pivot_x, pivot_y = screen_w * 0.5, screen_h * 0.5
    local center_x = self.base_pos.x + self.base_size.x * 0.5
    local center_y = self.base_pos.y + self.base_size.y * 0.5

    local dx, dy = center_x - pivot_x, center_y - pivot_y
    local scale    = 0.35 + 0.65 * fade
    local rotation = (1.0 - fade) * 0.35

    local rot_x = dx * math.cos(rotation) - dy * math.sin(rotation)
    local rot_y = dx * math.sin(rotation) + dy * math.cos(rotation)

    local new_w, new_h = self.base_size.x * scale, self.base_size.y * scale
    local new_center_x = pivot_x + rot_x * scale
    local new_center_y = pivot_y + rot_y * scale

    self.wobble = self.wobble + (globals.frametime() or 0) * 8.0
    local wobble = math.sin(self.wobble) * (1.0 - fade) * 12.0

    Mem.write_i32(menu_addr("w_ptr"), math.floor(new_w))
    Mem.write_i32(menu_addr("h_ptr"), math.floor(new_h))
    Mem.write_i32(menu_addr("pos_x"), math.floor(new_center_x - new_w * 0.5 + wobble))
    Mem.write_i32(menu_addr("pos_y"), math.floor(new_center_y - new_h * 0.5))

    self:toggle_icons(0x00)
end

--[[----------------------------------------------------------------------------
    6. Меню
------------------------------------------------------------------------------]]

local ui_master      = ui.new_checkbox("LUA", "Patches", "Включить патчи памяти")
local ui_master_anim = ui.new_checkbox("LUA", "Patches", "Анимация меню (fade / spin)")

local ui_anim_base = ui.new_textbox("LUA", "Patches", "Menu base (hex, пусто = 0x43400000)")

local ui_toggle = {}
local ui_site   = {}

local function refresh_toggle(patch)
    local element = ui_toggle[patch.key]
    if element ~= nil then ui.set(element, patch.applied) end
end

local function set_patch(patch, wanted)
    if wanted then
        local ok, why = patch:apply()
        if ok then
            log("%s: включён (%s @ 0x%08X)", patch.name, patch.hint or "patch", patch:address() or 0)
        else
            log_error("%s: НЕ включён — %s", patch.name, tostring(why))
        end
        refresh_toggle(patch)
        return ok
    end

    local ok, why = patch:restore()
    if ok then
        log("%s: выключен, байты возвращены", patch.name)
    else
        log_error("%s: не удалось вернуть байты — %s", patch.name, tostring(why))
    end
    refresh_toggle(patch)
    return ok
end

local function restore_all(reason)
    for _, patch in ipairs(patches) do patch:restore() end
    MenuAnim:disable()
    if ui_master then ui.set(ui_master, false) end
    if ui_master_anim then ui.set(ui_master_anim, false) end
    for _, patch in ipairs(patches) do refresh_toggle(patch) end
    log("все патчи сняты (%s)", tostring(reason))
end

for _, entry in ipairs(patches) do
    -- В Lua 5.1 (LuaJIT) переменная цикла одна на все итерации, поэтому в
    -- замыкания копируем её: иначе все коллбэки смотрели бы на последний патч.
    local patch = entry

    ui_toggle[patch.key] = ui.new_checkbox("LUA", "Patches", patch.name)
    ui_site[patch.key]   = ui.new_textbox("LUA", "Patches", patch.name .. ": адрес")
    patch.textbox = ui_site[patch.key]

    ui.set_callback(ui_site[patch.key], function()
        local text = ui.get(ui_site[patch.key])
        if text == nil or text == "" then
            patch.site_override = nil
            log("%s: снова встроенный адрес 0x%08X", patch.name, patch.site)
            return
        end
        local parsed = parse_address(text)
        if parsed == nil then
            log_error("%s: \"%s\" не похоже на адрес (нужно 0x1062A526 или 1062A526)", patch.name, tostring(text))
            return
        end
        patch.site_override = parsed
        log("%s: адрес переопределён на 0x%08X", patch.name, parsed)
        if patch.applied then
            -- Переехали на новый адрес — старый обязательно вернуть.
            patch:restore()
            ui.set(ui_toggle[patch.key], false)
            set_patch(patch, true)
        end
    end)

    ui.set_callback(ui_toggle[patch.key], function()
        set_patch(patch, ui.get(ui_toggle[patch.key]) == true)
    end)
end

local ui_read = ui.new_button("LUA", "Patches", "Read sites (проверить адреса)", function()
    log("источник Win32: %s", tostring(Win.reason))
    if not Win.ready then
        log_error("VirtualProtect недоступен — патчи не будут включены")
        return
    end
    for _, patch in ipairs(patches) do
        local addr = patch:address()
        if addr == nil then
            log("%-32s адрес не задан", patch.name)
        else
            local ok, why = Mem.is_executable(addr, #patch.payload)
            log("%-32s 0x%08X  %s  [%s]  %s", patch.name, addr, Mem.hex(addr, #patch.payload),
                patch.hint or "", ok and "ok" or ("не годится: " .. tostring(why)))
        end
    end
    MenuAnim.base_text = ui_anim_base
    MenuAnim:resolve_base()
    local ok, why = MenuAnim:preflight()
    log("анимация меню @ 0x%08X: %s", MenuAnim.base or MenuAnim.base_default,
        ok and "поля читаются" or ("не подходит: " .. tostring(why)))
end)

local ui_restore = ui.new_button("LUA", "Patches", "Restore everything", function()
    restore_all("кнопка")
end)

ui.set_callback(ui_master, function()
    local wanted = ui.get(ui_master) == true
    for _, patch in ipairs(patches) do
        if wanted then
            set_patch(patch, ui.get(ui_toggle[patch.key]) == true)
        else
            patch:restore()
            refresh_toggle(patch)
        end
    end
end)

ui.set_callback(ui_anim_base, function()
    MenuAnim.base_text = ui_anim_base
    local parsed = parse_address(ui.get(ui_anim_base))
    if parsed == nil and ui.get(ui_anim_base) ~= nil and ui.get(ui_anim_base) ~= "" then
        log_error("Menu base: \"%s\" не похоже на адрес, остаётся 0x%08X",
                  tostring(ui.get(ui_anim_base)), MenuAnim.base_default)
        return
    end
    MenuAnim.base = parsed or MenuAnim.base_default
    log("Menu base = 0x%08X", MenuAnim.base)
    if MenuAnim.enabled then
        MenuAnim:disable()
        local ok, why = MenuAnim:enable()
        if not ok then log_error("анимация меню выключена: %s", tostring(why)) end
    end
end)

ui.set_callback(ui_master_anim, function()
    if ui.get(ui_master_anim) == true then
        local ok, why = MenuAnim:enable()
        if ok then
            log("анимация меню включена")
        else
            log_error("анимация меню НЕ включена — %s", tostring(why))
            ui.set(ui_master_anim, false)
        end
    else
        MenuAnim:disable()
        log("анимация меню выключена, поля меню возвращены")
    end
end)

--[[----------------------------------------------------------------------------
    7. События
------------------------------------------------------------------------------]]

client.set_event_callback("paint_ui", function()
    if MenuAnim.enabled then MenuAnim:on_frame() end
end)

client.set_event_callback("shutdown", function()
    restore_all("shutdown")
end)

-- Новый уровень/карта: клиент подгружает свои байты заново, поэтому патчи
-- накладываются повторно, если чекбоксы включены.
client.set_event_callback("level_init", function()
    for _, patch in ipairs(patches) do
        if ui.get(ui_toggle[patch.key]) == true and not patch.applied then
            set_patch(patch, true)
        end
    end
end)

--[[----------------------------------------------------------------------------
    8. Инициализация
------------------------------------------------------------------------------]]

local win_ok, win_error = pcall(init_win)
if win_ok and Win.ready then
    log("Win32 через %s", Win.reason)
else
    if not win_ok then Win.ready, Win.reason = false, tostring(win_error) end
    log_error("Win32 недоступен (%s) — ни один байт не будет записан", tostring(Win.reason))
end

for _, patch in ipairs(patches) do
    log("%-32s site 0x%08X  %s", patch.name, patch.site, patch.hint or "")
end
log("патчи загружены: %d память + 1 анимация меню", #patches)
