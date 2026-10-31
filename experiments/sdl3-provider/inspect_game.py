"""Read-only GTA2 process inspection; no injection, execution, or memory writes."""
import ctypes as c
from ctypes import wintypes as w
import json
import struct

kernel = c.WinDLL("kernel32", use_last_error=True)
kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
kernel.ReadProcessMemory.restype = w.BOOL
kernel.CloseHandle.argtypes = [w.HANDLE]
INVALID_HANDLE = c.c_void_p(-1).value


class Process(c.Structure):
    _fields_ = [("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("pid", w.DWORD),
                ("heap", c.c_size_t), ("module", w.DWORD), ("threads", w.DWORD),
                ("parent", w.DWORD), ("priority", w.LONG), ("flags", w.DWORD),
                ("name", w.WCHAR * 260)]


class Module(c.Structure):
    _fields_ = [("dwSize", w.DWORD), ("moduleId", w.DWORD), ("pid", w.DWORD),
                ("globalUsage", w.DWORD), ("processUsage", w.DWORD),
                ("base", c.c_void_p), ("size", w.DWORD), ("handle", w.HMODULE),
                ("name", w.WCHAR * 256), ("path", w.WCHAR * 260)]


def entries(flags, pid, entry_type, prefix):
    first = getattr(kernel, prefix + "FirstW")
    following = getattr(kernel, prefix + "NextW")
    for fn in (first, following):
        fn.argtypes = [w.HANDLE, c.POINTER(entry_type)]
        fn.restype = w.BOOL
    snapshot = kernel.CreateToolhelp32Snapshot(flags, pid)
    if snapshot == INVALID_HANDLE:
        raise c.WinError(c.get_last_error())
    try:
        item = entry_type()
        item.dwSize = c.sizeof(item)
        success = first(snapshot, c.byref(item))
        while success:
            yield entry_type.from_buffer_copy(item)
            success = following(snapshot, c.byref(item))
    finally:
        kernel.CloseHandle(snapshot)


def inspect(pid):
    modules = list(entries(0x8 | 0x10, pid, Module, "Module32"))
    handle = kernel.OpenProcess(0x10 | 0x1000, False, pid)
    if not handle:
        raise c.WinError(c.get_last_error())
    try:
        def read(address, size):
            data = c.create_string_buffer(size)
            count = c.c_size_t()
            if not kernel.ReadProcessMemory(handle, address, data, size, c.byref(count)):
                raise c.WinError(c.get_last_error())
            return data.raw

        def integer(address, size=4):
            return int.from_bytes(read(address, size), "little")

        def owner(address):
            return next((m.name for m in modules if m.base <= address < m.base + m.size), "unknown")

        hooks = {}
        for address in (0x45C3E9, 0x45C3F2, 0x4A7683, 0x45C20D, 0x4D142E, 0x4D1778):
            code = read(address, 5)
            target = address + 5 + struct.unpack("<i", code[1:])[0] if code[0] in (0xE8, 0xE9) else None
            hooks[hex(address)] = {"bytes": code.hex(), "target": hex(target) if target else None,
                                   "module": owner(target) if target else None}
            if target:
                wrapper = read(target, 32)
                chained = []
                for offset in range(len(wrapper) - 6):
                    if wrapper[offset:offset + 2] == b"\xff\x15":
                        pointer = struct.unpack_from("<I", wrapper, offset + 2)[0]
                        destination = integer(pointer)
                        chained.append({"pointer": hex(pointer), "target": hex(destination),
                                        "module": owner(destination)})
                if chained:
                    hooks[hex(address)]["indirectCalls"] = chained
        game = integer(0x5EB4FC)
        players = []
        if game:
            for index in range(min(integer(game + 0x23, 1), 6)):
                player = integer(game + 4 + index * 4)
                if not player:
                    continue
                ped = integer(player + 0x2C4)
                values = {"slot": index, "player": hex(player), "ped": hex(ped),
                          "index": integer(player + 0x2E, 1), "attack": integer(player + 0x7C, 1)}
                if ped:
                    weapon = integer(ped + 0x170)
                    values.update(health=integer(ped + 0x216, 2), pedId=integer(ped + 0x200),
                                  weapon=hex(weapon), car=hex(integer(ped + 0x16C)))
                    if weapon:
                        values.update(ammo=integer(weapon, 2), weaponTimer=integer(weapon + 2, 1))
                players.append(values)
        return {"pid": pid, "modules": [{"name": m.name, "base": hex(m.base), "path": m.path}
                for m in modules if m.name.lower().endswith(".asi") or "ginput" in m.name.lower() or "sdl3" in m.name.lower()],
                "hooks": hooks, "game": hex(game), "gameStatus": integer(game) if game else None,
                "replayState": integer(0x5ECAC8 + 0x38), "players": players}
    finally:
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    games = [p for p in entries(2, 0, Process, "Process32") if p.name.lower() == "gta2.exe"]
    print(json.dumps([inspect(p.pid) for p in games], indent=2))
