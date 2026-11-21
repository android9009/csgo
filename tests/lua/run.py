#!/usr/bin/env python3
"""Прогон lua-патчей против фейкового чита (mock/harness.lua).

Нужен lupa (Lua внутри python):  pip install lupa
Запуск из корня репозитория:      python3 tests/lua/run.py
"""
import os
import sys

try:
    import lupa
except ImportError:  # pragma: no cover
    sys.exit("нужен lupa:  pip install --break-system-packages lupa")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TESTS = os.path.join(ROOT, "tests", "lua")

os.environ.setdefault("LUA_TESTS_DIR", TESTS)
os.environ.setdefault("LUA_REPO_ROOT", ROOT)

runtime = lupa.LuaRuntime(unpack_returned_tuples=True)

# у LuaJIT/чита есть table.unpack, в 5.1 его зовут unpack — harness использует
# table.unpack, а патчи не используют ни то, ни другое; дадим оба.
runtime.execute("if table.unpack == nil then table.unpack = unpack end")

# пусть os.time/os.getenv работают: lupa отдаёт стандартный os
try:
    chunk = runtime.eval('assert(loadfile(os.getenv("LUA_TESTS_DIR") .. "/run_all.lua"))')
except lupa.LuaError as error:  # pragma: no cover
    sys.exit(f"не удалось загрузить run_all.lua: {error}")

try:
    failed = chunk()
except lupa.LuaError as error:
    print(str(error))
    sys.exit(1)

sys.exit(0 if int(failed or 0) == 0 else 1)
