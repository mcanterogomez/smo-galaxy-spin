#pragma once

#include "Library/Nerve/NerveStateBase.h"

namespace al { class LiveActor; }

class PlayerStateWait : public al::ActorStateBase {
public:
  al::LiveActor* mLiveActor;

    long tryGetSpecialStatusAnimName(const char** outName);
    void requestAnimName(const char* name);
    void exeWait();

public:
    u8 padding[0xE8 - sizeof(al::ActorStateBase)];
};