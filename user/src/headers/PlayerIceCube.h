#pragma once

#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Util/SensorMsgFunction.h"

class PlayerIceCube : public al::LiveActor {
public:
    PlayerIceCube(const char* name) : al::LiveActor(name) {}

    void init(const al::ActorInitInfo& info) override {
        al::initActorWithArchiveName(this, info, "PlayerIceCube", nullptr);
        makeActorDead();
    }

    void control() override {
        if (mTargetActor && al::isAlive(mTargetActor)) {
            al::setTrans(this, al::getTrans(mTargetActor));
        }
    }

    bool receiveMsg(const al::SensorMsg* message, al::HitSensor* other, al::HitSensor* self) override {
        if (al::isMsgPlayerTrample(message)
            || al::isMsgPlayerHipDropAll(message)
            || al::isMsgPlayerObjHipDropReflectAll(message)
            || al::isMsgPlayerSpinAttack(message)
            || rs::isMsgHackAttack(message)
            || rs::isMsgCapReflect(message)
            || rs::isMsgCapAttack(message)
            || rs::isMsgCapAttackCollide(message)
            || rs::isMsgCapAttackStayRolling(message)
            || rs::isMsgCapStartLockOn(message)
            || rs::isMsgTsukkunThrustAll(message)
        ) {
            mWasHit = true;
            return true;
        }
        return false;
    }

    void freeze(al::LiveActor* target) {
        mTargetActor = target;
        mWasHit = false;
        al::setTrans(this, al::getTrans(target));
        makeActorAlive();
    }

    void unfreeze() {
        mTargetActor = nullptr;
        mWasHit = false;
        makeActorDead();
    }

    al::LiveActor* getTarget() const { return mTargetActor; }
    bool wasHit() const { return mWasHit; }

private:
    al::LiveActor* mTargetActor = nullptr;
    bool mWasHit = false;
};