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
    bool           mIsDamageDirty;          // 0x10 (was _10)
    s32            mFlickerTimer;           // 0x14 (was _14)
    s32            _18;                     // 0x18
    bool           mIsPreventDamage;        // 0x1C
    char           filler2[3];              // 0x1D
    s32            mRemainingInvincibility; // 0x20
    s32            filler3;                 // 0x24
    void*          gap;                     // 0x28
};