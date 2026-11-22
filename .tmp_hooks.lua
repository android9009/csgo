--[[----------------------------------------------------------------------------
    9.9 Хуки persistence: cl_move (engine.dll) и write_usercmd (vtable)

    В Luas/nl/persistence.lua это две установки из w_axis:

      * cl_move — в engine.dll по шаблону
        8A CB F2 0F 10 05 ? ? ? ? DD 5D F0 F2 0F 58 45 F0 F2 0F 11 05 ? ? ? ?
        F3 0F 10 45 FC E8 ? ? ? ?
        на смещении +31 стоит `call rel32`. Патчится ТОЛЬКО disp32: сам E8
        остаётся, поэтому вызов уходит в пещеру, а из пещеры — назад в
        найденную цель. Пещера 64 байта:

          83 EC 04              sub  esp, 4
          F3 0F 11 04 24        movss [esp], xmm0
          9C 60                 pushfd / pushad
          B8 <колбэк>           mov  eax, колбэк
          FF D0                 call eax
          85 C0                 test eax, eax
          75 05                 jne  -> ret (колбэк сказал «я всё сделал»)
          E9 <цель>             jmp  на настоящий cl_move
          C3                    ret

      * write_usercmd — хук НЕ по коду, а по vtable: `VClient018` + 24 (индекс
        `wudtb_index`). Пещера 32 байта — тонкая обёртка, которая передаёт
        (this, cmd, verified) в колбэк:

          55                    push ebp
          8B EC                 mov  ebp, esp
          8B 4D 08              mov  ecx, [ebp+8]
          8B 55 0C              mov  edx, [ebp+0C]
          FF 75 10              push [ebp+10]
          B8 <колбэк>           mov  eax, колбэк
          FF D0                 call eax
          83 C4 04              add  esp, 4
          5D C3                 pop ebp / ret

    Байты пещер совпадают с `emit_cl_move_stub` / `emit_write_stub` байт в байт,
    включая rel8 = 5 у cl_move и rel32 на найденную цель.

    ОТЛИЧИЕ, которое нельзя спрятать: в оригинале колбэк — это `bind_function`
    от Lua-функции, то есть нативный код, который умеет только рантайм
    persistence. Отсюда колбэк задаётся АДРЕСОМ в поле меню; пока адрес не
    задан, хук не ставится и пишет причину. Без колбэка пещера вызовет 0 —
    поэтому «включить и забыть» здесь нет.
------------------------------------------------------------------------------]]

local NL_HOOKS = {}

-- Шаблон в текст-форме разбирается так же, как в разделе 9.8 (только '?'
-- вместо 0xCC бывает в исходниках).
local function pattern_bytes(text)
    local out = {}
    for token in text:gmatch("%S+") do
        if token == "?" or token == "??" then
            out[#out + 1] = PAT_WILD
        else
            out[#out + 1] = tonumber(token, 16) or PAT_WILD
        end
    end
    return out
end

local function parse_address(text)
    if type(text) ~= "string" then return nil end
    local trimmed = text:match("^%s*(.-)%s*$") or ""
    if trimmed == "" then return nil end
    local value = tonumber(trimmed)
    if value == nil then
        value = tonumber(trimmed:match("^0[xX](%x+)$") or "", 16)
    end
    if value == nil or value < 0x10000 or value > 0xFFFFFFFF then return nil end
    return math.floor(value)
end

local function module_handle_base(name)
    local handle = C.GetModuleHandleA(name)
    if handle == nil then return nil end
    local base = tonumber(ffi.cast("uintptr_t", handle))
    if base == nil or base == 0 then return nil end
    if vread_u32(base) ~= 0x00905A4D then return nil end
    local pe_offset = vread_u32(base + 0x3C)
    if pe_offset == nil or pe_offset < 64 or pe_offset > 4096 then return nil end
    if vread_u32(base + pe_offset) ~= 0x4550 then return nil end
    return base
end

--[[--------------------------- общая механика пещер -------------------------]]

local function hook_alloc(size)
    local block = C.VirtualAlloc(nil, size, MEM_COMMIT_RESERVE, PAGE_RWX)
    if block == nil then return nil end
    local address = tonumber(ffi.cast("uintptr_t", block))
    if address == nil or address == 0 then
        C.VirtualFree(block, 0, MEM_RELEASE)
        return nil
    end
    return block, address
end

local function hook_release(block)
    if block ~= nil then C.VirtualFree(block, 0, MEM_RELEASE) end
end

--[[--------------------------- cl_move --------------------------------------]]

local CL_MOVE_SIGNATURE = "8A CB F2 0F 10 05 ? ? ? ? DD 5D F0 F2 0F 58 45 F0 " ..
                         "F2 0F 11 05 ? ? ? ? F3 0F 10 45 FC E8 ? ? ? ?"
local CL_MOVE_CALL_OFFSET = 31
local CL_MOVE_STUB_SIZE = 64

local cl_move_hook = {
    applied = false, block = nil, cave = nil, site = nil,
    saved_relative = nil, target = nil, callback = 0,
}

local function build_cl_move_stub(cave, callback, target)
    local code = { 0x83, 0xEC, 0x04, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0x9C, 0x60, 0xB8 }
    le32_into(code, callback)
    local tail = { 0xFF, 0xD0, 0x85, 0xC0, 0x75, 0x05, 0xE9 }
    for _, byte in ipairs(tail) do code[#code + 1] = byte end
    patch_le32(code, 22, target - (cave + 26))
    code[#code + 1] = 0xC3
    for index = #code + 1, CL_MOVE_STUB_SIZE do code[index] = 0xCC end
    return code
end

local function cl_move_locate()
    local base = module_handle_base("engine.dll")
    if base == nil then return nil, "engine.dll не загружен или это не PE-образ" end

    local buffer = module_bytes(base)
    if buffer == nil then return nil, "образ engine.dll не читается" end

    local index, count = find_in_buffer(buffer, pattern_bytes(CL_MOVE_SIGNATURE))
    if index == nil then return nil, "шаблон cl_move не найден" end
    if count > 1 then return nil, "шаблон cl_move неоднозначен" end

    local match = module_address(base, index)
    local site = match + CL_MOVE_CALL_OFFSET
    if not executable_bytes(site, 5) then
        return nil, ("вызов cl_move 0x%08X не исполняемый"):format(site)
    end
    if vread_u8(site) ~= 0xE8 then
        return nil, ("на месте вызова не E8, а 0x%02X"):format(vread_u8(site) or 0)
    end
    local relative = vread_i32(site + 1)
    if relative == nil then return nil, "не читается disp32 вызова" end
    return site, relative, site + 5 + relative
end

local function cl_move_install(callback)
    if cl_move_hook.applied then return true end
    if callback == 0 then return false, "не задан адрес колбэка" end

    local site, relative, target = cl_move_locate()
    if site == nil then return false, relative end

    local block, cave = hook_alloc(CL_MOVE_STUB_SIZE)
    if block == nil then return false, "VirtualAlloc под пещеру не сработал" end

    if not raw_write(cave, build_cl_move_stub(cave, callback, target)) then
        hook_release(block)
        return false, "не удалось записать пещеру"
    end
    vflush(cave, CL_MOVE_STUB_SIZE)

    local old = ffi.new("unsigned int[1]")
    if C.VirtualProtect(ffi.cast("void*", site + 1), 4, PAGE_RWX, old) == 0 then
        hook_release(block)
        return false, "VirtualProtect на вызове не сработал"
    end
    local displacement = {}
    le32_into(displacement, cave - site - 5)
    local written = raw_write(site + 1, displacement)
    C.VirtualProtect(ffi.cast("void*", site + 1), 4, tonumber(old[0]) or PAGE_EXECANY, old)
    if not written then
        hook_release(block)
        return false, "не удалось переписать disp32"
    end
    vflush(site, 5)

    if vread_i32(site + 1) ~= cave - site - 5 then
        local back = {}
        le32_into(back, relative)
        raw_write(site + 1, back)
        vflush(site, 5)
        hook_release(block)
        return false, "обратное чтение disp32 не совпало — вызов возвращён"
    end

    cl_move_hook.applied, cl_move_hook.block, cl_move_hook.cave = true, block, cave
    cl_move_hook.site, cl_move_hook.saved_relative = site, relative
    cl_move_hook.target, cl_move_hook.callback = target, callback
    log("cl_move hook: вызов 0x%08X -> пещера 0x%08X (цель 0x%08X)", site, cave, target)
    return true
end

local function cl_move_restore()
    if not cl_move_hook.applied then return true end
    local ok = true
    if cl_move_hook.site ~= nil and cl_move_hook.saved_relative ~= nil then
        local displacement = {}
        le32_into(displacement, cl_move_hook.saved_relative)
        local old = ffi.new("unsigned int[1]")
        if C.VirtualProtect(ffi.cast("void*", cl_move_hook.site + 1), 4, PAGE_RWX, old) ~= 0 then
            ok = raw_write(cl_move_hook.site + 1, displacement) == true
            C.VirtualProtect(ffi.cast("void*", cl_move_hook.site + 1), 4,
                             tonumber(old[0]) or PAGE_EXECANY, old)
            vflush(cl_move_hook.site, 5)
        else
            ok = false
        end
    end
    hook_release(cl_move_hook.block)
    cl_move_hook.applied, cl_move_hook.block, cl_move_hook.cave = false, nil, nil
    cl_move_hook.site, cl_move_hook.saved_relative, cl_move_hook.target = nil, nil, nil
    log("cl_move hook: снят, disp32 вызова возвращён")
    return ok
end

--[[--------------------------- write_usercmd (vtable) -----------------------]]

local WRITE_INTERFACE = "VClient018"
local WRITE_VTABLE_INDEX = 24
local WRITE_STUB_SIZE = 32

local write_hook = {
    applied = false, block = nil, cave = nil, entry = nil,
    original = nil, callback = 0,
}

local function build_write_stub(callback)
    local code = { 0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08, 0x8B, 0x55, 0x0C, 0xFF, 0x75, 0x10, 0xB8 }
    le32_into(code, callback)
    local tail = { 0xFF, 0xD0, 0x83, 0xC4, 0x04, 0x5D, 0xC3 }
    for _, byte in ipairs(tail) do code[#code + 1] = byte end
    for index = #code + 1, WRITE_STUB_SIZE do code[index] = 0xCC end
    return code
end

local function write_entry_locate()
    local interface = nil
    local ok, value = pcall(function() return utils.create_interface("client.dll", WRITE_INTERFACE) end)
    if ok then interface = value end
    if interface == nil then
        return nil, nil, "интерфейс " .. WRITE_INTERFACE .. " не найден"
    end

    local address = tonumber(ffi.cast("uintptr_t", interface))
    if address == nil or address == 0 then
        return nil, nil, "create_interface вернул 0"
    end
    local table_pointer = vread_u32(address)
    if table_pointer == nil or table_pointer < 0x10000 then
        return nil, nil, "vtable интерфейса не читается"
    end

    local entry = table_pointer + WRITE_VTABLE_INDEX * 4
    local mbi = region_info(entry)
    if mbi == nil or mbi.State ~= MEM_COMMIT
       or bit.band(tonumber(mbi.Protect), 0x101) ~= 0 then
        return nil, nil, "запись vtable недоступна"
    end
    return entry, vread_u32(entry)
end

local function write_hook_install(callback)
    if write_hook.applied then return true end
    if callback == 0 then return false, "не задан адрес колбэка" end

    local entry, original, why = write_entry_locate()
    if entry == nil then return false, why end
    if original == nil then return false, "vtable[24] не читается" end
    if original == 0 then return false, "vtable[24] пуст" end

    local block, cave = hook_alloc(WRITE_STUB_SIZE)
    if block == nil then return false, "VirtualAlloc под пещеру не сработал" end

    if not raw_write(cave, build_write_stub(callback)) then
        hook_release(block)
        return false, "не удалось записать пещеру"
    end
    vflush(cave, WRITE_STUB_SIZE)

    local old = ffi.new("unsigned int[1]")
    if C.VirtualProtect(ffi.cast("void*", entry), 4, PAGE_RWX, old) == 0 then
        hook_release(block)
        return false, "VirtualProtect на vtable не сработал"
    end
    local pointer = {}
    le32_into(pointer, cave)
    local written = raw_write(entry, pointer)
    C.VirtualProtect(ffi.cast("void*", entry), 4, tonumber(old[0]) or PAGE_EXECANY, old)
    if not written then
        hook_release(block)
        return false, "не удалось записать vtable"
    end

    if vread_u32(entry) ~= cave then
        local back = {}
        le32_into(back, original)
        raw_write(entry, back)
        hook_release(block)
        return false, "обратное чтение vtable не совпало"
    end

    write_hook.applied, write_hook.block, write_hook.cave = true, block, cave
    write_hook.entry, write_hook.original, write_hook.callback = entry, original, callback
    log("write_usercmd hook: vtable %s[%d] 0x%08X -> пещера 0x%08X",
        WRITE_INTERFACE, WRITE_VTABLE_INDEX, entry, cave)
    return true
end

local function write_hook_restore()
    if not write_hook.applied then return true end
    local ok = true
    if write_hook.entry ~= nil and write_hook.original ~= nil then
        local back = {}
        le32_into(back, write_hook.original)
        local old = ffi.new("unsigned int[1]")
        if C.VirtualProtect(ffi.cast("void*", write_hook.entry), 4, PAGE_RWX, old) ~= 0 then
            ok = raw_write(write_hook.entry, back) == true
            C.VirtualProtect(ffi.cast("void*", write_hook.entry), 4,
                             tonumber(old[0]) or PAGE_EXECANY, old)
        else
            ok = false
        end
    end
    hook_release(write_hook.block)
    write_hook.applied, write_hook.block, write_hook.cave = false, nil, nil
    write_hook.entry, write_hook.original = nil, nil
    log("write_usercmd hook: снят, vtable[%d] возвращён", WRITE_VTABLE_INDEX)
    return ok
end

local function nl_hooks_restore()
    local ok = true
    if not cl_move_restore() then ok = false end
    if not write_hook_restore() then ok = false end
    return ok
end

--[[--------------------------- меню -----------------------------------------]]

local hooks_group = ui.create("Rage", "NL Hooks (persistence)")

local cl_move_switch = hooks_group:switch("cl_move hook (engine.dll)", false)
cl_move_switch:tooltip("Патчит только disp32 вызова cl_move (+31 в шаблоне): вызов уходит в пещеру " ..
                       "64 байта, из неё — в колбэк и затем в настоящую функцию. Нужен адрес колбэка.")

local cl_move_callback = hooks_group:input("cl_move callback (hex)", "")
cl_move_callback:tooltip("Адрес нативного колбэка, который зовёт пещера. Пусто или 0 — хук не ставится.")

local write_switch = hooks_group:switch("write_usercmd hook (VClient018+24)", false)
write_switch:tooltip("Хук vtable-слота 24 интерфейса VClient018 через тонкую пещеру 32 байта. " ..
                     "Нужен адрес колбэка.")

local write_callback = hooks_group:input("write_usercmd callback (hex)", "")
write_callback:tooltip("Адрес нативного колбэка (this, cmd, verified). Пусто или 0 — хук не ставится.")

NL_HOOKS.cl_move = cl_move_hook
NL_HOOKS.write_usercmd = write_hook

function NL_HOOKS:available(kind)
    local hook = self[kind]
    return hook ~= nil and hook.applied == true
end

function NL_HOOKS:restore_all()
    return nl_hooks_restore()
end

local function hooks_update()
    if ui.get(cl_move_switch) == true then
        local callback = parse_address(cl_move_callback:get())
        local ok, why = cl_move_install(callback or 0)
        if not ok then
            warn("cl_move hook: НЕ включён — %s", tostring(why))
            ui.set(cl_move_switch, false)
        end
    elseif cl_move_hook.applied then
        cl_move_restore()
    end

    if ui.get(write_switch) == true then
        local callback = parse_address(write_callback:get())
        local ok, why = write_hook_install(callback or 0)
        if not ok then
            warn("write_usercmd hook: НЕ включён — %s", tostring(why))
            ui.set(write_switch, false)
        end
    elseif write_hook.applied then
        write_hook_restore()
    end
end

local function hooks_poll()
    if ui.get(cl_move_switch) == true and not cl_move_hook.applied then hooks_update() end
    if ui.get(write_switch) == true and not write_hook.applied then hooks_update() end
end

cl_move_switch:set_callback(function() hooks_update() end, true)
write_switch:set_callback(function() hooks_update() end, true)
cl_move_callback:set_callback(function() hooks_update() end, true)
write_callback:set_callback(function() hooks_update() end, true)
