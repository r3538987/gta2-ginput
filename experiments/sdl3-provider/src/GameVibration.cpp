#include "plugin.h"
#include "CGame.h"
#include "CPlayerPed.h"
#include "CPed.h"
#include "CReplay.h"
#include "GInputSDL3.h"
#include "VibrationEffects.h"
#include <cstdio>
#include <cstdarg>
#include <string>

namespace {
game_vibration::Effects effects;
uint32_t lastSlot = GINPUT_SDL3_NO_PAD, lastInstance = 0;
bool pulseActive = false;
const char* lastStatus = nullptr;

void Trace(const char* format, ...) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Trace), &self);
    wchar_t path[32768]{};
    GetModuleFileNameW(self, path, 32768);
    std::wstring logPath(path);
    logPath.replace(logPath.find_last_of(L'.'), std::wstring::npos, L".log");
    FILE* log = nullptr;
    if (_wfopen_s(&log, logPath.c_str(), L"a") != 0) return;
    std::fprintf(log, "[%llu] ", GetTickCount64());
    va_list args;
    va_start(args, format);
    std::vfprintf(log, format, args);
    va_end(args);
    std::fputc('\n', log);
    std::fclose(log);
}

void Status(const char* status) {
    if (lastStatus == status) return;
    lastStatus = status;
    Trace("Observer=%s", status);
}

void Stop() {
    if (pulseActive && lastSlot != GINPUT_SDL3_NO_PAD)
        GInputSDL3_Rumble(lastSlot, 0, 0, 0);
    effects.Reset();
    pulseActive = false;
}

void Process(CPlayerPed* player) {
    if (!player || player->m_nIndex != 0) return;
    static bool reached = false;
    if (!reached) { Trace("Player callback reached"); reached = true; }
    GInputSDL3_Update();
    const auto slot = GInputSDL3_GetSelectedSlot();
    GInputSDL3State pad{};
    if (!GInputSDL3_GetState(slot, &pad, sizeof(pad)) || !pad.connected) {
        Status("no SDL3 controller");
        Stop();
        lastSlot = GINPUT_SDL3_NO_PAD;
        lastInstance = 0;
        return;
    }
    if (slot != lastSlot || pad.instanceId != lastInstance) {
        Stop();
        lastSlot = slot;
        lastInstance = pad.instanceId;
        Trace("SelectedSlot=%u Instance=%u", slot, pad.instanceId);
    }
    auto game = GetGame();
    auto ped = player ? player->m_pPed : nullptr;
    if (!game || game->GetIsUserPaused() || !ped || ped->m_nHealth <= 0 ||
        (gReplay && gReplay->IsPlayingBack())) {
        Status(!game ? "no game" : !ped ? "no ped" : ped->m_nHealth <= 0 ? "dead" : "pause/replay");
        Stop();
        return;
    }
    Status("tracking player");

    game_vibration::Snapshot observation{};
    observation.ped = reinterpret_cast<uintptr_t>(ped);
    observation.pedId = ped->m_nId;
    observation.health = ped->m_nHealth;
    auto weapon = ped->m_pSelectedWeapon;
    if (weapon) {
        observation.weapon = reinterpret_cast<uintptr_t>(weapon);
        observation.ammo = weapon->m_nAmmo;
        observation.firing = weapon->m_nTimer != 0;
    }
    auto car = ped->m_pCurrentCar;
    if (car) {
        observation.car = reinterpret_cast<uintptr_t>(car);
        observation.carDamage = car->m_nDamage;
        auto physics = car->m_pPhysics;
        observation.roughSurface = physics &&
            physics->m_vVelocity.FromInt16().Magnitude() > 0.01f &&
            physics->m_nTileSurfaceType == TILE_SURFACE_TYPE_GRASS;
    }
    // Use the game's action state, independent of GInput's button layout.
    observation.special = player->m_bButtonSpecial;
    const auto pulse = effects.Update(observation, GetTickCount64());
    if (pulse.milliseconds) {
        pulseActive = GInputSDL3_Rumble(slot, pulse.strength, 0, pulse.milliseconds) != 0;
        static ULONGLONG nextReport = 0;
        if (GetTickCount64() >= nextReport) {
            Trace("Rumble=%s Slot=%u Strength=%.3f Duration=%u Health=%d Ammo=%d Timer=%d",
                pulseActive ? "sent" : "failed", slot, pulse.strength, pulse.milliseconds,
                observation.health, observation.ammo, observation.firing);
            nextReport = GetTickCount64() + 2000;
        }
    } else if (pulseActive) {
        GInputSDL3_Rumble(slot, 0, 0, 0);
        pulseActive = false;
    }
}
}

void InitializeGameVibration() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    Trace("Vibration observer installed at 0x004A7683; SDKVersion=%u; no control writes", plugin::GetGameVersion());
    plugin::Events::initEngineEvent.before += [] { Stop(); };
    // Match the working vibration-only GInput ASI's sampling phase. Other mods,
    // including Seuko, replace the generic 0x45C3E9/0x45C3F2 frame call sites.
    plugin::ThiscallEvent<plugin::AddressList<0x4A7683, plugin::H_CALL>, plugin::PRIORITY_AFTER,
        plugin::ArgPickN<CPlayerPed*, 0>, void(CPlayerPed*)> observer;
    observer.after += [](CPlayerPed* player) { Process(player); };
    plugin::Events::shutdownEngineEvent.before += [] {
        Stop();
        GInputSDL3_Shutdown();
    };
}
