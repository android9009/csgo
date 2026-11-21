--[[============================================================================
    mock/harness.lua — фейковый мир для проверки lua-патчей без чита

    Подменяет ffi (cast/new/cdef/sizeof/load поверх симулированной памяти),
    bit, client (настоящий поиск по байтам, 0xCC = wildcard), ui, globals,
    vector, а для NL ещё cvar / events / utils.

    Состояние (память, регионы, "нативные" функции) живёт в MOCK, поэтому тест
    может и подготовить память, и прочитать её после действий скрипта.
============================================================================]]

local M = {}

local MEM_COMMIT, MEM_FREE, MEM_IMAGE, MEM_PRIVATE = 0x1000, 0x10000, 0x1000000, 0x20000
local PAGE_EXECUTE_READ, PAGE_READWRITE, PAGE_EXECUTE_READWRITE = 0x20, 0x04, 0x40
local PAGE_NOACCESS = 0x01

M.const = {
    MEM_COMMIT = MEM_COMMIT, MEM_FREE = MEM_FREE, MEM_IMAGE = MEM_IMAGE,
    MEM_PRIVATE = MEM_PRIVATE, PAGE_EXECUTE_READ = PAGE_EXECUTE_READ,
    PAGE_READWRITE = PAGE_READWRITE, PAGE_EXECUTE_READWRITE = PAGE_EXECUTE_READWRITE,
    PAGE_NOACCESS = PAGE_NOACCESS,
}

function M.reset()
    M.mem, M.regions, M.thunks = {}, {}, {}
    M.protect_calls, M.log_lines, M.error_lines = {}, {}, {}
    M.items, M.events, M.groups, M.event_objects = {}, {}, {}, {}
    M.state = {}
end

--[[--------------------------- память ---------------------------------------]]

function M.add_region(base, size, protect, kind, scan)
    M.regions[#M.regions + 1] = {
        base = base, size = size, protect = protect,
        state = MEM_COMMIT, kind = kind or MEM_IMAGE, scan = scan and true or false,
    }
    table.sort(M.regions, function(a, b) return a.base < b.base end)
end

function M.region_of(addr)
    for _, region in ipairs(M.regions) do
        if addr >= region.base and addr < region.base + region.size then return region end
    end
    return nil
end

function M.write_bytes(addr, bytes)
    for index = 1, #bytes do M.mem[addr + index - 1] = math.floor(bytes[index]) % 256 end
end

function M.fill(addr, count, byte)
    for index = 0, count - 1 do M.mem[addr + index] = byte % 256 end
end

function M.read_byte(addr) return M.mem[addr] or 0 end

function M.read_hex(addr, count)
    local out = {}
    for index = 0, count - 1 do out[#out + 1] = string.format("%02X", M.read_byte(addr + index)) end
    return table.concat(out, " ")
end

function M.write_u32(addr, value)
    value = math.floor(value) % 0x100000000
    M.write_bytes(addr, {
        value % 0x100,
        math.floor(value / 0x100) % 0x100,
        math.floor(value / 0x10000) % 0x100,
        math.floor(value / 0x1000000) % 0x100,
    })
end

function M.read_u32(addr)
    return M.read_byte(addr) + M.read_byte(addr + 1) * 0x100
         + M.read_byte(addr + 2) * 0x10000 + M.read_byte(addr + 3) * 0x1000000
end

function M.write_i32(addr, value)
    if value < 0 then value = value + 0x100000000 end
    M.write_u32(addr, value)
end

function M.read_i32(addr)
    local value = M.read_u32(addr)
    if value >= 0x80000000 then return value - 0x100000000 end
    return value
end

function M.write_f32(addr, value)
    local bits = string.unpack("<I4", string.pack("<f", value))
    M.write_u32(addr, bits)
end

function M.read_f32(addr)
    local bits = M.read_u32(addr)
    return string.unpack("<f", string.pack("<I4", bits))
end

-- Сигнатура: строка байт, 0xCC — wildcard (как в gamesense).
function M.find_pattern(pattern)
    local wanted = {}
    for index = 1, #pattern do wanted[index] = pattern:byte(index) end

    for _, region in ipairs(M.regions) do
        if region.scan then
            for start = region.base, region.base + region.size - #wanted do
                local match = true
                for offset = 1, #wanted do
                    local byte = wanted[offset]
                    if byte ~= 0xCC and M.read_byte(start + offset - 1) ~= byte then
                        match = false
                        break
                    end
                end
                if match then return start end
            end
        end
    end
    return nil
end

--[[--------------------------- kernel32 -------------------------------------]]

local function normalize(value)
    if type(value) == "table" and value.addr ~= nil then return value.addr end
    return value
end

function M.install_kernel32()
    local kernel32 = {}

    kernel32.VirtualProtect = function(addr, size, new_protect, old_out)
        addr = normalize(addr)
        local region = M.region_of(addr)
        local old = region and region.protect or PAGE_READWRITE
        M.protect_calls[#M.protect_calls + 1] = { addr = addr, size = size, new = new_protect, old = old }
        if region then region.protect = new_protect end
        if old_out ~= nil then old_out[0] = old end
        return 1
    end

    kernel32.VirtualQuery = function(addr, mbi, size)
        addr = normalize(addr)
        if addr >= 0x7FFF0000 then return 0 end
        local region = M.region_of(addr)
        if region == nil then
            local next_base = 0x7FFF0000
            for _, other in ipairs(M.regions) do
                if other.base > addr and other.base < next_base then next_base = other.base end
            end
            region = { base = addr, size = math.min(next_base - addr, 0x100000),
                       protect = PAGE_NOACCESS, state = MEM_FREE, kind = 0 }
        end
        mbi.BaseAddress = region.base
        mbi.AllocationBase = region.base
        mbi.AllocationProtect = region.protect
        mbi.RegionSize = region.size
        mbi.State = region.state or MEM_COMMIT
        mbi.Protect = region.protect
        mbi.Type = region.kind
        return size
    end

    kernel32.GetModuleHandleA = function() return nil end
    kernel32.GetProcAddress = function(_, name) return M.thunks[name] or 0 end

    M.kernel32 = kernel32
    return kernel32
end

--[[--------------------------- ffi ------------------------------------------]]

local Ptr = {}
Ptr.__index = function(pointer, key)
    if type(key) == "number" then
        local ctype = pointer.ctype
        if ctype:find("^uint8_t%*") or ctype:find("^char%*") then
            return M.read_byte(pointer.addr + key)
        elseif ctype:find("^int%*") then
            return M.read_i32(pointer.addr + key * 4)
        elseif ctype:find("^float%*") then
            return M.read_f32(pointer.addr + key * 4)
        elseif ctype:find("void%*%*") or ctype:find("void%*") then
            -- чтение указателя даёт указатель же (как cdata в LuaJIT)
            local inner = ctype:gsub("%*$", "")
            return M.ptr(inner, M.read_u32(pointer.addr + key * 4))
        end
        return M.read_u32(pointer.addr + key * 4)
    elseif key == "addr" then
        return rawget(pointer, "addr")
    end
    return nil
end

Ptr.__newindex = function(pointer, key, value)
    if type(key) ~= "number" then return end
    local ctype = pointer.ctype
    if ctype:find("^uint8_t%*") or ctype:find("^char%*") then
        M.mem[pointer.addr + key] = math.floor(value) % 256
    elseif ctype:find("^float%*") then
        M.write_f32(pointer.addr + key * 4, value)
    else
        M.write_u32(pointer.addr + key * 4, value)
    end
end

Ptr.__add = function(pointer, value) return M.ptr(pointer.ctype, pointer.addr + value) end
Ptr.__sub = function(pointer, value) return M.ptr(pointer.ctype, pointer.addr - value) end
Ptr.__tostring = function(pointer)
    return string.format("ptr<%s 0x%08X>", pointer.ctype, pointer.addr)
end

function M.ptr(ctype, addr)
    return setmetatable({ ctype = ctype, addr = addr }, Ptr)
end

local STRUCT_FIELDS = {
    SKEET_MBI   = { "BaseAddress", "AllocationBase", "AllocationProtect", "RegionSize", "State", "Protect", "Type" },
    SKEET_MBI32 = { "BaseAddress", "AllocationBase", "AllocationProtect", "RegionSize", "State", "Type" },
    PAW_MBI     = { "BaseAddress", "AllocationBase", "AllocationProtect", "RegionSize", "State", "Protect", "Type" },
}

local STRUCT_SIZE = { SKEET_MBI = 28, SKEET_MBI32 = 24, PAW_MBI = 28 }

local Callable = {}
Callable.__index = Callable
Callable.__call = function(self, ...)
    local thunk = M.thunks[self.addr]
    if thunk == nil then
        error(string.format("mock: вызов незарегистрированного адреса 0x%08X", self.addr))
    end
    local args = { ... }
    for index = 1, #args do args[index] = normalize(args[index]) end
    return thunk(table.unpack(args))
end

function M.make_ffi(options)
    options = options or {}
    local ffi = {}

    ffi.cdef = function() return true end

    ffi.cast = function(ctype, value)
        if ctype:find("%*%)") then
            return setmetatable({ addr = normalize(value) }, Callable)
        end
        if ctype == "uintptr_t" or ctype == "size_t" then
            return normalize(value)
        end
        return M.ptr(ctype, normalize(value))
    end

    ffi.new = function(ctype)
        local base = ctype:gsub("%[.*$", "")
        if STRUCT_FIELDS[base] then
            local struct = {}
            for _, field in ipairs(STRUCT_FIELDS[base]) do struct[field] = 0 end
            return struct
        end
        return { [0] = 0 }
    end

    ffi.sizeof = function(ctype)
        local base = ctype:gsub("%[.*$", "")
        if STRUCT_SIZE[base] then return STRUCT_SIZE[base] end
        return 4
    end

    ffi.copy = function(destination, source, count)
        local source_addr = normalize(source)
        for index = 0, (count or 1) - 1 do
            M.mem[destination.addr + index] = M.read_byte(source_addr + index)
        end
    end

    ffi.string = function(pointer, count)
        local out = {}
        for index = 0, (count or 64) - 1 do
            local byte = M.read_byte(pointer.addr + index)
            if byte == 0 then break end
            out[#out + 1] = string.char(byte)
        end
        return table.concat(out)
    end

    ffi.load = function(name)
        if options.block_loader then error("mock: ffi.load запрещён на этой сборке") end
        if name == "kernel32" then return M.install_kernel32() end
        error("mock: неизвестная библиотека " .. tostring(name))
    end

    ffi.C = M.install_kernel32()

    return ffi
end

--[[--------------------------- bit ------------------------------------------]]

function M.make_bit()
    local mask = 0xFFFFFFFF
    local function fold(a, b, predicate)
        local result, bitv = 0, 1
        for _ = 1, 32 do
            if predicate(a % 2, b % 2) then result = result + bitv end
            a, b, bitv = math.floor(a / 2), math.floor(b / 2), bitv * 2
        end
        return result % (mask + 1)
    end
    return {
        band = function(a, b) return fold(a, b, function(x, y) return x == 1 and y == 1 end) end,
        bor  = function(a, b) return fold(a, b, function(x, y) return x == 1 or y == 1 end) end,
        bxor = function(a, b) return fold(a, b, function(x, y) return x ~= y end) end,
        lshift = function(a, n) return math.floor(a * 2 ^ n) % (mask + 1) end,
        rshift = function(a, n) return math.floor(a / 2 ^ n) % (mask + 1) end,
        tohex  = function(a) return string.format("%08x", math.floor(a) % (mask + 1)) end,
    }
end

--[[--------------------------- UI / окружение -------------------------------]]

local Item = {}
Item.__index = Item

local function new_item(kind, tab, container, name, value)
    local item = setmetatable({
        kind = kind, tab = tab, container = container, name = name,
        value = value, callbacks = {},
    }, Item)
    M.items[#M.items + 1] = item
    return item
end

function Item:get() return self.value end
function Item:set(value) self.value = value end          -- как в читах: без вызова коллбэка
function Item:set_callback(callback, force)
    self.callbacks[#self.callbacks + 1] = callback
    if force then M.fire(self) end
end
function Item:tooltip() return self end

-- Пользователь потрогал элемент: значение меняется и коллбэк вызывается.
function M.fire(item, value)
    if value ~= nil then item.value = value end
    for _, callback in ipairs(item.callbacks) do callback(item) end
end

function M.find_item(name)
    for _, item in ipairs(M.items) do
        if item.name == name then return item end
    end
    return nil
end

local Vector = {}
Vector.__index = Vector
Vector.__add = function(a, b) return setmetatable({ x = a.x + b.x, y = a.y + b.y }, Vector) end
Vector.__sub = function(a, b) return setmetatable({ x = a.x - b.x, y = a.y - b.y }, Vector) end
Vector.__tostring = function(v) return string.format("(%.1f, %.1f)", v.x, v.y) end

local function vector(...)
    local args = { ... }
    if type(args[1]) == "table" and args[1].x ~= nil then
        return setmetatable({ x = args[1].x, y = args[1].y }, Vector)
    end
    return setmetatable({ x = args[1] or 0, y = args[2] or 0 }, Vector)
end

function M.install_globals(kind, options)
    options = options or {}
    local ffi = M.make_ffi(options)
    local bit_lib = M.make_bit()

    local ui = {}
    local events = {}
    local cvar = {}

    if kind == "skeet" then
        ui.new_checkbox = function(tab, container, name) return new_item("checkbox", tab, container, name, false) end
        ui.new_textbox  = function(tab, container, name) return new_item("textbox", tab, container, name, "") end
        ui.new_slider   = function(tab, container, name, min, max, default)
            return new_item("slider", tab, container, name, default or min)
        end
        ui.new_button   = function(tab, container, name, callback)
            local item = new_item("button", tab, container, name, false)
            item.callback = callback
            return item
        end
        M.press = function(item) if item.callback then item.callback() end end
    else
        local Group = {}
        Group.__index = Group
        function Group:switch(name, default) return new_item("switch", self.name, nil, name, default or false) end
        function Group:slider(name, min, max, default, scale)
            return new_item("slider", self.name, nil, name, default or min)
        end
        function Group:button(name, callback)
            local item = new_item("button", self.name, nil, name, false)
            item.callback = callback
            return item
        end
        function Group:hotkey(name, default) return new_item("hotkey", self.name, nil, name, false) end
        function Group:label(text) return new_item("label", self.name, nil, text, nil) end

        ui.create = function(tab, name)
            M.groups[#M.groups + 1] = { tab = tab, name = name }
            return setmetatable({ tab = tab, name = name }, Group)
        end

        -- cvar.sv_maxunlag:float([value[, force]])
        local function make_cvar(name, initial)
            local value = initial
            local object = {}
            function object:float(new_value)
                if new_value ~= nil then
                    M.state["cvar:" .. name] = M.state["cvar:" .. name] or value
                    value = new_value
                    return true
                end
                return value
            end
            function object:int(new_value)
                if new_value ~= nil then value = new_value; return true end
                return math.floor(value)
            end
            cvar[name] = object
            M.state["cvar+" .. name] = initial
            return object
        end
        M.make_cvar = make_cvar
        make_cvar("sv_maxunlag", options.sv_maxunlag or 0.2)

        -- events.net_update_end:set(fn) — объект на каждое событие
        setmetatable(events, {
            __index = function(table_ref, event)
                local object = {
                    set = function(_, callback, once)
                        M.event_objects[event] = { callback = callback, once = once }
                        table_ref[event] = object
                    end,
                    unset = function() M.event_objects[event] = nil end,
                    call = function() M.fire_event(event) end,
                }
                return object
            end,
        })
    end

    ui.get = function(item) return item.value end
    ui.set = function(item, value) item.value = value end   -- без коллбэка, как в читах
    ui.set_callback = function(item, callback) item:set_callback(callback) end

    local client = {
        find_signature = function(_, pattern) return M.find_pattern(pattern) end,
        set_event_callback = function(event, callback)
            events[event] = events[event] or {}
            table.insert(events[event], callback)
            M.events[event] = M.events[event] or {}
            table.insert(M.events[event], callback)
        end,
        screen_size = function() return 1920, 1080 end,
        error_log = function(message) M.error_lines[#M.error_lines + 1] = message end,
        log = function(message) M.log_lines[#M.log_lines + 1] = message end,
        exec = function() end,
        userid_to_entindex = function() return 0 end,
    }

    local globals = {
        frametime = function() return 1 / 64 end,
        framecount = function() return M.state.framecount or 0 end,
        tickinterval = options.tickinterval or 0.015625,
        curtime = function() return M.state.curtime or 0 end,
        tickcount = function() return M.state.tickcount or 0 end,
    }

    local utils = {
        net_channel = function() return options.net_channel or { latency = { [0] = 0.03 } } end,
    }

    -- require: только те модули, что есть в тестовом окружении
    local modules = { ffi = ffi, bit = bit_lib, vector = vector }
    local old_require = _G.require
    _G.require = function(name)
        if modules[name] ~= nil then return modules[name] end
        if old_require then return old_require(name) end
        error("mock: модуль " .. tostring(name) .. " не поддерживается")
    end

    _G.ffi = ffi
    _G.bit = bit_lib
    _G.client = client
    _G.ui = ui
    _G.globals = globals
    _G.vector = vector
    _G.cvar = cvar
    _G.events = events
    _G.utils = utils
    _G.print = function(...)
        local parts = {}
        for index = 1, select("#", ...) do parts[#parts + 1] = tostring(select(index, ...)) end
        M.log_lines[#M.log_lines + 1] = table.concat(parts, " ")
    end

    -- наружу: и через возвращаемое значение, и через сам модуль
    M.cvar, M.ui, M.client, M.globals = cvar, ui, client, globals
    M.events = M.events or {}

    return { ffi = ffi, bit = bit_lib, ui = ui, client = client, events = events,
             cvar = cvar, utils = utils, globals = globals, vector = vector }
end

function M.fire_event(name)
    local fired = false
    local object = M.event_objects[name]
    if object ~= nil and object.callback ~= nil then
        object.callback()
        fired = true
    end
    local list = M.events[name]
    if list ~= nil then
        for _, callback in ipairs(list) do
            callback()
            fired = true
        end
    end
    return fired
end

function M.log_contains(fragment)
    for _, line in ipairs(M.log_lines) do
        if line:find(fragment, 1, true) then return true, line end
    end
    for _, line in ipairs(M.error_lines) do
        if line:find(fragment, 1, true) then return true, line end
    end
    return false
end

return M
