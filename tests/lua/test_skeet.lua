--[[============================================================================
    tests/lua/test_skeet.lua — прогон lua-patches/skeet_all_patches.lua
    против фейкового gamesense (mock/harness.lua).

    Проверяется: резолв Win32 (оба пути), включение/выключение каждого патча,
    возврат байтов, отказ на padding-адресе, переопределение адреса из меню,
    анимация меню и полный restore по shutdown.
============================================================================]]

local M = {}

local function new_report()
    return { passed = 0, failures = {} }
end

local function check(report, condition, text)
    if condition then
        report.passed = report.passed + 1
    else
        report.failures[#report.failures + 1] = text
    end
end

local CLIENT_TEXT = 0x10600000
local SCAN        = 0x10001000
local MENU        = 0x43400000

local SITE = {
    normalize = 0x1062A526,
    dirflags  = 0x106E3F9C,
    skala     = 0x106D67A8,
}

local ORIGINAL = {
    normalize = { 0x0F, 0x57, 0xC0, 0xF3, 0x0F, 0x10, 0x05 },
    dirflags  = { 0xC6, 0x00, 0x01 },
    skala     = { 0x32, 0xC0, 0x5F, 0x5E, 0x5D, 0xC2, 0x08, 0x00 },
}

local THUNK = {
    gate      = 0x00A00000,
    gmh       = 0x00B00000,
    gpa       = 0x00B00004,
    vp        = 0x00B00010,
    vq        = 0x00B00020,
    fake_mod  = 0x00C00000,
}

local function build_memory(H)
    H.add_region(CLIENT_TEXT, 0x100000, H.const.PAGE_EXECUTE_READ, H.const.MEM_IMAGE, false)
    H.add_region(SCAN, 0x2000, H.const.PAGE_EXECUTE_READ, H.const.MEM_IMAGE, true)
    H.add_region(MENU, 0x100000, H.const.PAGE_READWRITE, H.const.MEM_IMAGE, false)
    H.add_region(0x43600000, 0x300000, H.const.PAGE_READWRITE, H.const.MEM_IMAGE, false)

    -- gate: push ecx ; ret
    H.write_bytes(SCAN, { 0x51, 0xC3 })

    -- GetModuleHandleA: C6 06 00 | FF 15 <slot> | 50
    H.write_bytes(SCAN + 0x100, { 0xC6, 0x06, 0x00, 0xFF, 0x15 })
    H.write_u32(SCAN + 0x105, SCAN + 0x800)
    H.write_bytes(SCAN + 0x109, { 0x50 })
    H.write_u32(SCAN + 0x800, THUNK.gmh)

    -- GetProcAddress: 50 | FF 15 <slot> | 85 C0 0F 84 ?? ?? ?? ?? | 6A 00
    H.write_bytes(SCAN + 0x200, { 0x50, 0xFF, 0x15 })
    H.write_u32(SCAN + 0x203, SCAN + 0x900)
    H.write_bytes(SCAN + 0x207, { 0x85, 0xC0, 0x0F, 0x84, 0xCC, 0xCC, 0xCC, 0xCC, 0x6A, 0x00 })
    H.write_u32(SCAN + 0x900, THUNK.gpa)

    -- всё, что вызывается через гейт: первый аргумент = адрес функции
    H.natives = {
        [THUNK.gmh] = function(name) return THUNK.fake_mod end,
        [THUNK.gpa] = function(module, name)
            if name == "VirtualProtect" then return THUNK.vp end
            if name == "VirtualQuery" then return THUNK.vq end
            return 0
        end,
        [THUNK.vp] = function(addr, size, protect, old_out)
            return H.kernel32.VirtualProtect(addr, size, protect, old_out)
        end,
        [THUNK.vq] = function(addr, mbi, size) return H.kernel32.VirtualQuery(addr, mbi, size) end,
    }
    H.thunks[SCAN] = function(target, ...)
        local native = H.natives[target]
        if native == nil then error(string.format("mock: нет натива для 0x%08X", target)) end
        return native(...)
    end

    -- оригинальные байты в сайтах
    H.write_bytes(SITE.normalize, ORIGINAL.normalize)
    H.write_bytes(SITE.dirflags, ORIGINAL.dirflags)
    H.write_bytes(SITE.skala, ORIGINAL.skala)

    -- поля меню чита
    H.write_i32(0x434799C8, 560)   -- min_w
    H.write_i32(0x434799CC, 660)   -- min_h
    H.write_i32(0x434799B8, 100)   -- pos_x
    H.write_i32(0x434799BC, 120)   -- pos_y
    H.write_i32(0x434799C0, 800)   -- w
    H.write_i32(0x434799C4, 700)   -- h
    H.mem[0x434799E0] = 1          -- act (меню открыто)
    H.write_i32(0x43479A38, 0)     -- fade_mode
    H.write_f32(0x4346F920, 270.0)
    H.write_f32(0x43479A5C, 1.0)   -- fade_val

    -- rage-таб: указатель на указатель
    local rage_table = 0x43600000
    local rage_instance = 0x43600100
    H.write_i32(0x43479A00, rage_table)
    H.write_i32(rage_table, rage_instance)
    H.write_i32(rage_instance + 0x20, 60)
    H.write_i32(rage_instance + 0x24, 80)
    H.write_i32(rage_instance + 0x28, 120)
    H.write_i32(rage_instance + 0x2C, 40)
    H.mem[rage_instance + 0x15] = 1

    -- 8 табов
    local tab_table = 0x43700000
    H.write_i32(0x43479A04, tab_table)
    for index = 1, 8 do
        local slot = tab_table - (0x20 - 0x4 * (index - 1))
        local instance = 0x43800000 + index * 0x100
        H.write_i32(slot, instance)
        H.write_i32(instance + 0x20, 10 * index)
        H.write_i32(instance + 0x24, 10 * index)
        H.write_i32(instance + 0x28, 100)
        H.write_i32(instance + 0x2C, 30)
        H.mem[instance + 0x15] = 1
    end
end

local function load_script(H, path, options)
    H.install_globals("skeet", options or {})
    build_memory(H)
    local chunk = assert(loadfile(path))
    chunk()
end

-- 1. Путь через kernel32: полный цикл включения/выключения
function M.run_kernel32_path(H, path)
    local report = new_report()
    H.reset()
    if H.natives == nil then H.natives = {} end
    load_script(H, path, { block_loader = false })

    check(report, H.log_contains("Win32 через kernel32"), "Win32 резолвится через kernel32")
    check(report, H.log_contains("site 0x1062A526"), "в логе есть список сайтов")

    -- включаем normalize
    local toggle = H.find_item("Normalize patch (Z_NORM)")
    check(report, toggle ~= nil, "чекбокс Normalize создан")
    H.fire(toggle, true)
    check(report, H.read_hex(SITE.normalize, 7) == "90 90 90 90 90 90 90", "normalize: 7 байт -> NOP")
    check(report, H.log_contains("включён"), "включение залогировано")

    -- выключаем: байты вернулись
    H.fire(toggle, false)
    local restored = true
    for index, byte in ipairs(ORIGINAL.normalize) do
        if H.read_byte(SITE.normalize + index - 1) ~= byte then restored = false end
    end
    check(report, restored, "normalize: байты возвращены байт в байт")

    -- skala: mov eax,1 ; ret 8
    local skala = H.find_item("Shoot like Skala Johnson")
    H.fire(skala, true)
    check(report, H.read_hex(SITE.skala, 8) == "B8 01 00 00 00 C2 08 00", "skala: payload записан")

    -- dirflags
    local dirflags = H.find_item("Direction flags patch")
    H.fire(dirflags, true)
    check(report, H.read_hex(SITE.dirflags, 3) == "90 90 90", "dirflags: 3 байта -> NOP")

    -- shutdown возвращает всё
    H.fire_event("shutdown")
    local all_restored = true
    for site, bytes in pairs(ORIGINAL) do
        local address = SITE[site]
        for index, byte in ipairs(bytes) do
            if H.read_byte(address + index - 1) ~= byte then all_restored = false end
        end
    end
    check(report, all_restored, "shutdown: все сайты возвращены")
    check(report, H.log_contains("все патчи сняты"), "shutdown залогирован")

    -- повторное включение после shutdown идемпотентно
    H.fire(toggle, true)
    check(report, H.read_hex(SITE.normalize, 7) == "90 90 90 90 90 90 90", "повторное включение работает")
    H.fire(toggle, false)

    return report
end

-- 2. Путь через signature-гейт (ffi.load запрещён)
function M.run_gate_path(H, path)
    local report = new_report()
    H.reset()
    if H.natives == nil then H.natives = {} end
    load_script(H, path, { block_loader = true })

    check(report, H.log_contains("signature gate"), "Win32 резолвится через signature-гейт")

    local toggle = H.find_item("Shoot like Skala Johnson")
    H.fire(toggle, true)
    check(report, H.read_hex(SITE.skala, 8) == "B8 01 00 00 00 C2 08 00", "через гейт патч тоже пишется")
    H.fire(toggle, false)

    return report
end

-- 3. Защита от чужих адресов: padding и не-исполняемая страница
function M.run_rejections(H, path)
    local report = new_report()
    H.reset()
    if H.natives == nil then H.natives = {} end
    load_script(H, path, { block_loader = false })

    -- padding (0xCC) на сайте
    H.fill(SITE.dirflags, 3, 0xCC)
    local dirflags = H.find_item("Direction flags patch")
    H.fire(dirflags, true)
    check(report, H.read_hex(SITE.dirflags, 3) == "CC CC CC", "padding-адрес не патчится")
    check(report, dirflags.value == false, "чекбокс вернулся в выключенное состояние")
    check(report, H.log_contains("padding"), "отказ объяснён в логе")

    -- адрес в неисполняемом регионе
    local normalize = H.find_item("Normalize patch (Z_NORM)")
    local address_box = H.find_item("Normalize patch (Z_NORM): адрес")
    H.fire(address_box, "0x434799C8")   -- это поле меню, не код
    H.fire(normalize, true)
    check(report, H.read_hex(0x434799C8, 7) == "30 02 00 00 00 00 00"
                 or H.read_i32(0x434799C8) == 560, "не-исполняемая страница не тронута")
    check(report, normalize.value == false, "патч на не-исполняемый адрес не включился")

    -- некорректная строка адреса
    H.fire(address_box, "not an address")
    check(report, H.log_contains("не похоже на адрес"), "мусор в поле адреса отклонён")

    -- нормальный код после сброса поля
    H.fire(address_box, "")
    H.fire(normalize, true)
    check(report, H.read_hex(SITE.normalize, 7) == "90 90 90 90 90 90 90", "сброс адреса вернул встроенный")
    H.fire(normalize, false)

    return report
end

-- 4. Анимация меню
function M.run_menu_animation(H, path)
    local report = new_report()
    H.reset()
    if H.natives == nil then H.natives = {} end
    load_script(H, path, { block_loader = false })

    local anim = H.find_item("Анимация меню (fade / spin)")
    check(report, anim ~= nil, "чекбокс анимации создан")

    H.fire(anim, true)
    check(report, H.log_contains("анимация меню включена"), "анимация включилась")

    -- кадр во время анимации
    H.write_i32(0x43479A38, 1)     -- mode != 0 -> идёт анимация
    H.write_f32(0x43479A5C, 0.5)   -- fade
    H.fire_event("paint_ui")

    check(report, H.read_i32(0x434799C0) ~= 800, "ширина меню изменилась на кадре")
    check(report, H.read_i32(0x434799C8) == 0, "min width обнулён на время анимации")

    -- выключение возвращает поля
    H.fire(anim, false)
    check(report, H.read_i32(0x434799C0) == 800, "ширина возвращена")
    check(report, H.read_i32(0x434799C4) == 700, "высота возвращена")
    check(report, H.read_i32(0x434799B8) == 100, "позиция X возвращена")
    check(report, H.read_i32(0x434799C8) == 560, "min width возвращён")
    check(report, H.read_byte(0x43600100 + 0x15) == 1, "иконка Rage-таба снова видима")

    -- чужой билд: поля не похожи на меню -> отказ
    H.write_i32(0x434799C0, 999999)
    H.fire(anim, true)
    check(report, anim.value == false, "неправдоподобные поля меню не трогаются")
    check(report, H.log_contains("не подходят") or H.log_contains("вне"), "отказ по preflight залогирован")

    return report
end

-- 5. Патчи panchan: airlag + ragebot improved (абсолютные сайты + эталоны)
local PANCHAN = 0x43320000

local PANCHAN_BYTES = {
    airlag      = { addr = 0x433AC17C, bytes = { 0x74, 0x0A, 0x8B, 0x45, 0x08, 0x85 } },
    delay_a     = { addr = 0x433ED5BD, bytes = { 0x75, 0x07 } },
    delay_b     = { addr = 0x433ED757, bytes = { 0x0F, 0x2F, 0x15, 0x58, 0x27, 0x47, 0x43 } },
    delay_c     = { addr = 0x4335BAA5, bytes = { 0x74, 0x64 } },
    bt_distance = { addr = 0x433AED50, bytes = { 0x0F, 0x2F, 0x15, 0x60, 0x2B, 0x47, 0x43 } },
    bt_gap      = { addr = 0x433AF012, bytes = { 0x83, 0xF9, 0x08 } },
    jump        = { addr = 0x43372D89, bytes = { 0xF7, 0x40, 0x04, 0x00, 0x00, 0x02, 0x00,
                                                 0x0F, 0x84, 0x94, 0x04, 0x00, 0x00 } },
    samples_a   = { addr = 0x4333278D, bytes = { 0x81, 0xFF, 0x80, 0x00, 0x00, 0x00 } },
    samples_b   = { addr = 0x4333285D, bytes = { 0x81, 0xF9, 0x80, 0x00, 0x00, 0x00 } },
    seed_a      = { addr = 0x433326CF, bytes = { 0x0F, 0xB6, 0x88, 0x50, 0xFC, 0x46, 0x43 } },
    seed_b      = { addr = 0x4333273E, bytes = { 0x0F, 0xB6, 0x87, 0x50, 0xFC, 0x46, 0x43 } },
    seed_c      = { addr = 0x433327B3, bytes = { 0x0F, 0xB6, 0x91, 0x50, 0xFC, 0x46, 0x43 } },
    seed_d      = { addr = 0x4333281F, bytes = { 0x0F, 0xB6, 0x80, 0x50, 0xFC, 0x46, 0x43 } },
    percent     = { addr = 0x43332890, bytes = { 0x6A, 0x64 } },
    hitbox_a    = { addr = 0x433337EA, bytes = { 0x6A, 0x13 } },
    hitbox_b    = { addr = 0x43333C88, bytes = { 0x6A, 0x13 } },
    mask        = { addr = 0x43333349, bytes = { 0xBE, 0xCA, 0x00, 0x00, 0x00 } },
    dist_a      = { addr = 0x43333078, bytes = { 0x76, 0x27 } },
    dist_b      = { addr = 0x433330A8, bytes = { 0x76, 0x0D } },
}

local function build_panchan(H)
    H.add_region(PANCHAN, 0x200000, H.const.PAGE_EXECUTE_READ, H.const.MEM_IMAGE, false)
    for _, entry in pairs(PANCHAN_BYTES) do
        H.write_bytes(entry.addr, entry.bytes)
    end
end

local function panchan_restored(H)
    for _, entry in pairs(PANCHAN_BYTES) do
        for index, byte in ipairs(entry.bytes) do
            if H.read_byte(entry.addr + index - 1) ~= byte then return false, entry.addr end
        end
    end
    return true
end

function M.run_panchan(H, path)
    local report = new_report()
    H.reset()
    if H.natives == nil then H.natives = {} end
    load_script(H, path, { block_loader = false })
    build_panchan(H)

    local master = H.find_item("Патчи panchan (airlag + ragebot improved)")
    check(report, master ~= nil, "мастер-чекбокс panchan создан")
    H.fire(master, true)

    -- airlag
    local airlag = H.find_item("panchan: Air lag")
    H.fire(airlag, true)
    check(report, H.read_hex(PANCHAN_BYTES.airlag.addr, 6) == "90 90 90 90 90 90",
          "airlag: 6 байт -> NOP")

    -- delay shot fix
    local delay = H.find_item("panchan: Delay Shot Fix")
    H.fire(delay, true)
    check(report, H.read_hex(PANCHAN_BYTES.delay_a.addr, 2) == "EB 07", "delay: jne -> jmp")
    check(report, H.read_hex(PANCHAN_BYTES.delay_b.addr, 7) == "0F 2F 15 6C EE 46 43",
          "delay: константа переставлена")
    check(report, H.read_hex(PANCHAN_BYTES.delay_c.addr, 2) == "90 90", "delay: rescan -> NOP")

    -- BT fix: своя константа 512^2 + gap
    local bt = H.find_item("panchan: BT Fix")
    H.fire(bt, true)
    local bt_pointer = H.read_u32(PANCHAN_BYTES.bt_distance.addr + 3)
    check(report, bt_pointer ~= 0x43472B60 and bt_pointer >= 0x70000000,
          "BT distance переписан на свой буфер")
    check(report, math.abs(H.read_f32(bt_pointer) - 512 * 512) < 1,
          "в буфере лежит 512^2")
    check(report, H.read_hex(PANCHAN_BYTES.bt_gap.addr, 3) == "83 F9 14", "BT gap 8 -> 20")

    -- jump: только со снайперкой в прицеле
    local jump = H.find_item("panchan: Jump Scout Fix")
    H.fire(jump, true)
    check(report, H.read_byte(PANCHAN_BYTES.jump.addr) == 0xF7,
          "без прицела Jump Scout не патчится")

    H.entity.local_player = 1
    H.entity.props.m_bIsScoped = 1
    H.entity.props.m_fFlags = 257          -- на земле
    H.fire_event("createmove")
    check(report, H.read_hex(PANCHAN_BYTES.jump.addr, 13) == "90 90 90 90 90 90 90 90 90 90 90 90 90",
          "в прицеле Jump Scout -> 13 NOP")

    H.entity.props.m_bIsScoped = 0
    H.fire_event("createmove")
    check(report, H.read_byte(PANCHAN_BYTES.jump.addr) == 0xF7,
          "прицел снят — байты Jump Scout возвращены")

    -- hitscan: значения из слайдеров
    local hitscan = H.find_item("panchan: Hitscan Fix")
    H.fire(hitscan, true)
    check(report, H.read_hex(PANCHAN_BYTES.samples_a.addr, 6) == "81 FF 00 02 00 00",
          "hitscan samples A = 512")
    check(report, H.read_hex(PANCHAN_BYTES.samples_b.addr, 6) == "81 F9 00 02 00 00",
          "hitscan samples B = 512")
    local seed_pointer = H.read_u32(PANCHAN_BYTES.seed_a.addr + 3)
    check(report, seed_pointer >= 0x70000000, "seed указывает на свою таблицу")
    check(report, H.read_byte(seed_pointer + 7) == 7, "таблица seed заполнена (i % 256)")
    check(report, H.read_hex(PANCHAN_BYTES.percent.addr, 2) == "6A 4C", "percent cap = 76")
    check(report, H.read_hex(PANCHAN_BYTES.hitbox_a.addr, 2) == "6A 12", "hitbox cap = 18")
    check(report, H.read_hex(PANCHAN_BYTES.mask.addr, 5) == "BE FF 00 00 00", "mask = 255")
    check(report, H.read_hex(PANCHAN_BYTES.dist_a.addr, 2) == "EB 27", "distance A: jbe -> jmp")
    check(report, H.read_hex(PANCHAN_BYTES.dist_b.addr, 2) == "EB 0D", "distance B: jbe -> jmp")

    -- воздух: samples = 256
    local samples_box = H.find_item("panchan: hitscan samples")
    H.fire(samples_box, 384)
    H.entity.props.m_fFlags = 256          -- в воздухе
    H.fire_event("createmove")
    check(report, H.read_hex(PANCHAN_BYTES.samples_a.addr, 6) == "81 FF 00 01 00 00",
          "в воздухе samples = 256")
    H.entity.props.m_fFlags = 257
    -- после приземления panchan держит 256 ещё 4 тика
    for _ = 1, 5 do H.fire_event("createmove") end
    check(report, H.read_hex(PANCHAN_BYTES.samples_a.addr, 6) == "81 FF 80 01 00 00",
          "на земле снова слайдер (384)")

    -- всё выключено: байты вернулись байт в байт
    H.fire(master, false)
    local restored, where = panchan_restored(H)
    check(report, restored, string.format("выключение вернуло все сайты panchan (сбой @0x%08X)", where or 0))
    check(report, H.log_contains("patches panchan выключены"), "выключение залогировано")

    return report
end

-- 6. panchan: чужой билд — эталон не совпал, группа откатывается целиком
function M.run_panchan_rejections(H, path)
    local report = new_report()
    H.reset()
    if H.natives == nil then H.natives = {} end
    load_script(H, path, { block_loader = false })
    build_panchan(H)

    local master = H.find_item("Патчи panchan (airlag + ragebot improved)")
    H.fire(master, true)

    -- BT fix: операнд не тот, что в panchan -> отказ, ничего не записано
    H.write_bytes(PANCHAN_BYTES.bt_distance.addr, { 0x0F, 0x2F, 0x15, 0x11, 0x22, 0x33, 0x44 })
    local original_disp = H.read_hex(PANCHAN_BYTES.bt_distance.addr, 7)
    local bt = H.find_item("panchan: BT Fix")
    H.fire(bt, true)
    check(report, H.read_hex(PANCHAN_BYTES.bt_distance.addr, 7) == original_disp,
          "BT distance с чужим операндом не тронут")
    check(report, H.read_hex(PANCHAN_BYTES.bt_gap.addr, 3) == "83 F9 08",
          "BT gap откатился вместе с группой")
    check(report, bt.value == false, "группа выключилась после отказа")
    check(report, H.log_contains("операнд"), "причина отказа названа")

    -- delay: у среднего сайта испорчен эталон -> вся группа откатывается
    H.write_bytes(PANCHAN_BYTES.delay_b.addr, { 0x0F, 0x2F, 0x15, 0x00, 0x00, 0x00, 0x00 })
    local delay = H.find_item("panchan: Delay Shot Fix")
    H.fire(delay, true)
    check(report, H.read_hex(PANCHAN_BYTES.delay_a.addr, 2) == "75 07",
          "первый сайт delay откатился после отказа второго")
    check(report, delay.value == false, "delay выключился")

    -- padding вместо кода
    H.fill(PANCHAN_BYTES.airlag.addr, 6, 0xCC)
    local airlag = H.find_item("panchan: Air lag")
    H.fire(airlag, true)
    check(report, H.read_hex(PANCHAN_BYTES.airlag.addr, 6) == "CC CC CC CC CC CC",
          "padding не патчится")
    check(report, airlag.value == false, "airlag выключился")
    check(report, H.log_contains("padding"), "padding отклонён с объяснением")

    return report
end

-- 7. panchan: переопределение базы из меню
function M.run_panchan_base(H, path)
    local report = new_report()
    H.reset()
    if H.natives == nil then H.natives = {} end
    load_script(H, path, { block_loader = false })
    build_panchan(H)

    local master = H.find_item("Патчи panchan (airlag + ragebot improved)")
    local base_box = H.find_item("panchan base (hex, пусто = 0x43320000)")
    check(report, base_box ~= nil, "поле базы panchan создано")

    -- сдвигаем всю картину на 0x1000 и говорим об этом скрипту
    H.add_region(PANCHAN + 0x1000, 0x200000, H.const.PAGE_EXECUTE_READ, H.const.MEM_IMAGE, false)
    for _, entry in pairs(PANCHAN_BYTES) do
        H.write_bytes(entry.addr + 0x1000, entry.bytes)
    end

    H.fire(base_box, "0x43321000")
    H.fire(master, true)
    local airlag = H.find_item("panchan: Air lag")
    H.fire(airlag, true)
    check(report, H.read_hex(PANCHAN_BYTES.airlag.addr + 0x1000, 6) == "90 90 90 90 90 90",
          "база из меню применена")
    H.fire(master, false)

    -- мусор в поле базы не меняет ничего
    H.fire(base_box, "nonsense")
    check(report, H.log_contains("panchan base"), "некорректная база отклонена")

    return report
end

return M
