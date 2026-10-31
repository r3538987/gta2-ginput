#include "VibrationEffects.h"
#include <cstdio>
#include <cstdlib>

using namespace game_vibration;
void Check(bool value, const char* name) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}

int main() {
    Effects effects;
    Snapshot observation{};
    observation.ped = 1;
    observation.pedId = 7;
    observation.weapon = 2;
    observation.health = 100;
    observation.ammo = 20;
    Check(!effects.Update(observation, 0).milliseconds, "first observation establishes baseline");
    observation.ammo = 19;
    observation.firing = true;
    auto pulse = effects.Update(observation, 10);
    Check(pulse.milliseconds == 100 && pulse.strength > 0.47f, "gunshot effect");
    pulse = effects.Update(observation, 60);
    Check(pulse.milliseconds == 50, "same ammo does not retrigger gunshot");
    Check(!effects.Update(observation, 110).milliseconds, "gunshot expires on time");
    observation.ammo = 30;
    Check(!effects.Update(observation, 120).milliseconds, "ammo pickup does not count as firing");
    observation.weapon = 3;
    observation.ammo = 2;
    Check(!effects.Update(observation, 130).milliseconds, "weapon switch does not count as firing");

    observation.health = 90;
    pulse = effects.Update(observation, 150);
    Check(pulse.milliseconds == 100 && pulse.strength > 0.31f && pulse.strength < 0.32f, "injury effect");
    effects.Update(observation, 250);
    observation.health = 100;
    Check(!effects.Update(observation, 260).milliseconds, "healing is not an injury");

    observation.car = 4;
    observation.carDamage = 50;
    Check(!effects.Update(observation, 300).milliseconds, "entering a damaged car establishes baseline");
    observation.carDamage = 60;
    Check(effects.Update(observation, 310).milliseconds == 50, "car impact effect");
    effects.Update(observation, 360);
    observation.car = 5;
    observation.carDamage = 100;
    Check(!effects.Update(observation, 370).milliseconds, "changing cars does not count as impact");

    observation.roughSurface = true;
    Check(effects.Update(observation, 400).milliseconds == 50, "moving over grass effect");
    observation.ammo = 1;
    pulse = effects.Update(observation, 410);
    Check(pulse.milliseconds == 100 && pulse.strength > 0.47f, "gunfire wins over rough surface");
    pulse = effects.Update(observation, 480);
    Check(pulse.strength > 0.47f, "rough surface does not truncate active gunshot");
    pulse = effects.Update(observation, 510);
    Check(pulse.milliseconds == 50 && pulse.strength < 0.32f, "continuous rough surface does not prolong gunshot strength");
    observation.roughSurface = false;
    Check(!effects.Update(observation, 560).milliseconds, "rough surface feedback expires after leaving it");

    observation.car = 0;
    observation.special = true;
    Check(effects.Update(observation, 600).milliseconds == 50, "on-foot special action feedback");
    observation.special = false;
    effects.Update(observation, 650);
    observation.health = 80;
    Check(effects.Update(observation, 660).milliseconds != 0, "pending effect before respawn");
    observation.pedId = 8;
    Check(!effects.Update(observation, 670).milliseconds, "respawn clears pending effects even if pointer reused");
    observation.health = 0;
    Check(!effects.Update(observation, 680).milliseconds, "death clears effects");
    observation.health = 100;
    Check(!effects.Update(observation, 690).milliseconds, "new live baseline after death");
    observation.health = 90;
    Check(effects.Update(observation, 700).milliseconds != 0, "effect before reset");
    effects.Reset();
    Check(!effects.Update(observation, 710).milliseconds, "pause or controller switch reset clears history");
    observation.ammo = 10;
    observation.firing = false;
    effects.Update(observation, 800);
    observation.ammo = 9;
    Check(!effects.Update(observation, 810).milliseconds, "ammo change waits for firing phase");
    observation.firing = true;
    Check(effects.Update(observation, 820).milliseconds == 100,
        "ammo decrease before weapon timer still produces gunshot vibration");
    std::puts("PASS: gunfire, injury, vehicle effects, special action, effect mixing, expiry, respawn, and reset.");
}
