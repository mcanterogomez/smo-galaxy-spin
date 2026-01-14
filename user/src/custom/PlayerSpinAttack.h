#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"
#include "custom/_Nerves.h"

namespace PlayerSpinAttack {

    struct InputIsTriggerActionXexclusivelyHook : public mallow::hook::Trampoline<InputIsTriggerActionXexclusivelyHook> {
        static bool Callback(const al::LiveActor* actor, int port) {
            if(port == 100) return Orig(actor, PlayerFunction::getPlayerInputPort(actor));

            /*if (al::isPadHoldZL(port) || al::isPadHoldZR(port)
            ) {
                const PlayerActorHakoniwa* player = (const PlayerActorHakoniwa*)actor;
                if (!rs::isOnGround(player, player->mCollider)) return Orig(actor, port);
            }*/

            bool canCapThrow = true;

            switch (mallow::config::getConfg<ModOptions>()->spinButton) {
                case 'Y':
                    canCapThrow = al::isPadTriggerX(port);
                    break;
                case 'X':
                    canCapThrow = al::isPadTriggerY(port);
                    break;
            }
            return Orig(actor, port) && canCapThrow;
        }
    };

    struct InputIsTriggerActionCameraResetHook : public mallow::hook::Trampoline<InputIsTriggerActionCameraResetHook> {
        static bool Callback(const al::LiveActor* actor, int port) {
            switch (mallow::config::getConfg<ModOptions>()->spinButton) {
                case 'L':
                    return al::isPadTriggerR(port);
                /*case 'R':
                    return al::isPadTriggerL(port);*/
            }
            return Orig(actor, port);
        }
    };

    // shared pre-logic for TryActionCapSpinAttack hooks
    static inline int TryCapSpinPre(PlayerActorHakoniwa* player) {
        bool newIsCarry = player->mCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) { prevIsCarry = newIsCarry; return -1; }
        prevIsCarry = newIsCarry;

        // Fresh spin sequence called from normal input, not from tryCapSpinAndRethrow
        if (!isSpinRethrow) {
            canGalaxySpin = true;
            canStandardSpin = true;
            isGalaxyAfterStandardSpin = false;
            isStandardAfterGalaxySpin = false;
        }

        if (isPadTriggerGalaxySpin(-1)
            && !rs::is2D(player)
            && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)
        ) {
            if (player->mAnimator->isAnim("SpinSeparate")
            || player->mAnimator->isAnim("SpinSeparateSwim")
            || player->mAnimator->isAnim("SpinAttackLeft")
            || player->mAnimator->isAnim("SpinAttackRight")
            || player->mAnimator->isAnim("SpinAttackAirLeft")
            || player->mAnimator->isAnim("SpinAttackAirRight")
            || player->mAnimator->isAnim("KoopaCapPunchL")
            || player->mAnimator->isAnim("KoopaCapPunchR")
            || player->mAnimator->isAnim("KoopaCapPunchFinishL")
            || player->mAnimator->isAnim("KoopaCapPunchFinishR")        
            || player->mAnimator->isAnim("RabbitGet")
            || player->mAnimator->isAnim("Kick")
            || player->mAnimator->isAnim("CapeAttack")
            || player->mAnimator->isAnim("TailAttack")) return -1;

            if (canGalaxySpin) triggerGalaxySpin = true;
            else { triggerGalaxySpin = true; galaxyFakethrowRemainder = -2; }
            return 1;
        }

        if (isFireThrowing()) return -1;

        if (al::isPadTriggerR(-1)
            && !rs::is2D(player)
            && !player->mCarryKeeper->isCarry()
            && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) canFireball = true;

        return 0; // fallthrough to Orig
    }

    struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack> {
        static bool Callback(PlayerActorHakoniwa* player, bool a2) {
            switch (TryCapSpinPre(player)
            ) {
                case 1:  return true;
                case -1: return false;
            }
            if(Orig(player, a2)) { triggerGalaxySpin = false; return true; }
            return false;
        }
    };

    struct PlayerTryActionCapSpinAttackBindEnd : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttackBindEnd> {
        static bool Callback(PlayerActorHakoniwa* player, bool a2) {
            switch (TryCapSpinPre(player)
            ) {
                case 1:  return true;
                case -1: return false;
            }
            if(Orig(player, a2)) { triggerGalaxySpin = false; return true; }
            return false;
        }
    };

    struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear> {
        static void Callback(PlayerStateSpinCap* state) {
            const bool isGrounded = rs::isOnGround(state->mActor, state->mCollider)
                && !state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val2);
            const bool forcedGroundSpin = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val33);

            // Safety fix: clear leftover spin state from area load mid-spin
            if (galaxyFakethrowRemainder != -1 &&
                !al::isNerve(state, &GalaxySpinGround) &&
                !al::isNerve(state, &GalaxySpinAir)) {
                galaxyFakethrowRemainder = -1;
                isGalaxySpin = false;
                // DO NOT reset triggerGalaxySpin here!
            }

            // Handle cross-spin transition flags
            if (isGalaxyAfterStandardSpin) {
                isGalaxyAfterStandardSpin = false;
                canStandardSpin = false;
                triggerGalaxySpin = true;
            }
            if (isStandardAfterGalaxySpin) {
                isStandardAfterGalaxySpin = false;
                canGalaxySpin = false;
                triggerGalaxySpin = false;
            }

            // If not a GalaxySpin, run original cap throw logic
            if (!triggerGalaxySpin) {
                canStandardSpin = false;
                isGalaxySpin = false;
                Orig(state); // Mario goes full 2017
                return;
            }

            // Now we’re in GalaxySpin mode
            hitBufferCount = 0;
            isGalaxySpin = true;
            canGalaxySpin = false;
            triggerGalaxySpin = false;

            // Reset internal flags
            state->mIsDead = false;
            state->mIsInWater = false;
            state->_99 = 0;
            state->_80 = 0;
            state->_9C = {0.0f, 0.0f, 0.0f};
            state->_A8 = 0;
            state->_A9 = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val0);

            if (forcedGroundSpin || isGrounded) {
                if (state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val1)) {
                    al::alongVectorNormalH(
                        al::getVelocityPtr(state->mActor),
                        al::getVelocity(state->mActor),
                        al::getGravity(state->mActor),
                        rs::getCollidedGroundNormal(state->mCollider)
                    );
                }
                state->mActionGroundMoveControl->appear();
                al::setNerve(state, &GalaxySpinGround);
            } else {
                state->_78 = 1;
                if (isGalaxySpin && galaxyFakethrowRemainder == -2)
                    al::setNerve(state, getNerveAt(nrvSpinCapFall));
                else
                    al::setNerve(state, &GalaxySpinAir);
            }
        }
    };

    struct PlayerStateSpinCapKill : public mallow::hook::Trampoline<PlayerStateSpinCapKill> {
        static void Callback(PlayerStateSpinCap* state) {
            Orig(state);

            isPunching = false;
            isSpinActive = false;
            isNearCollectible = false;
            isNearTreasure = false;
            isNearSwoonedEnemy = false;

            galaxyFakethrowRemainder = -1; 
            al::invalidateHitSensor(state->mActor, "Punch");
        }
    };

    struct PlayerStateSpinCapFall : public mallow::hook::Trampoline<PlayerStateSpinCapFall> {
        static void Callback(PlayerStateSpinCap* state) {
            Orig(state);
            // If fakethrow is active and the current animation is "SpinSeparate"
            if (galaxyFakethrowRemainder != -1 && state->mAnimator->isAnim("SpinSeparate")) {
                bool onGround = rs::isOnGround(state->mActor, state->mCollider);
                if (onGround) {
                    // Transition to the ground spin nerve without restarting the animation.
                    state->mActionGroundMoveControl->appear();
                    al::setNerve(state, &GalaxySpinGround);
                    return;
                }
            }
            // Normal FakeSpin timer logic for when still airborne:
            if (galaxyFakethrowRemainder == -2) {
                galaxyFakethrowRemainder = 21;
                al::validateHitSensor(state->mActor, "GalaxySpin");
                // Start the SpinSeparate animation if it hasn't been started yet.
                //state->mAnimator->startSubAnim("SpinSeparate");
                state->mAnimator->startAnim("SpinSeparate");
                galaxySensorRemaining = 21;
            } else if (galaxyFakethrowRemainder > 0) {
                galaxyFakethrowRemainder--;
            } else if (galaxyFakethrowRemainder == 0) {
                galaxyFakethrowRemainder = -1;
                al::invalidateHitSensor(state->mActor, "GalaxySpin");
            }
        }
    };

    struct PlayerStateSpinCapIsEnableCancelHipDrop : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelHipDrop> {
        static bool Callback(PlayerStateSpinCap* state) {
            return Orig(state) || (al::isNerve(state, &GalaxySpinAir) && al::isGreaterStep(state, 10));
        }
    };

    struct PlayerStateSpinCapIsEnableCancelAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelAir> {
        static bool Callback(PlayerStateSpinCap* state) {
            return Orig(state) && !(!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
        }
    };

    struct PlayerStateSpinCapIsSpinAttackAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsSpinAttackAir> {
        static bool Callback(PlayerStateSpinCap* state) {
            return Orig(state) || (!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
        }
    };

    struct PlayerStateSpinCapIsEnableCancelGround : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelGround> {
        static bool Callback(PlayerStateSpinCap* state) {
            // Check if Mario is in the GalaxySpinGround nerve and performing the SpinSeparate move
            bool isSpin = state->mAnimator->isAnim("SpinSeparate")
                || state->mAnimator->isAnim("SpinSeparateSwim")
                || state->mAnimator->isAnim("SpinAttackLeft")
                || state->mAnimator->isAnim("SpinAttackRight")
                || state->mAnimator->isAnim("CapeAttack")
                || state->mAnimator->isAnim("TailAttack");

            // Allow canceling only if Mario is in the SpinSeparate move
            return Orig(state) || (al::isNerve(state, &GalaxySpinGround) && isSpin && al::isGreaterStep(state, 10));
        }
    };

    struct PlayerSpinCapAttackIsSeparateSingleSpin : public mallow::hook::Trampoline<PlayerSpinCapAttackIsSeparateSingleSpin> {
        static bool Callback(PlayerStateSwim* thisPtr) {
            if(triggerGalaxySpin) {
                return true;
            }
            return Orig(thisPtr);
        }
    };

    struct PlayerStateSwimExeSwimSpinCap : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCap> {
        static void Callback(PlayerStateSwim* thisPtr) {
            Orig(thisPtr);
            if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
                al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
                hitBufferCount = 0;
                isGalaxySpin = true;
                triggerGalaxySpin = false;
                isSpinActive = true;

                if (isNearCollectible || isNearTreasure || isNearSwoonedEnemy) al::validateHitSensor(thisPtr->mActor, "Punch");
            }

            if(isGalaxySpin && (al::isGreaterStep(thisPtr, 15) || al::isStep(thisPtr, -1)))
                al::invalidateHitSensor(thisPtr->mActor, "Punch");

            if(isGalaxySpin && (al::isGreaterStep(thisPtr, 32) || al::isStep(thisPtr, -1))) {
                al::invalidateHitSensor(thisPtr->mActor, "GalaxySpin");
                isGalaxySpin = false;
                isSpinActive = false;
            }
        }
    };

    struct PlayerStateSwimExeSwimSpinCapSurface : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCapSurface> {
        static void Callback(PlayerStateSwim* thisPtr) {
            Orig(thisPtr);
            if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
                al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
                hitBufferCount = 0;
                isGalaxySpin = true;
                triggerGalaxySpin = false;
                isSpinActive = true;

                if (isNearCollectible || isNearTreasure || isNearSwoonedEnemy) al::validateHitSensor(thisPtr->mActor, "Punch");
            }

            if(isGalaxySpin && (al::isGreaterStep(thisPtr, 15) || al::isStep(thisPtr, -1)))
                al::invalidateHitSensor(thisPtr->mActor, "Punch");

            if(isGalaxySpin && (al::isGreaterStep(thisPtr, 32) || al::isStep(thisPtr, -1))) {
                al::invalidateHitSensor(thisPtr->mActor, "GalaxySpin");
                isGalaxySpin = false;
                isSpinActive = false;
            }
        }
    };

    struct PlayerStateSwimKill : public mallow::hook::Trampoline<PlayerStateSwimKill> {
        static void Callback(PlayerStateSwim* state) {
            Orig(state);
            isGalaxySpin = false;
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            isSpinActive = false;
        }
    };

    struct PlayerSpinCapAttackStartSpinSeparateSwimSurface : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwimSurface> {
        static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
            auto* holder = isHakoniwa->mModelHolder;
            auto* model  = holder->findModelActor("Normal");
            auto* cape = al::tryGetSubActor(model, "ケープ");

            if(!isGalaxySpin && !triggerGalaxySpin) {
                Orig(thisPtr, animator);
                return;
            }

            if (isNearCollectible) animator->startAnim("RabbitGet");
            else if (isNearTreasure || isNearSwoonedEnemy) animator->startAnim("Kick");
            else if ((isMario && cape && al::isAlive(cape)) || isFeather) animator->startAnim("CapeAttack");
            else if (isTanooki) animator->startAnim("TailAttack");
            else animator->startAnim("SpinSeparateSwim");
        }
    };

    struct PlayerSpinCapAttackStartSpinSeparateSwim : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwim> {
        static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
            auto* holder = isHakoniwa->mModelHolder;
            auto* model  = holder->findModelActor("Normal");
            auto* cape = al::tryGetSubActor(model, "ケープ");

            if(!isGalaxySpin && !triggerGalaxySpin) {
                Orig(thisPtr, animator);
                return;
            }

            if (isNearCollectible) animator->startAnim("RabbitGet");
            else if (isNearTreasure || isNearSwoonedEnemy) animator->startAnim("Kick");
            else if ((isMario && cape && al::isAlive(cape)) || isFeather) animator->startAnim("CapeAttack");
            else if (isTanooki) animator->startAnim("TailAttack");
            else animator->startAnim("SpinSeparateSwim");
        }
    };

    struct DisallowCancelOnUnderwaterSpinPatch : public mallow::hook::Inline<DisallowCancelOnUnderwaterSpinPatch> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if(isGalaxySpin)
                ctx->W[20] = true;
        }
    };

    struct DisallowCancelOnWaterSurfaceSpinPatch : public mallow::hook::Inline<DisallowCancelOnWaterSurfaceSpinPatch> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if(isGalaxySpin)
                ctx->W[21] = true;
        }
    };

    void tryCapSpinAndRethrow(PlayerActorHakoniwa* player, bool a2) {
        if(isGalaxySpin) { // currently in GalaxySpin
            isSpinRethrow = true;
            bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can only succeed for standard throw
            isSpinRethrow = false;

            if(!trySpin) return;

            if(!isPadTriggerGalaxySpin(-1)) {  // standard throw or fakethrow
                if(canStandardSpin) {
                    // tries a standard spin, is allowed to do so
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    isStandardAfterGalaxySpin = true;
                    return;
                }
                else {
                    // tries a standard spin, not allowed to do so
                    //player->mPlayerSpinCapAttack->tryStartCapSpinAirMiss(player->mPlayerAnimator);
                    // fakespins on standard spins should not happen in this mod
                    return;
                }
            } else {  // Y pressed => GalaxySpin or fake-GalaxySpin
                if(galaxyFakethrowRemainder != -1 || player->mAnimator->isAnim("SpinSeparate"))
                    return;  // already in fakethrow or GalaxySpin

                if(canGalaxySpin) {
                    // tries a GalaxySpin, is allowed to do so => should never happen, but better safe than sorry
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    return;
                }
                else {
                    // tries a GalaxySpin, not allowed to do so
                    galaxyFakethrowRemainder = -2;
                    return;
                }
            }

            // not attempting or allowed to initiate a spin, so check if should be fakethrow
            if(isPadTriggerGalaxySpin(-1) && galaxyFakethrowRemainder == -1 && !player->mAnimator->isAnim("SpinSeparate")) {
                // Y button pressed, start a galaxy fakethrow
                galaxyFakethrowRemainder = -2;
                return;
            }
        }
        else { // currently in standard spin
            isSpinRethrow = true;
            bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can succeed for GalaxySpin and fakethrow
            isSpinRethrow = false;

            if(!trySpin) return;

            if(!isPadTriggerGalaxySpin(-1)) {  // standard throw or fakethrow
                if(canStandardSpin) {
                    // tries a standard spin, is allowed to do so => should never happen, but better safe than sorry
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    return;
                }
                else {
                    // tries a standard spin, not allowed to do so
                    //player->mPlayerSpinCapAttack->tryStartCapSpinAirMiss(player->mPlayerAnimator);
                    // fakespins on standard spins should not happen in this mod
                    return;
                }
            } else {  // Y pressed => GalaxySpin or fake-GalaxySpin
                if(galaxyFakethrowRemainder != -1 || player->mAnimator->isAnim("SpinSeparate"))
                    return;  // already in fakethrow or GalaxySpin

                if(canGalaxySpin) {
                    // tries a GalaxySpin, is allowed to do so
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    isGalaxyAfterStandardSpin = true;
                    return;
                }
                else {
                    // tries a GalaxySpin, not allowed to do so
                    galaxyFakethrowRemainder = -2;
                    return;
                }
            }
        }
    }

    struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            if (isFireThrowing()) return;

            if (isPadTriggerGalaxySpin(-1)
                && !thisPtr->mAnimator->isAnim("SpinSeparate")
            ) {
                if ((isMario || isBrawl)
                    && isHammer && al::isDead(isHammer)) al::setNerve(thisPtr, &HammerNrv);
                else {
                    triggerGalaxySpin = true;
                    al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                }
                return;
            }
            Orig(thisPtr);
        }
    };

    struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            if (isPadTriggerGalaxySpin(-1)
                && !thisPtr->mAnimator->isAnim("SpinSeparate")
            ) {
                if ((isMario || isBrawl)
                    && isHammer && al::isDead(isHammer)) al::setNerve(thisPtr, &HammerNrv);
                else {
                    triggerGalaxySpin = true;
                    al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                }
                return;
            }
            Orig(thisPtr);
        }
    };

    struct PlayerCarryKeeperStartThrowNoSpin : public mallow::hook::Trampoline<PlayerCarryKeeperStartThrowNoSpin> {
        static bool Callback(PlayerCarryKeeper* state) {
            if (isSpinActive || galaxySensorRemaining != -1 || galaxyFakethrowRemainder != -1) return false;
            return Orig(state); 
        }
    };

    struct PlayerCarryKeeperIsCarryDuringSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSpin> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            // if either currently in galaxyspin or already finished galaxyspin while still in-air
            if(ctx->X[0] && (isGalaxySpin || !canGalaxySpin)) ctx->X[0] = false;
        }
    };

    struct PlayerCarryKeeperIsCarryDuringSwimSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSwimSpin> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            // if either currently in galaxyspin
            if(ctx->X[0] && (isGalaxySpin || triggerGalaxySpin)) ctx->X[0] = false;
        }
    };

    inline void Install() {
        #ifndef ALLOW_CAPPY_ONLY
            // Modify triggers
            InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");
            InputIsTriggerActionCameraResetHook::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");

            // Trigger spin instead of cap throw
            PlayerTryActionCapSpinAttack::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
            PlayerTryActionCapSpinAttackBindEnd::InstallAtSymbol("_ZN19PlayerActorHakoniwa29tryActionCapSpinAttackBindEndEv");
            PlayerSpinCapAttackAppear::InstallAtSymbol("_ZN18PlayerStateSpinCap6appearEv");
            PlayerStateSpinCapKill::InstallAtSymbol("_ZN18PlayerStateSpinCap4killEv");
            PlayerStateSpinCapFall::InstallAtSymbol("_ZN18PlayerStateSpinCap7exeFallEv");
            PlayerStateSpinCapIsEnableCancelHipDrop::InstallAtSymbol("_ZNK18PlayerStateSpinCap21isEnableCancelHipDropEv");
            PlayerStateSpinCapIsEnableCancelAir::InstallAtSymbol("_ZNK18PlayerStateSpinCap17isEnableCancelAirEv");
            PlayerStateSpinCapIsSpinAttackAir::InstallAtSymbol("_ZNK18PlayerStateSpinCap15isSpinAttackAirEv");
            PlayerStateSpinCapIsEnableCancelGround::InstallAtSymbol("_ZNK18PlayerStateSpinCap20isEnableCancelGroundEv");
            PlayerSpinCapAttackIsSeparateSingleSpin::InstallAtSymbol("_ZNK19PlayerSpinCapAttack20isSeparateSingleSpinEv");
            PlayerStateSwimExeSwimSpinCap::InstallAtSymbol("_ZN15PlayerStateSwim14exeSwimSpinCapEv");
            PlayerStateSwimExeSwimSpinCapSurface::InstallAtSymbol("_ZN15PlayerStateSwim21exeSwimSpinCapSurfaceEv");
            PlayerStateSwimKill::InstallAtSymbol("_ZN15PlayerStateSwim4killEv");
            PlayerSpinCapAttackStartSpinSeparateSwimSurface::InstallAtSymbol("_ZN19PlayerSpinCapAttack28startSpinSeparateSwimSurfaceEP14PlayerAnimator");
            PlayerSpinCapAttackStartSpinSeparateSwim::InstallAtSymbol("_ZN19PlayerSpinCapAttack21startSpinSeparateSwimEP14PlayerAnimator");
            DisallowCancelOnUnderwaterSpinPatch::InstallAtOffset(0x489F30);
            DisallowCancelOnWaterSurfaceSpinPatch::InstallAtOffset(0x48A3C8);

            // Allow triggering spin on roll and squat
            PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");
            PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");

            // Allow carrying an object during a GalaxySpin
            PlayerCarryKeeperStartThrowNoSpin::InstallAtSymbol("_ZN17PlayerCarryKeeper10startThrowEb");
            PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);
            PlayerCarryKeeperIsCarryDuringSwimSpin::InstallAtOffset(0x489EE8);

            // Allow triggering another spin while falling from a spin
            exl::patch::CodePatcher fakethrowPatcher(0x423B80);
            fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
            fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
            fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
            fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
            fakethrowPatcher.Seek(0x423B9C);
            fakethrowPatcher.BranchInst(reinterpret_cast<void*>(&tryCapSpinAndRethrow));
            
            // Manually allow hacks and "special things" to use Y button
            exl::patch::CodePatcher yButtonPatcher(0x44C9FC);
            yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerHackAction
            yButtonPatcher.Seek(0x44C718);
            yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerAction
            yButtonPatcher.Seek(0x44C5F0);
            yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerCarryStart
        #endif
    }
}
