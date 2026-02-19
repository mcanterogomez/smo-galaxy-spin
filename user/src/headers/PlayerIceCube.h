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
        syncToTarget();

        al::tryStartAction(this, "Appear");

        f32 effectScale = mScale * kEffectScaleMult;
        al::setEffectAllScale(this, "Appear", sead::Vector3f(effectScale, effectScale, effectScale));

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
        if (mIsBreaking) {
            f32 effectScale = mScale * kEffectScaleMult;
            al::setEffectAllScale(this, "Break", sead::Vector3f(effectScale, effectScale, effectScale));
        } else {
            makeActorDead();
        }
    }

    al::LiveActor* getTarget() const { return mTarget; }
    bool wasHit() const { return mWasHit; }
    al::HitSensor* getAttacker() const { return mAttacker; }

    void markHit(al::HitSensor* attacker) {
        mWasHit = true;
        mAttacker = attacker;
    }

private:
    static constexpr f32 kScalePadding = 1.25f;
    static constexpr f32 kMinScale = 0.5f;
    static constexpr f32 kEffectScaleMult = 0.5f;
    static constexpr f32 kGroundRayLength = 500.0f;
    static constexpr f32 kAOERadius = 500.0f;

    void syncToTarget() {
        if (!mTarget) return;

        // Calculate scale from target's sensor radius
        al::HitSensor* sensor = al::getHitSensor(mTarget, "Body");
        if (!sensor && mTarget->getHitSensorKeeper())
            sensor = mTarget->getHitSensorKeeper()->getSensor(0);

        if (sensor) {
            f32 enemyDiameter = al::getSensorRadius(sensor) * 2.0f;
            sead::BoundBox3f cubeBox;
            al::calcModelBoundingBox(&cubeBox, this);
            f32 cubeSize = sead::Mathf::max(cubeBox.getSizeX(),
                            sead::Mathf::max(cubeBox.getSizeY(), cubeBox.getSizeZ()));
            if (cubeSize > 0.001f)
                mScale = sead::Mathf::max((enemyDiameter * kScalePadding) / cubeSize, kMinScale);
        }

        al::setScaleAll(this, mScale);

        // Scale sensor radius to match cube
        if (sensor) al::setSensorRadius(this, "Body", al::getSensorRadius(sensor) * mScale);

        // Position cube
        sead::Vector3f pos = al::getTrans(mTarget);
        sead::Vector3f gravity = al::getGravity(mTarget);
        sead::BoundBox3f cubeBox;
        al::calcModelBoundingBox(&cubeBox, this);
        f32 halfHeight = cubeBox.getSizeY() * mScale * 0.5f;

        // Raycast slightly above enemy position
        sead::Vector3f rayStart = pos - gravity;
        sead::Vector3f rayDelta = gravity * kGroundRayLength;
        sead::Vector3f groundPos;

        if (alCollisionUtil::getHitPosOnArrow(mTarget, &groundPos, rayStart, rayDelta, nullptr, nullptr)) {
            if ((groundPos - pos).dot(gravity) < halfHeight)
                pos = groundPos - (gravity * halfHeight);
        }

        al::setTrans(this, pos);
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

            if (al::isSensorPlayerAll(other))
                continue;

            if (al::isNear(otherActor, center, kAOERadius))
                al::sendMsgExplosion(other, selfSensor, nullptr);
        }
    }

    al::LiveActor* mTarget = nullptr;
    al::HitSensor* mAttacker = nullptr;
    f32 mScale = 1.0f;
    bool mWasHit = false;
    bool mIsBreaking = false;
};