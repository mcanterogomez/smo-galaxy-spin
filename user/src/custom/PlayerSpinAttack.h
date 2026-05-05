#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"
#include "custom/_Nerves.h"

namespace PlayerSpinAttack {

    struct InputIsTriggerActionXexclusivelyHook : public mallow::hook::Trampoline<InputIsTriggerActionXexclusivelyHook> {
        static bool Callback(const al::LiveActor* actor, int port) {
            if(port == 100) return Orig(actor, PlayerFunction::getPlayerInputPort(actor));

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

    // Shared pre-logic for TryActionCapSpinAttack hooks
    static SpinPre TryCapSpinPre(PlayerActorHakoniwa* player) {
        bool newIsCarry = player->mCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) { prevIsCarry = newIsCarry; return SpinPre::Reject; }
        prevIsCarry = newIsCarry;

        if (isActionBusy()) return SpinPre::Reject;

        #ifndef ALLOW_CAPPY_ONLY
            if (!isSpinRethrow) spin.resetForNewSpin();

            if (isPadTriggerGalaxySpin(-1)
                && !rs::is2D(player)
                && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)
            ) {
                if (isSpinAnim(player->mAnimator) || isPunchAnim(player->mAnimator)) return SpinPre::Reject;

                if (spin.canGalaxy) spin.trigger = true;
                else { spin.trigger = true; spin.fakethrowRemainder = -2; }
                return SpinPre::Accept;
            }
        #endif

        if ((al::isPadTriggerR(-1) || al::isPadHoldZR(-1))
            && !rs::is2D(player)
            && !player->mCarryKeeper->isCarry()
            && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) canAction = true;

        return SpinPre::Fallthrough;
    }

    struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack> {
        static bool Callback(PlayerActorHakoniwa* player, bool a2) {
            switch (TryCapSpinPre(player)
            ) {
                case SpinPre::Accept:  return true;
                case SpinPre::Reject:  return false;
                default: break;
            }
            if(Orig(player, a2)) { spin.trigger = false; return true; }
            return false;
        }
    };

    struct PlayerTryActionCapSpinAttackBindEnd : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttackBindEnd> {
        static bool Callback(PlayerActorHakoniwa* player, bool a2) {
            switch (TryCapSpinPre(player)
            ) {
                case SpinPre::Accept:  return true;
                case SpinPre::Reject:  return false;
                default: break;
            }
            if(Orig(player, a2)) { spin.trigger = false; return true; }
            return false;
        }
    };

    struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear> {
        static void Callback(PlayerStateSpinCap* state) {
            const bool isGrounded = rs::isOnGround(state->mActor, state->mCollider)
                && !state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val2);
            const bool forcedGroundSpin = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val33);

            // Safety fix: clear leftover spin state from area load mid-spin
            if (spin.fakethrowRemainder != -1
                && !al::isNerve(state, &GalaxySpinGround)
                && !al::isNerve(state, &GalaxySpinAir)
            ) {
                spin.fakethrowRemainder = -1;
                spin.isGalaxy = false;
                // DO NOT reset spin.trigger here!
            }

            // Handle cross-spin transition flags
            if (spin.galaxyAfterStandard
            ) {
                spin.galaxyAfterStandard = false;
                spin.canStandard = false;
                spin.trigger = true;
            }

            if (spin.standardAfterGalaxy
            ) {
                spin.standardAfterGalaxy = false;
                spin.canGalaxy = false;
                spin.trigger = false;
            }

            // If not a GalaxySpin, run original cap throw logic
            if (!spin.trigger
            ) {
                spin.canStandard = false;
                spin.isGalaxy = false;
                Orig(state); // Mario goes full 2017
                return;
            }

            // Now we’re in GalaxySpin mode
            hitBufferCount = 0;
            spin.isGalaxy = true;
            spin.canGalaxy = false;
            spin.trigger = false;

            // Reset internal flags
            state->mIsDead = false;
            state->mIsInWater = false;
            state->_99 = 0;
            state->_80 = 0;
            state->_9C = {0.0f, 0.0f, 0.0f};
            state->_A8 = 0;
            state->_A9 = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val0);

            if (forcedGroundSpin || isGrounded
            ) {
                if (state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val1)
                ) {
                    al::alongVectorNormalH(al::getVelocityPtr(state->mActor), al::getVelocity(state->mActor),
                        al::getGravity(state->mActor), rs::getCollidedGroundNormal(state->mCollider));
                }
                state->mActionGroundMoveControl->appear();
                al::setNerve(state, &GalaxySpinGround);
            } else {
                state->_78 = 1;
                if (spin.isGalaxy && spin.fakethrowRemainder == -2) al::setNerve(state, getNerveAt(nrvSpinCapFall));
                else al::setNerve(state, &GalaxySpinAir);
            }
        }
    };

    inline void cleanupSpinAttackState(al::LiveActor* actor) {
        isPunchActive = false;
        isSpinActive = false;
        isNearCollectible = false;
        isNearTreasure = false;
        isNearSwoonedEnemy = false;

        spin.fakethrowRemainder = -1;
        spin.isGalaxy = false;
        attackSensorRemaining = -1;

        al::invalidateHitSensor(actor, "Punch");
        al::invalidateHitSensor(actor, "GalaxySpin");
        al::invalidateHitSensor(actor, "DoubleSpin");
    }

    struct PlayerStateSpinCapKill : public mallow::hook::Trampoline<PlayerStateSpinCapKill> {
        static void Callback(PlayerStateSpinCap* state) {
            Orig(state);
            cleanupSpinAttackState(state->mActor); 
        }
    };

    struct PlayerStateSpinCapFall : public mallow::hook::Trampoline<PlayerStateSpinCapFall> {
        static void Callback(PlayerStateSpinCap* state) {
            Orig(state);
            // If fakethrow is active and the current animation is "SpinSeparate"
            if (spin.fakethrowRemainder != -1 && state->mAnimator->isAnim("SpinSeparate")
            ) {
                bool onGround = rs::isOnGround(state->mActor, state->mCollider);
                if (onGround) {
                    // Transition to the ground spin nerve without restarting the animation.
                    state->mActionGroundMoveControl->appear();
                    al::setNerve(state, &GalaxySpinGround);
                    return;
                }
            }
            // Normal FakeSpin timer logic for when still airborne:
            if (spin.fakethrowRemainder == -2
            ) {
                spin.fakethrowRemainder = 21;
                al::validateHitSensor(state->mActor, "GalaxySpin");
                state->mAnimator->startAnim("SpinSeparate");
                attackSensorRemaining = 21;
            }
            else if (spin.fakethrowRemainder > 0) spin.fakethrowRemainder--;
            else if (spin.fakethrowRemainder == 0) spin.fakethrowRemainder = -1;
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
            return Orig(state) || (al::isNerve(state, &GalaxySpinGround) && isSpinAnim(state->mAnimator) && al::isGreaterStep(state, 10));
        }
    };

    struct PlayerSpinCapAttackIsSeparateSingleSpin : public mallow::hook::Trampoline<PlayerSpinCapAttackIsSeparateSingleSpin> {
        static bool Callback(PlayerStateSwim* thisPtr) {
            if(spin.trigger) return true;

            return Orig(thisPtr);
        }
    };

    // Shared swim spin logic
    static void SwimSpinAttackLogic(PlayerStateSwim* thisPtr) {
        if(spin.trigger && al::isFirstStep(thisPtr)
        ) {
            al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
            hitBufferCount = 0;
            spin.isGalaxy = true;
            spin.trigger = false;
            isSpinActive = true;

            if (isNearCollectible || isNearTreasure || isNearSwoonedEnemy) { al::validateHitSensor(thisPtr->mActor, "Punch"); attackSensorRemaining = 15; }
            else { al::validateHitSensor(thisPtr->mActor, "GalaxySpin"); attackSensorRemaining = 32; }
        }
    }

    struct PlayerStateSwimExeSwimSpinCap : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCap> {
        static void Callback(PlayerStateSwim* thisPtr) { Orig(thisPtr); SwimSpinAttackLogic(thisPtr); }
    };

    struct PlayerStateSwimExeSwimSpinCapSurface : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCapSurface> {
        static void Callback(PlayerStateSwim* thisPtr) { Orig(thisPtr); SwimSpinAttackLogic(thisPtr); }
    };

    struct PlayerStateSwimKill : public mallow::hook::Trampoline<PlayerStateSwimKill> {
        static void Callback(PlayerStateSwim* state) {
            Orig(state);
            cleanupSpinAttackState(state->mActor);
        }
    };

    // Shared swim anim selection
    static void SwimSpinAnimSelect(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
        if (isNearCollectible) animator->startAnim("RabbitGet");
        else if (isNearTreasure || isNearSwoonedEnemy) animator->startAnim("Kick");
        else if (isFeather) animator->startAnim("CapeAttack");
        else if (isTanooki) animator->startAnim("TailAttack");
        else {
            animator->startAnim("SpinSeparateSwim");

            #ifdef ALLOW_GALAXY_SFX
                al::tryEmitEffect(isHakoniwa->mModelHolder->findModelActor("Normal"), "SpinAttack", nullptr);
                al::tryStartSe(isHakoniwa->mModelHolder->findModelActor("Normal"), "SpinAttack");
            #endif
        }
    }

    struct PlayerSpinCapAttackStartSpinSeparateSwimSurface : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwimSurface> {
        static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
            if(!spin.isGalaxy && !spin.trigger) { Orig(thisPtr, animator); return; }
            SwimSpinAnimSelect(thisPtr, animator);
        }
    };

    struct PlayerSpinCapAttackStartSpinSeparateSwim : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwim> {
        static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
            if(!spin.isGalaxy && !spin.trigger) { Orig(thisPtr, animator); return; }
            SwimSpinAnimSelect(thisPtr, animator);
        }
    };

    struct DisallowCancelOnUnderwaterSpinPatch : public mallow::hook::Inline<DisallowCancelOnUnderwaterSpinPatch> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if(spin.isGalaxy) ctx->W[20] = true;
        }
    };

    struct DisallowCancelOnWaterSurfaceSpinPatch : public mallow::hook::Inline<DisallowCancelOnWaterSurfaceSpinPatch> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if(spin.isGalaxy) ctx->W[21] = true;
        }
    };

    void tryCapSpinAndRethrow(PlayerActorHakoniwa* player, bool a2) {
        if(spin.isGalaxy
        ) { // currently in GalaxySpin
            isSpinRethrow = true;
            bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can only succeed for standard throw
            isSpinRethrow = false;

            if(!trySpin) return;

            if(!isPadTriggerGalaxySpin(-1)
            ) {  // standard throw or fakethrow
                if(spin.canStandard) {
                    // tries a standard spin, is allowed to do so
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    spin.standardAfterGalaxy = true;
                    return;
                } else {
                    // tries a standard spin, not allowed to do so
                    //player->mPlayerSpinCapAttack->tryStartCapSpinAirMiss(player->mPlayerAnimator);
                    // fakespins on standard spins should not happen in this mod
                    return;
                }
            } else {  // Y pressed => GalaxySpin or fake-GalaxySpin
                if(spin.fakethrowRemainder != -1 || player->mAnimator->isAnim("SpinSeparate"))
                    return;  // already in fakethrow or GalaxySpin

                if(spin.canGalaxy) {
                    // tries a GalaxySpin, is allowed to do so => should never happen, but better safe than sorry
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    return;
                } else {
                    // tries a GalaxySpin, not allowed to do so
                    spin.fakethrowRemainder = -2;
                    return;
                }
            }

            // not attempting or allowed to initiate a spin, so check if should be fakethrow
            if(isPadTriggerGalaxySpin(-1) && spin.fakethrowRemainder == -1 && !player->mAnimator->isAnim("SpinSeparate")
            ) {
                // Y button pressed, start a galaxy fakethrow
                spin.fakethrowRemainder = -2;
                return;
            }
        } else { // currently in standard spin
            isSpinRethrow = true;
            bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can succeed for GalaxySpin and fakethrow
            isSpinRethrow = false;

            if(!trySpin) return;

            if(!isPadTriggerGalaxySpin(-1)
            ) {  // standard throw or fakethrow
                if(spin.canStandard) {
                    // tries a standard spin, is allowed to do so => should never happen, but better safe than sorry
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    return;
                } else {
                    // tries a standard spin, not allowed to do so
                    //player->mPlayerSpinCapAttack->tryStartCapSpinAirMiss(player->mPlayerAnimator);
                    // fakespins on standard spins should not happen in this mod
                    return;
                }
            } else {  // Y pressed => GalaxySpin or fake-GalaxySpin
                if(spin.fakethrowRemainder != -1 || player->mAnimator->isAnim("SpinSeparate"))
                    return;  // already in fakethrow or GalaxySpin

                if(spin.canGalaxy) {
                    // tries a GalaxySpin, is allowed to do so
                    al::setNerve(player, getNerveAt(spinCapNrvOffset));
                    spin.galaxyAfterStandard = true;
                    return;
                } else {
                    // tries a GalaxySpin, not allowed to do so
                    spin.fakethrowRemainder = -2;
                    return;
                }
            }
        }
    }

    // Shared squat and roll state
    static bool TriggerSpinFromState(PlayerActorHakoniwa* thisPtr) {
        if (!isPadTriggerGalaxySpin(-1) || isSpinAnim(thisPtr->mAnimator)) return false;

        if ((isMario || isBrawl)
            && isHammer && al::isDead(isHammer)) al::setNerve(thisPtr, &HammerNrv);
        else {
            spin.trigger = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
        }
        return true;
    }

    struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            if (isActionBusy()) return;

            #ifndef ALLOW_CAPPY_ONLY
                if (TriggerSpinFromState(thisPtr)) return;
            #endif

            Orig(thisPtr);
        }
    };

    struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            if (TriggerSpinFromState(thisPtr)) return;

            Orig(thisPtr);
        }
    };

    struct PlayerCarryKeeperStartThrowNoSpin : public mallow::hook::Trampoline<PlayerCarryKeeperStartThrowNoSpin> {
        static bool Callback(PlayerCarryKeeper* state) {
            if (isSpinActive || attackSensorRemaining != -1 || spin.fakethrowRemainder != -1) return false;
            return Orig(state); 
        }
    };

    struct PlayerCarryKeeperIsCarryDuringSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSpin> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            // if either currently in galaxyspin or already finished galaxyspin while still in-air
            if(ctx->X[0] && (spin.isGalaxy || !spin.canGalaxy)) ctx->X[0] = false;
        }
    };

    struct PlayerCarryKeeperIsCarryDuringSwimSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSwimSpin> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            // if either currently in galaxyspin
            if(ctx->X[0] && (spin.isGalaxy || spin.trigger)) ctx->X[0] = false;
        }
    };

    inline void Install() {

        PlayerTryActionCapSpinAttack::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
        PlayerTryActionCapSpinAttackBindEnd::InstallAtSymbol("_ZN19PlayerActorHakoniwa29tryActionCapSpinAttackBindEndEv");
        PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");

        #ifndef ALLOW_CAPPY_ONLY
            // Modify triggers
            InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");
            InputIsTriggerActionCameraResetHook::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");

            // Trigger spin instead of cap throw
            //PlayerTryActionCapSpinAttack::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
            //PlayerTryActionCapSpinAttackBindEnd::InstallAtSymbol("_ZN19PlayerActorHakoniwa29tryActionCapSpinAttackBindEndEv");
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
            //PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");
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
