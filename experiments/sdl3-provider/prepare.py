"""Prepare isolated build inputs from existing sources; never modify either repo."""
from pathlib import Path
import hashlib
import zipfile
import difflib

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent.parent
DEPS = ROOT / "deps"
BUILD = ROOT / "build"
SDL_ARCHIVE = "SDL3-devel-3.4.18-VC.zip"
HASHES = {
    SDL_ARCHIVE: "78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd",
    "misi-source.zip": "ffd40b4ce2dab94eb4db1e02e8114de30a3015a0379aa95aed64f6bd57d85621",
}


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8", newline="\n")


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError(f"Expected exactly one occurrence of {before!r}")
    return text.replace(before, after, 1)


def prepare():
    for name, expected in HASHES.items():
        archive = DEPS / name
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Unexpected dependency hash: {archive}")
        destination = DEPS if name == SDL_ARCHIVE else DEPS / "misi"
        with zipfile.ZipFile(archive) as source:
            for item in source.infolist():
                parts = Path(item.filename).parts
                if item.is_dir() or ".git" in parts:
                    continue
                if name != SDL_ARCHIVE:
                    parts = parts[1:]  # Archive's 0.5.0/ wrapper
                    if not item.filename.lower().endswith((".cpp", ".c", ".h", ".txt")):
                        continue
                target = destination.joinpath(*parts).resolve()
                if destination.resolve() not in target.parents:
                    raise RuntimeError("Dependency archive contains an unsafe path")
                target.parent.mkdir(parents=True, exist_ok=True)
                data = source.read(item)
                if not target.exists() or target.read_bytes() != data:
                    target.write_bytes(data)

    classic = BUILD / "classic"
    for name in ("Pad.h", "Settings.h"):
        write(classic / name, (REPO / "source" / name).read_text())
    settings = (REPO / "source/Settings.cpp").read_text()
    write(classic / "Settings.cpp", settings.replace('"GInputII.ini"', '"GInputSDL3Hooks.ini"'))
    main = (REPO / "source/dllmain.cpp").read_text()
    main = '#include "GInputSDL3.h"\n' + main
    vibration_begin = main.index("    static void ProcessVibration(")
    vibration_end = main.index("    static void ProcessPlayerControls(", vibration_begin)
    main = main[:vibration_begin] + main[vibration_end:]
    main = replace_once(main, "Pads[1]->Mode = settings.ControlSetP1;", "Pads[1]->Mode = settings.ControlSetP2;")
    main = replace_once(main, "delete Pads[1];", "delete Pads[1];\n        GInputSDL3_Shutdown();")
    main = replace_once(main, """        // Vibration
        if (settings.Vibration)
            ProcessVibration(_this->m_nIndex, pad, ped);""",
        "        // Vibration is controlled independently by GameVibration.cpp.")
    main = replace_once(main, "} gta2GInput;", """};
void InitializeGameVibration();
extern "C" __declspec(dllexport) void InitializeGInputSDL3Features(uint32_t features) {
    if (features & GINPUT_GAME_INPUT) {
        static GTA2GInput instance;
    }
    if (features & GINPUT_GAME_VIBRATION) InitializeGameVibration();
}
""")
    write(classic / "ClassicHooks.cpp", main)

    pad = (REPO / "source/Pad.cpp").read_text()
    pad = '#include "GInputSDL3.h"\n' + pad
    pad = pad.replace('#pragma comment(lib, "Xinput9_1_0.lib")', '')
    begin, end = pad.index("void CPad::Update()"), pad.index("void CPad::StartShake(")
    pad = pad[:begin] + (ROOT / "src/PadUpdate.inc").read_text() + "\n\n" + pad[end:]
    begin, end = pad.index("void CPad::StopShaking()"), pad.index("void CPad::Clear()")
    pad = pad[:begin] + """void CPad::StopShaking() {
    ShakeDur = 0;
    ShakeFreq = 0;
    GInputSDL3_Rumble(Index, 0.0f, 0.0f, 0);
}

""" + pad[end:]
    write(classic / "Pad.cpp", pad)

    misi = DEPS / "misi/MISI"
    original = (misi / "MISI.cpp").read_text()
    begin = original.index("int l_GetXInputConnectedDevicesNumber(lua_State* l) {")
    end = original.index("int l_SendInput(lua_State *l)", begin)
    patched = original[:begin] + "// Controller callbacks are implemented in MisiBridge.cpp.\n\n" + original[end:]
    write(BUILD / "misi/MISI.cpp", patched)
    # Keep an inspectable source patch alongside the generated build outputs.
    write(BUILD / "misi-sdl3.patch", "".join(difflib.unified_diff(
        original.splitlines(True), patched.splitlines(True),
        fromfile="a/MISI/MISI.cpp", tofile="b/MISI/MISI.cpp")))
    # Only a pragma library path is modernized; Lua itself is built unmodified.
    header = (misi / "misimethods.h").read_text()
    header = header.replace('"lua-5.3.4/Debug/lua.lib"', '"MisiLua.lib"')
    header = header.replace('"lua-5.3.4/Release/lua.lib"', '"MisiLua.lib"')
    write(BUILD / "misi/misimethods.h", header)
    for name in ("stdafx.h", "targetver.h", "gta2_address.h", "appmethods.h"):
        write(BUILD / "misi" / name, (misi / name).read_text())
    print("Prepared SDL3, MISI bridge, and optional classic adapter under build/.")


if __name__ == "__main__":
    prepare()
