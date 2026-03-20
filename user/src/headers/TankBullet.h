#pragma once
#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include <math/seadVector.h>

class TankBullet : public al::LiveActor {
public:
    TankBullet(const char* name);
    void init(const al::ActorInitInfo& info) override;
    void shoot(const sead::Vector3f& pos, const sead::Vector3f& velocity, int lifetime, bool, bool);

private:
    char buffer[0x190 - sizeof(al::LiveActor)];
};