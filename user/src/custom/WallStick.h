#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"
#include "Library/Collision/CollisionPartsTriangle.h"
#include "Library/Collision/CollisionPartsKeeperUtil.h"

namespace WallStick {

    enum : int { Idle = -1, Enter = 0, Active = 1, Exit = 2 };

    inline const sead::Vector3f defaultGravity = {0.f, -1.f, 0.f};
    inline sead::Vector3f stickGravity = defaultGravity;

    inline void enterDrill(al::LiveActor* model) {
        al::hideModelIfShow(model);
        al::hideSilhouetteModelIfShow(model);
        al::tryStartSe(model, "DrillIn");
    }

    inline void exitDrill(al::LiveActor* model) {
        al::showModelIfHide(model);
        al::showSilhouetteModelIfHide(model);
        al::tryStopSe(model, "DrillMove", -1, nullptr);
        al::tryStartSe(model, "DrillOut");
    }

    inline void popDrill(al::LiveActor* model) {
        exitDrill(model);
        al::tryEmitEffect(model, "DrillLand", nullptr);
        al::tryStartSe(model, "DrillSpin");

        if (isHakoniwa) {
            al::validateHitSensor(isHakoniwa, "GalaxySpin");
            hitBufferCount = 0;
            drillSensorRemaining = 25; // Allow drill hitbox for a few frames after popping
        }
    }

    inline void resetGravity(PlayerActorHakoniwa* thisPtr) {
        stickGravity = defaultGravity;
        al::setGravity(thisPtr, defaultGravity);
    }

    inline void snapGravityToWall(PlayerActorHakoniwa* thisPtr) {
        stickGravity = -rs::getCollidedWallNormal(thisPtr->mCollider);
        al::setGravity(thisPtr, stickGravity);
    }

    // Shoots a ray along stickGravity. If it hits a surface, updates
    // stickGravity and actor gravity to that surface's normal and returns true.
    inline bool isFoundSurface(PlayerActorHakoniwa* thisPtr) {
        const sead::Vector3f up = -stickGravity;
        al::Triangle tri;
        sead::Vector3f hitPos;

        if (alCollisionUtil::getFirstPolyOnArrow(thisPtr, &hitPos, &tri,
                al::getTrans(thisPtr) + up * 80.f, stickGravity * 150.f, nullptr, nullptr)) {
            stickGravity = -tri.mNormals[0];
            al::setGravity(thisPtr, stickGravity);
            return true;
        }
        return false;
    }

    inline void update(PlayerActorHakoniwa* thisPtr) {
        auto* model = thisPtr->mModelHolder->findModelActor("Normal");
        auto* anim  = thisPtr->mAnimator;
        auto* input = thisPtr->mInput;
        const bool onGround = rs::isPlayerOnGround(thisPtr);
        const bool onWall   = rs::isCollidedWall(thisPtr->mCollider);
        const bool isHoldZR = al::isPadHoldZR(-1);
        const bool isMoving = input->isMove();

        switch (drillStep) {

            // Not drilling. Start on ZR+surface. Reset gravity if airborne.
            case Idle: {
                if (!onGround && !onWall) { resetGravity(thisPtr); return; }
                if (!isHoldZR || !canAction || isActionBusy()
                    || input->isTriggerJump() || al::isInWater(thisPtr)
                    || PlayerEquipmentFunction::isEquipmentForceDash(thisPtr->mEquipmentUser)) return;

                canAction = false;
                // Force-cancel any active player state (spin cap, taunt, etc).
                al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                anim->endSubAnim();

                if (onWall) {
                    // Wall: snap gravity, no animation
                    snapGravityToWall(thisPtr);
                    anim->startSubAnim("DrillIn");
                    drillStep = Enter;
                } else {
                    // Ground: play DrillIn anim
                    stickGravity = defaultGravity;
                    if (!al::isEqualSubString(anim->mCurAnim, "HipDrop")) anim->startSubAnim("DrillIn");
                    drillStep = Enter;
                }
                break;
            }

            // DrillIn anim playing. Wait for it to finish. Nothing interrupts.
            case Enter: {
                if (!anim->isSubAnim("DrillIn") || anim->isSubAnimEnd()) {
                    enterDrill(model);
                    drillStep = Active;
                }
                break;
            }

            // Drilled in, stuck to surface.
            case Active: {
                // Jump: pop out. Don't reset gravity — jump uses it to launch
                if (input->isTriggerJump()) {
                    popDrill(model);
                    drillStep = Idle;
                    break;
                }

                // ZR released: play DrillOut anim
                if (!isHoldZR) {
                    resetGravity(thisPtr);
                    anim->startSubAnim(isMoving ? "DrillOutFast" : "DrillOut");
                    exitDrill(model);
                    drillStep = Exit;
                    break;
                }

                // Stay stuck: wall takes priority, then raycast, else lost
                if (onWall) snapGravityToWall(thisPtr);
                else if (!onGround && !isFoundSurface(thisPtr)) {
                    resetGravity(thisPtr);
                    anim->startSubAnim("DrillOutFast");
                    exitDrill(model);
                    drillStep = Exit;
                    break;
                }

                // Fx + sound
                al::tryEmitEffect(model, "DrillMove", nullptr);
                if (isMoving && !al::checkIsPlayingSe(model, "DrillMove", nullptr)) al::tryStartSe(model, "DrillMove");
                else if (!isMoving) al::tryStopSe(model, "DrillMove", -1, nullptr);
                break;
            }

            // DrillOut anim playing. Wait for it to finish. Nothing interrupts.
            case Exit: {
                al::setNerve(thisPtr, getNerveAt((!onGround || isMoving) ? nrvHakoniwaFall : nrvHakoniwaWait));
                drillStep = Idle;
                break;
            }
        }
    }

    struct PlayerJudgeStartSquatHook : public mallow::hook::Trampoline<PlayerJudgeStartSquatHook> {
        static bool Callback(void* thisPtr) {
            if (isDrill && isHakoniwa->mHackCap->isPutOn()
                && al::isPadHoldZR(-1)) return false;

            return Orig(thisPtr);
        }
    };

    inline void Install() {
        PlayerJudgeStartSquatHook::InstallAtSymbol("_ZNK21PlayerJudgeStartSquat5judgeEv");
    }
}