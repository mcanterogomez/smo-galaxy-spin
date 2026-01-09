#pragma once

#include <basis/seadTypes.h>
#include "Player/IPlayerModelChanger.h"

class PlayerEffect {
public:
    PlayerEffect(al::LiveActor* actor, const PlayerModelHolder* holder, const sead::Matrix34<float>* matrix);

    bool tryEmitInvincibleEffect();
    void suspendInvincibleEffect();
    void restartInvincibleEffect();
    bool tryDeleteInvincibleEffect();
    void updateInvincibleEffect(const IPlayerModelChanger* modelChanger, bool flag);
};