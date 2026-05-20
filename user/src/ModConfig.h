#pragma once

#include <mallow/config.hpp>

// =========================================================
//              COMPILE-TIME FLAGS
// =========================================================

// Disables galaxy spin hooks entirely — vanilla Cappy throw only.
//#define ALLOW_CAPPY_ONLY

// Gates all power-up suits, fireballs, ice, hammer, drill etc.
#define ALLOW_POWERUPS
    #define ALLOW_MARIO // Enables Mario costume detection for powers and blaster.
    #define ALLOW_DASH // Enables dash and water surface running. Requires ALLOW_POWERUPS.

#ifndef ALLOW_POWERUPS
    #undef ALLOW_MARIO
    #undef ALLOW_DASH
#endif

// Enables taunts via D-pad left/right while idle.
#define ALLOW_TAUNT

// Patches out Cappy eyes at binary level.
#define REMOVE_CAPPY_EYES

// =========================================================
//              RUNTIME CONFIG  (mod_config.json)
// =========================================================

struct ModConfig : public mallow::config::ConfigBase {
    // Which button triggers attack: "Y" (default) or "X".
    char attackButton;
    // Use spin instead of punch as attack.
    bool spinOnly;
    // Emit Galaxy-style SFX and particles on spin.
    bool galaxySfx;

    void read(const ArduinoJson::JsonObject& config) override {
        mallow::config::ConfigBase::read(config);

        attackButton = (config["attackButton"] | "Y")[0];
        spinOnly = config["spinOnly"] | false;
        galaxySfx = config["galaxySfx"] | false;
    }
};