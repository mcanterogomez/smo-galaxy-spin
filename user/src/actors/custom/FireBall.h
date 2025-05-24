#pragma once

#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/Nerve/NerveUtil.h"
#include <math/seadVector.h>
#include <math/seadQuat.h>

class FireBrosFireBall : public al::LiveActor {
public:
    FireBrosFireBall(const char* name, const al::LiveActor* host);

    virtual void init(const al::ActorInitInfo& info) override;

    void attach(const sead::Matrix34f* attachMtx,
                const sead::Vector3f& unkVec1,
                const sead::Vector3f& unkVec2,
                const char* attachAction);

    void shoot(const sead::Vector3f& startPos,
               const sead::Quatf& startQuat,
               const sead::Vector3f& offset,
               bool isHackAttack,
               int unused,
               bool unkBool);

private:
    char buffer[0x140 - sizeof(al::LiveActor)];
};