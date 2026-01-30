#pragma once

#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/ActorModelFunction.h"
#include "Library/LiveActor/ActorActionFunction.h"
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

        if (mTarget && al::isAlive(mTarget)) {
            updateTransform();
        }
    }

    void freeze(al::LiveActor* target) {
        mTarget = target;
        mWasHit = false;
        mAttacker = nullptr;
        mIsBreaking = false;

        makeActorAlive();
        al::tryStartAction(this, "Appear");
        updateTransform();
    }

    void unfreeze() {
        if (mTarget && al::isAlive(mTarget)) updateTransform();

        mTarget = nullptr;
        mWasHit = false;
        mAttacker = nullptr;

        makeActorAlive();
        mIsBreaking = al::tryStartAction(this, "Break");
        if (!mIsBreaking) makeActorDead();
    }

    al::LiveActor* getTarget() const { return mTarget; }
    bool wasHit() const { return mWasHit; }
    al::HitSensor* getAttackerSensor() const { return mAttacker; }

    void markHit(al::HitSensor* source) {
        mWasHit = true;
        mAttacker = source;
    }

private:
    static constexpr float kPadding = 1.5f;
    static constexpr float kMinScale = 0.5f;
    static constexpr float kGroundRayDist = 500.0f;

    void updateTransform() {
        if (!mTarget) return;

        // Scale: uniform based on largest target dimension
        sead::BoundBox3f cubeBox, targetBox;
        al::calcModelBoundingBox(&cubeBox, this);
        al::calcModelBoundingBox(&targetBox, mTarget);

        sead::Vector3f cubeSize = cubeBox.getMax() - cubeBox.getMin();
        sead::Vector3f targetSize = targetBox.getMax() - targetBox.getMin();

        float maxRatio = 0.0f;
        if (cubeSize.x > 0) maxRatio = sead::Mathf::max(maxRatio, targetSize.x / cubeSize.x);
        if (cubeSize.y > 0) maxRatio = sead::Mathf::max(maxRatio, targetSize.y / cubeSize.y);
        if (cubeSize.z > 0) maxRatio = sead::Mathf::max(maxRatio, targetSize.z / cubeSize.z);

        float scale = sead::Mathf::max(maxRatio * kPadding, kMinScale);
        al::setScale(this, sead::Vector3f(scale, scale, scale));

        // Position: center on target, lift if clipping ground
        sead::Vector3f pos = al::getTrans(mTarget);
        float halfHeight = (cubeSize.y * scale) * 0.5f;

        sead::Vector3f groundHit;
        if (alCollisionUtil::getHitPosOnArrow(mTarget, &groundHit, pos,
                sead::Vector3f(0, -kGroundRayDist, 0), nullptr, nullptr)) {
            float bottom = pos.y - halfHeight;
            if (bottom < groundHit.y) pos.y = groundHit.y + halfHeight;
        }

        al::setTrans(this, pos);
    }

    al::LiveActor* mTarget = nullptr;
    al::HitSensor* mAttacker = nullptr;
    bool mWasHit = false;
    bool mIsBreaking = false;
};