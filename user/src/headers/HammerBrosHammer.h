#pragma once

#include "Enemy/Bros.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/Nerve/NerveUtil.h"
#include <math/seadVector.h>
#include <math/seadQuat.h>

class HammerBrosHammer : public BrosWeaponBase {
public:
    HammerBrosHammer(const char*          name,
                     const al::LiveActor* host,
                     const char*          archiveName,
                     bool                 flag);

    void init(const al::ActorInitInfo& info) override;

    void attach(const sead::Matrix34f* handMtx,
                const sead::Vector3f&  unk1,
                const sead::Vector3f&  unk2,
                const char*            attachAction) override;

    void shoot(const sead::Vector3f& startPos,
               const sead::Quatf&    startQuat,
               const sead::Vector3f& offset,
               bool                  isHackAttack,
               int                   unused,
               bool                  unk) override;

    void killCollide(al::HitSensor* sensor, const sead::Vector3f& trans, bool isHack) override {}
    void killEnemy() override {}

private:
    char buffer[0x178 - sizeof(BrosWeaponBase)];
};