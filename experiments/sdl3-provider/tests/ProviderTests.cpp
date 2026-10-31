#define WIN32_LEAN_AND_MEAN
#define SDL_MAIN_HANDLED
#include <windows.h>
#include <SDL3/SDL.h>
#include "GInputSDL3.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int l_GetXInputConnectedDevicesNumber(lua_State*);
int l_IsXInputKeyPress(lua_State*);
int l_GetXInputAxes(lua_State*);

void Check(bool condition, const char* message) {
    if (!condition) {
        char error[512]{};
        GInputSDL3_GetLastError(error, sizeof(error));
        std::fprintf(stderr, "FAIL: %s\nSDL: %s\nProvider: %s\n", message, SDL_GetError(), error);
        std::exit(1);
    }
}

std::wstring ConfigPath() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    std::wstring config(path);
    return config.substr(0, config.find_last_of(L"\\/") + 1) + L"ProviderTests.ini";
}

void SetConfig(const wchar_t* key, const wchar_t* value) {
    Check(WritePrivateProfileStringW(L"SDL3", key, value, ConfigPath().c_str()) != 0, "write test config");
}

GInputSDL3State StateFor(SDL_JoystickID id, uint32_t* slot = nullptr) {
    Check(GInputSDL3_Update() != 0, "update initialized provider");
    GInputSDL3State state{};
    for (uint32_t i = 0; i < GINPUT_SDL3_MAX_PADS; ++i) {
        Check(GInputSDL3_GetState(i, &state, sizeof(state)) != 0, "read state");
        if (state.connected && state.instanceId == id) {
            if (slot) *slot = i;
            return state;
        }
    }
    Check(false, "virtual gamepad must be discovered");
    return {};
}

struct RumbleProbe { Uint16 low = 0, high = 0; unsigned calls = 0; };
bool SDLCALL CaptureRumble(void* userdata, Uint16 low, Uint16 high) {
    if (auto probe = static_cast<RumbleProbe*>(userdata)) {
        probe->low = low;
        probe->high = high;
        ++probe->calls;
    }
    return true;
}

SDL_JoystickID Attach(RumbleProbe* probe = nullptr) {
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "GInput SDL3 integration test";
    desc.userdata = probe;
    desc.Rumble = CaptureRumble;
    const auto id = SDL_AttachVirtualJoystick(&desc);
    Check(id != 0, "attach virtual controller");
    return id;
}

void LuaCheck(lua_State* lua, const char* code) {
    const int status = luaL_dostring(lua, code);
    if (status) std::fprintf(stderr, "Lua error: %s\n", lua_tostring(lua, -1));
    Check(status == LUA_OK, "MISI Lua bridge assertions");
}

int main() {
    GInputSDL3State state{};
    state.buttons = ~0u;
    Check(!GInputSDL3_GetState(0, &state, sizeof(state)) && state.buttons == 0,
        "uninitialized reads return neutral data");
    Check(!GInputSDL3_Update(), "polling before initialization is safe");
    Check(GInputSDL3_GetSelectedSlot() == GINPUT_SDL3_NO_PAD, "no selected controller before initialization");
    SetConfig(L"EnableGameInputHooks", L"0");
    SetConfig(L"EnableGameVibration", L"0");
    SetConfig(L"AllowBackgroundInput", L"1");
    SetConfig(L"DeadZonePercent", L"20");
    SetConfig(L"TriggerThresholdPercent", L"25");
    Check(GInputSDL3_GetAPIVersion() == 1, "ABI version");
    Check(GInputSDL3_Initialize(ConfigPath().c_str()) != 0, "initialize provider in a non-game host");
    Check(!GInputSDL3_AreGameInputHooksEnabled(), "provider-only initialization installs no game adapter");
    Check(!GInputSDL3_IsGameVibrationEnabled(), "provider-only initialization installs no vibration observer");
    Check(!GetModuleHandleW(L"GInputSDL3Hooks.dll"), "classic adapter is not even loaded");
    Check(!SDL_GamepadEventsEnabled() && !SDL_JoystickEventsEnabled(),
        "manual polling does not accumulate unused controller events");
    Check(!GInputSDL3_GetState(0, nullptr, sizeof(state)), "reject null output");
    Check(!GInputSDL3_GetState(0, &state, sizeof(state) - 1), "reject undersized output");
    Check(!GInputSDL3_GetState(4, &state, sizeof(state)), "reject invalid slot");

    RumbleProbe firstRumble{};
    const auto first = Attach(&firstRumble);
    auto joystick = SDL_OpenJoystick(first);
    Check(joystick != nullptr, "open virtual joystick");
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    uint32_t firstSlot = 0;
    state = StateFor(first, &firstSlot);
    Check(state.leftX == 0 && state.rightY == 0 && state.buttons == 0, "neutral controller");
    Check(GInputSDL3_GetConnectedCount() >= 1, "connected count");
    Check(GInputSDL3_GetSelectedSlot() <= firstSlot, "selection is the first occupied slot");
    Check(GInputSDL3_Rumble(firstSlot, 0.5f, 0.25f, 100) != 0, "SDL3 rumble dispatch");
    Check(firstRumble.calls && firstRumble.low == 32767 && firstRumble.high == 16383,
        "virtual controller receives the actual low/high motor strengths");
    Check(GInputSDL3_Rumble(firstSlot, 0, 0, 0) != 0 && !firstRumble.low && !firstRumble.high,
        "explicit rumble stop reaches controller");

    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, -32768);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -32768);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 16384);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, 32767);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, true);
    state = StateFor(first);
    Check(state.leftX == -1 && state.leftY == 1 && state.rightY == -1, "axis limits and Y inversion");
    Check(std::abs(state.rightX - 0.500015f) < 0.001f, "analog magnitude preserved");
    Check(state.buttons == ((1u << GINPUT_A) | (1u << GINPUT_START) | (1u << GINPUT_RIGHT_TRIGGER)),
        "MISI button numbering and trigger threshold");
    Check(state.rightTrigger == 1 && state.leftTrigger == 0, "analog trigger range");

    // Exercise the exact callbacks linked into patched MISI, with its Lua runtime.
    lua_State* lua = luaL_newstate();
    Check(lua != nullptr, "create Lua 5.3.4 state");
    luaL_openlibs(lua);
    lua_register(lua, "GetXInputConnectedDevicesNumber", l_GetXInputConnectedDevicesNumber);
    lua_register(lua, "IsXInputKeyPress", l_IsXInputKeyPress);
    lua_register(lua, "GetXInputAxes", l_GetXInputAxes);
    // Bridge chooses the first connected pad, so its input assertions require
    // the test controller in the first occupied slot (normal CI/no hardware).
    if (firstSlot == 0) {
        LuaCheck(lua, "assert(GetXInputConnectedDevicesNumber() >= 1); "
            "assert(IsXInputKeyPress(10) and IsXInputKeyPress(15)); "
            "assert(not IsXInputKeyPress(-1) and not IsXInputKeyPress(16)); "
            "local x,y,rx,ry = GetXInputAxes(); assert(x == -1 and y == 1 and ry == -1); "
            "assert(rx > 0.49 and rx < 0.51)");
    }

    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 6000);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, -6000);
    state = StateFor(first);
    Check(state.leftX == 0 && state.rightX == 0, "MISI-style deadzone");

    // MISI runs scripts on worker threads. Serialize concurrent reads/polls.
    std::thread reader([] {
        for (int i = 0; i < 100; ++i) {
            GInputSDL3State value{};
            Check(GInputSDL3_Update() != 0, "worker poll");
            Check(GInputSDL3_GetState(0, &value, sizeof(value)) != 0 && value.apiVersion == 1,
                "worker snapshot");
        }
    });
    for (int i = 0; i < 100; ++i) StateFor(first);
    reader.join();

    RumbleProbe secondRumble{};
    const auto second = Attach(&secondRumble);
    uint32_t secondSlot = 0;
    StateFor(second, &secondSlot);
    Check(secondSlot != firstSlot, "controllers occupy different slots");
    SDL_CloseJoystick(joystick);
    Check(SDL_DetachVirtualJoystick(first), "detach first controller");
    GInputSDL3_Update();
    GInputSDL3_GetState(firstSlot, &state, sizeof(state));
    Check(!state.connected && !state.buttons && state.leftX == 0, "disconnect clears held inputs");
    uint32_t stableSlot = 0;
    StateFor(second, &stableSlot);
    Check(stableSlot == secondSlot, "remaining controller retains its slot");
    if (GInputSDL3_GetSelectedSlot() == secondSlot) {
        Check(GInputSDL3_Rumble(GInputSDL3_GetSelectedSlot(), 0.3f, 0, 50) != 0 &&
            secondRumble.calls && secondRumble.low > 0, "rumble follows TwinStick selection after disconnect");
    }
    Check(SDL_DetachVirtualJoystick(second), "detach second controller");
    GInputSDL3_Update();
    lua_close(lua);
    GInputSDL3_Shutdown();
    Check(!GInputSDL3_Update() && !GInputSDL3_GetConnectedCount(), "clean shutdown");

    // A mistakenly enabled game adapter must never patch a non-game process.
    SetConfig(L"EnableGameInputHooks", L"1");
    Check(!GInputSDL3_Initialize(ConfigPath().c_str()), "report the rejected adapter request");
    char error[512]{};
    GInputSDL3_GetLastError(error, sizeof(error));
    Check(std::strstr(error, "requires gta2.exe") != nullptr, "game adapter rejected outside GTA2");
    Check(!GInputSDL3_AreGameInputHooksEnabled() && !GetModuleHandleW(L"GInputSDL3Hooks.dll"),
        "unsafe mode does not load adapter");
    GInputSDL3_Shutdown();
    SetConfig(L"EnableGameInputHooks", L"0");
    SetConfig(L"EnableGameVibration", L"1");
    Check(!GInputSDL3_Initialize(ConfigPath().c_str()), "vibration-only request is rejected outside GTA2");
    Check(!GInputSDL3_IsGameVibrationEnabled() && !GInputSDL3_AreGameInputHooksEnabled() &&
        !GetModuleHandleW(L"GInputSDL3Hooks.dll"), "vibration toggle never enables input outside GTA2");
    GInputSDL3_Shutdown();
    std::puts("PASS: provider startup, SDL3 input/rumble, MISI Lua bridge, concurrency, hotplug, and independent adapter toggles.");
    return 0;
}
