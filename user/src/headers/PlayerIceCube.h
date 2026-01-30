#pragma once

#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/ActorModelFunction.h"
#include "Library/LiveActor/ActorActionFunction.h"

class PlayerIceCube : public al::LiveActor {
public:
    PlayerIceCube(const char* name) : al::LiveActor(name) {}

    void init(const al::ActorInitInfo& info) override {
        al::initActorWithArchiveName(this, info, "PlayerIceCube", nullptr);
        makeActorDead();
    }

    void control() override {
        // Break animation lifecycle
        if (mIsBreaking) {
            if (al::isActionEnd(this)) {
                mIsBreaking = false;
                makeActorDead();
            }
            return;
        }

        // Track frozen target, match size
        if (mTargetActor && al::isAlive(mTargetActor)) {
            al::setTrans(this, al::getTrans(mTargetActor));
            updateScaleToMatchTarget();
        }
    }

    // Activate cube, play appear anim, size to target
    void freeze(al::LiveActor* target) {
        mTargetActor = target;
        mWasHit = false;
        mAttackerSensor = nullptr;
        mIsBreaking = false;

        al::setTrans(this, al::getTrans(target));
        makeActorAlive();
        al::tryStartAction(this, "Appear");
        updateScaleToMatchTarget();
    }

    // Play break anim at frozen pos, then kill
    void unfreeze() {
        // Lock pos at final target location
        if (mTargetActor && al::isAlive(mTargetActor)) {
            al::setTrans(this, al::getTrans(mTargetActor));
        }

        mTargetActor = nullptr;
        mWasHit = false;
        mAttackerSensor = nullptr;

        // Play destruction anim
        makeActorAlive();
        mIsBreaking = al::tryStartAction(this, "Break");
        
        // Fallback if Break doesn't exist
        if (!mIsBreaking) makeActorDead();
    }

    al::LiveActor* getTarget() const { return mTargetActor; }
    bool wasHit() const { return mWasHit; }
    al::HitSensor* getAttackerSensor() const { return mAttackerSensor; }
    
    void markHit(al::HitSensor* source) {
        mWasHit = true;
        mAttackerSensor = source;
    }

private:
    static constexpr float kScalePadding = 1.2f;
    static constexpr float kMinScale = 0.5f;

    // Get per-axis scale ratio
    sead::Vector3f getScaleRatio() const {
        sead::BoundBox3f cubeBox, targetBox;
        al::calcModelBoundingBox(&cubeBox, this);
        al::calcModelBoundingBox(&targetBox, mTargetActor);

        auto tryDivide = [](float num, float den) -> float {
            return (den != 0.0f) ? (num / den) : 0.0f;
        };
        
        return sead::Vector3f(
            tryDivide(targetBox.getSizeX(), cubeBox.getSizeX()),
            tryDivide(targetBox.getSizeY(), cubeBox.getSizeY()),
            tryDivide(targetBox.getSizeZ(), cubeBox.getSizeZ())
        );
    }

    // Match scale with uniform max axis + padding
    void updateScaleToMatchTarget() {
        if (!mTargetActor) return;

        sead::Vector3f scale = getScaleRatio();
        
        // Use max axis for uniform coverage
        float uniform = sead::Mathf::max(scale.x, sead::Mathf::max(scale.y, scale.z));
        
        // Apply padding and clamp to minimum
        uniform = sead::Mathf::max(uniform * kScalePadding, kMinScale);

        al::setScale(this, sead::Vector3f(uniform, uniform, uniform));
    }

    al::LiveActor* mTargetActor = nullptr;
    bool mWasHit = false;
    al::HitSensor* mAttackerSensor = nullptr;
    bool mIsBreaking = false;
};