#pragma once

#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorModelFunction.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/ActorCollisionFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/HitSensor/HitSensorKeeper.h"
#include "Library/Collision/CollisionPartsKeeperUtil.h"

class PlayerIceCube : public al::LiveActor {
public:
    PlayerIceCube(const char* name) : al::LiveActor(name) {}

    void init(const al::ActorInitInfo& info) override {
        al::initActorWithArchiveName(this, info, "PlayerIceCube", nullptr);
        makeActorDead();
    }

    void control() override {
        if (mIsBreaking) {
            if (al::isActionEnd(this)) {
                mIsBreaking = false;
                makeActorDead();
            }
            return;
        }

        // Guard: kill cube if target is gone
        if (!mTarget || !al::isAlive(mTarget)) {
            mTarget = nullptr;
            makeActorDead();
            return;
        }

        syncToTarget();
    }

    void freeze(al::LiveActor* target) {
        mTarget = target;
        mWasHit = false;
        mAttacker = nullptr;
        mIsBreaking = false;

        makeActorAlive();
        al::tryStartAction(this, "Appear");
        syncToTarget();
        sendAOEExplosion();
    }

    void unfreeze() {
        if (mTarget && al::isAlive(mTarget))
            syncToTarget();

        mTarget = nullptr;
        mWasHit = false;
        mAttacker = nullptr;

        makeActorAlive();
        mIsBreaking = al::tryStartAction(this, "Break");
        if (!mIsBreaking)
            makeActorDead();
    }

    al::LiveActor* getTarget() const { return mTarget; }
    bool wasHit() const { return mWasHit; }
    al::HitSensor* getAttacker() const { return mAttacker; }

    void markHit(al::HitSensor* attacker) {
        mWasHit = true;
        mAttacker = attacker;
    }

private:
    static constexpr f32 kScalePadding = 1.5f;
    static constexpr f32 kMinScale = 0.5f;
    static constexpr f32 kGroundRayLength = 500.0f;
    static constexpr f32 kAOERadius = 500.0f;

    void syncToTarget() {
        if (!mTarget) return;

        f32 scale = calcTargetScale();
        al::setScaleAll(this, scale);

        sead::Vector3f pos = al::getTrans(mTarget);

        sead::BoundBox3f cubeBox;
        al::calcModelBoundingBox(&cubeBox, this);
        f32 halfHeight = cubeBox.getSizeY() * scale * 0.5f;

        // Grounded: use collision data directly
        if (al::isOnGround(mTarget, 0)) {
            pos.y = al::getCollidedGroundPos(mTarget).y + halfHeight;
        }
        // Airborne: raycast to check if near ground
        else {
            sead::Vector3f groundPos;
            sead::Vector3f rayDelta(0.0f, -kGroundRayLength, 0.0f);

            if (alCollisionUtil::getHitPosOnArrow(mTarget, &groundPos, pos, rayDelta, nullptr, nullptr)) {
                if (pos.y - halfHeight < groundPos.y)
                    pos.y = groundPos.y + halfHeight;
            }
        }

        al::setTrans(this, pos);
    }

    f32 calcTargetScale() const {
        sead::BoundBox3f cubeBox, targetBox;
        al::calcModelBoundingBox(&cubeBox, this);
        al::calcModelBoundingBox(&targetBox, mTarget);

        auto safeDivide = [](f32 a, f32 b) { return b > 0.001f ? a / b : 0.0f; };

        f32 ratioX = safeDivide(targetBox.getSizeX(), cubeBox.getSizeX());
        f32 ratioY = safeDivide(targetBox.getSizeY(), cubeBox.getSizeY());
        f32 ratioZ = safeDivide(targetBox.getSizeZ(), cubeBox.getSizeZ());

        f32 maxRatio = sead::Mathf::max(ratioX, sead::Mathf::max(ratioY, ratioZ));
        return sead::Mathf::max(maxRatio * kScalePadding, kMinScale);
    }

    void sendAOEExplosion() {
        if (!mTarget) return;

        al::HitSensor* selfSensor = al::getHitSensor(mTarget, "Body");
        if (!selfSensor && mTarget->getHitSensorKeeper())
            selfSensor = mTarget->getHitSensorKeeper()->getSensor(0);
        if (!selfSensor) return;

        sead::Vector3f center = al::getTrans(this);

        for (u16 i = 0; i < selfSensor->mSensorCount; i++) {
            al::HitSensor* other = selfSensor->mSensors[i];
            if (!other) continue;

            al::LiveActor* otherActor = other->getParentActor();
            if (!otherActor || otherActor == mTarget || !al::isAlive(otherActor))
                continue;

            if (al::isNear(otherActor, center, kAOERadius))
                al::sendMsgExplosion(other, selfSensor, nullptr);
        }
    }

    al::LiveActor* mTarget = nullptr;
    al::HitSensor* mAttacker = nullptr;
    bool mWasHit = false;
    bool mIsBreaking = false;
};