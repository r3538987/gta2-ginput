#define WIN32_LEAN_AND_MEAN
#define SDL_MAIN_HANDLED
#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "GInputSDL3.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>

namespace {
std::mutex lock;
SDL_Gamepad* pads[GINPUT_SDL3_MAX_PADS]{};
GInputSDL3State states[GINPUT_SDL3_MAX_PADS]{};
bool initialized = false, hooksEnabled = false, backgroundInput = false;
bool vibrationEnabled = false;
bool previousGamepadEvents = true;
bool previousJoystickEvents = true;
DWORD ownerThread = 0;
float deadzone = 0.20f, triggerThreshold = 0.25f;
uint64_t sample = 0;
char lastError[512]{};
HMODULE hooksModule = nullptr;
std::wstring ModulePath();

void Error(const char* message) {
    if (std::strcmp(lastError, message) == 0) return;
    strncpy_s(lastError, message, _TRUNCATE);
    OutputDebugStringA("GInputSDL3: ");
    OutputDebugStringA(lastError);
    OutputDebugStringA("\n");
    auto path = ModulePath();
    path.replace(path.find_last_of(L'.'), std::wstring::npos, L".log");
    FILE* log = nullptr;
    if (_wfopen_s(&log, path.c_str(), L"a") == 0) {
        std::fprintf(log, "Error=%s\n", lastError);
        std::fclose(log);
    }
}

std::wstring ModulePath() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GInputSDL3_Initialize), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
    return path;
}

float Axis(Sint16 value, bool invert = false) {
    float normalized = std::max(-1.0f, value / 32767.0f);
    // Match MISI's existing per-axis deadzone, without rescaling the remainder.
    if (std::abs(normalized) <= deadzone) return 0.0f;
    return invert ? -normalized : normalized;
}

bool InForeground() {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return backgroundInput || process == GetCurrentProcessId();
}

void RefreshPads() {
    for (auto& pad : pads) {
        if (pad && !SDL_GamepadConnected(pad)) {
            SDL_CloseGamepad(pad);
            pad = nullptr;
        }
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        bool open = false;
        for (auto pad : pads)
            if (pad && SDL_GetGamepadID(pad) == ids[i]) open = true;
        if (open) continue;
        for (auto& pad : pads) {
            if (!pad) {
                pad = SDL_OpenGamepad(ids[i]);
                break;
            }
        }
    }
    SDL_free(ids);
}

bool EnableHooks() {
    // Provider-only use is safe in a diagnostic host; game adapters are GTA2-only.
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    const wchar_t* name = wcsrchr(executable, L'\\');
    if (_wcsicmp(name ? name + 1 : executable, L"gta2.exe") != 0) {
        Error("The game adapter requires gta2.exe; no input or vibration callbacks were installed.");
        return false;
    }
    const auto module = ModulePath();
    const auto path = module.substr(0, module.find_last_of(L"\\/") + 1) + L"GInputSDL3Hooks.dll";
    hooksModule = LoadLibraryW(path.c_str());
    if (!hooksModule) {
        Error("Cannot load GInputSDL3Hooks.dll. It is required for enabled input or game vibration.");
        return false;
    }
    const auto start = GetProcAddress(hooksModule, "InitializeGInputSDL3Features");
    if (!start) {
        Error("GInputSDL3Hooks.dll is outdated. Copy the matching DLL from the experimental package.");
        FreeLibrary(hooksModule);
        hooksModule = nullptr;
        return false;
    }
    // The adapter subscribes to game events and subsequently calls this API.
    // Caller must release the provider lock before invoking it.
    return true;
}
}

uint32_t __cdecl GInputSDL3_GetAPIVersion() { return GINPUT_SDL3_API_VERSION; }

int __cdecl GInputSDL3_Initialize(const wchar_t* iniPath) {
    std::unique_lock<std::mutex> guard(lock);
    if (initialized) return 1;
    auto configPath = ModulePath();
    configPath.replace(configPath.find_last_of(L'.'), std::wstring::npos, L".ini");
    if (iniPath) configPath = iniPath;
    const auto read = [&](const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(L"SDL3", key, fallback, configPath.c_str()));
    };
    const bool requestedHooks = read(L"EnableGameInputHooks", 0) != 0;
    // Existing INIs without the new setting retain provider-only behavior.
    const bool requestedVibration = read(L"EnableGameVibration", 0) != 0;
    backgroundInput = read(L"AllowBackgroundInput", 0) != 0;
    deadzone = std::clamp(read(L"DeadZonePercent", 20), 0, 99) / 100.0f;
    triggerThreshold = std::clamp(read(L"TriggerThresholdPercent", 25), 0, 99) / 100.0f;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        Error(SDL_GetError());
        return 0;
    }
    ownerThread = GetCurrentThreadId();
    previousGamepadEvents = SDL_GamepadEventsEnabled();
    previousJoystickEvents = SDL_JoystickEventsEnabled();
    SDL_SetGamepadEventsEnabled(false);
    SDL_SetJoystickEventsEnabled(false);
    initialized = true;
    lastError[0] = '\0';
    if (requestedHooks || requestedVibration) {
        if (!EnableHooks()) return 0;
        // Mark the mode before calling out, but never call an adapter under lock.
        hooksEnabled = requestedHooks;
        vibrationEnabled = requestedVibration;
        const uint32_t features = (requestedHooks ? GINPUT_GAME_INPUT : 0u) |
            (requestedVibration ? GINPUT_GAME_VIBRATION : 0u);
        const auto start = reinterpret_cast<void(__cdecl*)(uint32_t)>(
            GetProcAddress(hooksModule, "InitializeGInputSDL3Features"));
        guard.unlock();
        start(features);
    }
    return 1;
}

void __cdecl GInputSDL3_Shutdown() {
    std::lock_guard<std::mutex> guard(lock);
    if (!initialized) return;
    if (ownerThread != GetCurrentThreadId()) {
        Error("Shutdown must run on the thread that initialized SDL3.");
        return;
    }
    for (auto& pad : pads) {
        if (pad) SDL_CloseGamepad(pad);
        pad = nullptr;
    }
    std::memset(states, 0, sizeof(states));
    SDL_SetGamepadEventsEnabled(previousGamepadEvents);
    SDL_SetJoystickEventsEnabled(previousJoystickEvents);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    initialized = false;
    ownerThread = 0;
}

int __cdecl GInputSDL3_Update() {
    std::lock_guard<std::mutex> guard(lock);
    if (!initialized) return 0;
    SDL_UpdateGamepads();
    RefreshPads();
    ++sample;
    const bool focused = InForeground();
    constexpr SDL_GamepadButton buttons[] = {
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
        SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
        SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH
    };
    for (uint32_t i = 0; i < GINPUT_SDL3_MAX_PADS; ++i) {
        auto& state = states[i];
        state = {};
        state.apiVersion = GINPUT_SDL3_API_VERSION;
        state.sampleNumber = sample;
        auto pad = pads[i];
        if (!pad) continue;
        state.connected = 1;
        state.instanceId = SDL_GetGamepadID(pad);
        if (!focused) {
            SDL_RumbleGamepad(pad, 0, 0, 0);
            continue;
        }
        state.leftX = Axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
        state.leftY = Axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY), true);
        state.rightX = Axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
        state.rightY = Axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY), true);
        state.leftTrigger = std::max(0.0f, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f);
        state.rightTrigger = std::max(0.0f, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f);
        for (uint32_t b = 0; b < std::size(buttons); ++b)
            if (SDL_GetGamepadButton(pad, buttons[b])) state.buttons |= 1u << b;
        if (state.leftTrigger > triggerThreshold) state.buttons |= 1u << GINPUT_LEFT_TRIGGER;
        if (state.rightTrigger > triggerThreshold) state.buttons |= 1u << GINPUT_RIGHT_TRIGGER;
    }
    return 1;
}

int __cdecl GInputSDL3_GetState(uint32_t slot, GInputSDL3State* state, uint32_t size) {
    if (!state || size < sizeof(*state)) return 0;
    std::lock_guard<std::mutex> guard(lock);
    *state = {};
    state->apiVersion = GINPUT_SDL3_API_VERSION;
    if (!initialized || slot >= GINPUT_SDL3_MAX_PADS) return 0;
    *state = states[slot];
    return 1;
}

uint32_t __cdecl GInputSDL3_GetConnectedCount() {
    std::lock_guard<std::mutex> guard(lock);
    uint32_t count = 0;
    for (const auto& state : states) count += state.connected != 0;
    return count;
}

uint32_t __cdecl GInputSDL3_GetSelectedSlot() {
    std::lock_guard<std::mutex> guard(lock);
    if (initialized) {
        for (uint32_t i = 0; i < GINPUT_SDL3_MAX_PADS; ++i)
            if (states[i].connected) return i;
    }
    return GINPUT_SDL3_NO_PAD;
}

int __cdecl GInputSDL3_Rumble(uint32_t slot, float low, float high, uint32_t milliseconds) {
    std::lock_guard<std::mutex> guard(lock);
    if (!initialized || slot >= GINPUT_SDL3_MAX_PADS || !pads[slot] ||
        !std::isfinite(low) || !std::isfinite(high)) return 0;
    if (!InForeground()) low = high = 0.0f;
    if (!SDL_RumbleGamepad(pads[slot], static_cast<Uint16>(std::clamp(low, 0.0f, 1.0f) * 65535),
        static_cast<Uint16>(std::clamp(high, 0.0f, 1.0f) * 65535), milliseconds)) {
        Error(SDL_GetError());
        return 0;
    }
    return 1;
}

void __cdecl GInputSDL3_GetLastError(char* message, uint32_t capacity) {
    if (!message || !capacity) return;
    std::lock_guard<std::mutex> guard(lock);
    strncpy_s(message, capacity, lastError, _TRUNCATE);
}

int __cdecl GInputSDL3_AreGameInputHooksEnabled() {
    std::lock_guard<std::mutex> guard(lock);
    return hooksEnabled ? 1 : 0;
}

int __cdecl GInputSDL3_IsGameVibrationEnabled() {
    std::lock_guard<std::mutex> guard(lock);
    return vibrationEnabled ? 1 : 0;
}

extern "C" __declspec(dllexport) void __cdecl InitializeASI() {
    const int ready = GInputSDL3_Initialize(nullptr);
    char error[512]{};
    GInputSDL3_GetLastError(error, sizeof(error));
    auto path = ModulePath();
    path.replace(path.find_last_of(L'.'), std::wstring::npos, L".log");
    FILE* log = nullptr;
    if (_wfopen_s(&log, path.c_str(), L"w") == 0) {
        std::fprintf(log, "GInput SDL3 experiment; API %u; SDL %d\nInitialized=%d\nEnableGameInputHooks=%d\nEnableGameVibration=%d\n%s\n",
            GINPUT_SDL3_API_VERSION, SDL_GetVersion(), ready,
            GInputSDL3_AreGameInputHooksEnabled(), GInputSDL3_IsGameVibrationEnabled(), error);
        std::fclose(log);
    }
}

// Deliberately no SDL initialization, threads, or game patches inside DllMain.
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
