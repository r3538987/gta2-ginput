#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>

namespace game_vibration {
// Only observations cross into the effect detector; it cannot write game state.
struct Snapshot {
    uintptr_t ped = 0, weapon = 0, car = 0;
    uint32_t pedId = 0;
    int health = 0, ammo = -1, carDamage = -1;
    bool firing = false, roughSurface = false, special = false;
};

struct Pulse {
    uint32_t milliseconds = 0;
    float strength = 0;
};

class Effects {
    Snapshot previous{};
    bool havePrevious = false;
    uintptr_t sampledWeapon = 0;
    int firingAmmo = -1;
    // Gunfire, injury, car impact, and continuous rough/special feedback.
    std::array<uint64_t, 4> until{};

public:
    void Reset() {
        previous = {}; havePrevious = false; until.fill(0);
        sampledWeapon = 0; firingAmmo = -1;
    }

    Pulse Update(const Snapshot& current, uint64_t now) {
        if (!current.ped || current.health <= 0) {
            Reset();
            return {};
        }
        if (!havePrevious || current.ped != previous.ped || current.pedId != previous.pedId) {
            Reset();
        } else {
            if (current.health < previous.health) until[1] = now + 100;
            if (current.car && current.car == previous.car &&
                current.carDamage > previous.carDamage) until[2] = now + 50;
        }
        if (current.weapon != sampledWeapon || current.ammo < 0 || current.ammo > firingAmmo) {
            sampledWeapon = current.weapon;
            firingAmmo = current.ammo;
        } else if (current.weapon && current.firing) {
            // Ammo can change before the weapon timer becomes nonzero. Keep
            // the last firing sample until that phase, as the old ASI does.
            if (current.ammo < firingAmmo) until[0] = now + 100;
            firingAmmo = current.ammo;
        }
        if (current.roughSurface || (!current.car && current.special)) until[3] = now + 50;
        previous = current;
        havePrevious = true;
        Pulse result{};
        constexpr float strength[] = {120.0f / 255, 80.0f / 255, 80.0f / 255, 80.0f / 255};
        for (std::size_t i = 0; i < until.size(); ++i) {
            if (until[i] > now) {
                result.strength = std::max(result.strength, strength[i]);
                result.milliseconds = std::max(result.milliseconds,
                    static_cast<uint32_t>(until[i] - now));
            }
        }
        return result;
    }
};
}
