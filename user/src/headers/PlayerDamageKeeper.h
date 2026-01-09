#pragma once

#include <basis/seadTypes.h>

namespace al { class LiveActor; }
class PlayerEffect;              

class PlayerDamageKeeper {
public:
    void activatePreventDamage();
    void dead();
    void damage(s32 level);

public:
    al::LiveActor* mPlayerActor;            // 0x00
    PlayerEffect*  mPlayerEffect;           // 0x08
    bool           _10;                     // 0x10 (unknown flag)
    s32            _14;                     // 0x14 (unknown int)
    char           filler[4];               // 0x18 (padding)
    bool           mIsPreventDamage;        // 0x1C
    char           filler2[3];              // 0x1D (padding)
    s32            mRemainingInvincibility; // 0x20
    s32            filler3;                 // 0x24 (padding)
    void*          gap;                     // 0x28 (unknown)
};