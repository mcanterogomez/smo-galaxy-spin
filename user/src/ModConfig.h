#pragma once

#include <mallow/config.hpp>

// =========================================================
//              COMPILE-TIME FLAGS
// =========================================================

// Disables galaxy spin hooks entirely — vanilla Cappy throw only.
//#define ALLOW_CAPPY_ONLY

// Gates all power-up suits, fireballs, ice, hammer, drill etc.
#define ALLOW_POWERUPS

#ifdef ALLOW_POWERUPS
    #define ALLOW_DASH // Enables dash and water surface running. Requires ALLOW_POWERUPS.
#endif

// Enables taunts via D-pad left/right while idle.
#define ALLOW_TAUNT

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
    // Enable Mario costume powers.
    #ifdef ALLOW_POWERUPS
        bool enableMario;
    #endif
    // Hide UI by default on mod load.
    bool isHide;

    void read(const ArduinoJson::JsonObject& config) override {
        mallow::config::ConfigBase::read(config);
        attackButton = (config["attackButton"] | "Y")[0];
        spinOnly = config["spinOnly"] | false;
        galaxySfx = config["galaxySfx"] | false;
        #ifdef ALLOW_POWERUPS
            enableMario = config["enableMario"] | false;
        #endif
        isHide = config["isHide"] | false;
    }

    void write(ArduinoJson::JsonObject config) override {
        config["attackButton"] = (attackButton == 'X') ? "X" : "Y";
        config["spinOnly"] = spinOnly;
        config["galaxySfx"] = galaxySfx;
        #ifdef ALLOW_POWERUPS
            config["enableMario"] = enableMario;
        #endif
        config["isHide"] = isHide;
    }
};