#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"
#include "custom/_Nerves.h"
#include "custom/PowerUps.h"

namespace PlayerCore {

    struct PlayerActorHakoniwaInitPlayer : public mallow::hook::Trampoline<PlayerActorHakoniwaInitPlayer> {
        static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
            Orig(thisPtr, actorInfo, playerInfo);

            // Set Hakoniwa pointer
            isHakoniwa = thisPtr;

            PowerUps::executeInitPlayer(thisPtr, actorInfo, playerInfo);

            // Check for Super suit costume and cap
            const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
            const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);

            #ifdef ALLOW_MARIO
                isMario = (costume && al::isEqualString(costume, "Mario"))
                    && (cap && al::isEqualString(cap, "Mario"));
            #endif

            isNoCap = (cap && al::isEqualString(cap, "MarioNoCap"));
            isFeather = (costume && al::isEqualString(costume, "MarioFeather"));
            isFire = (costume && al::isEqualString(costume, "MarioColorFire"))
                && (cap && al::isEqualString(cap, "MarioColorFire"));
            isTanooki = (costume && al::isEqualString(costume, "MarioTanooki"))
                && (cap && al::isEqualString(cap, "MarioTanooki"));
            isBrawl = (costume && al::isEqualString(costume, "MarioColorBrawl"))
                && (cap && al::isEqualString(cap, "MarioColorBrawl"));
            isSuper = (costume && al::isEqualString(costume, "MarioColorSuper"))
                && (cap && al::isEqualString(cap, "MarioColorSuper"));
        }
    };

    struct PlayerStateWaitExeWait : public mallow::hook::Trampoline<PlayerStateWaitExeWait> {
        static void Callback(PlayerStateWait* state) {
            Orig(state);

            if (al::isFirstStep(state)
            ) {
                const char* special = nullptr;
                if (state->tryGetSpecialStatusAnimName(&special)
                ) {
                    if (al::isEqualString(special, "BattleWait")
                    ) {
                        state->requestAnimName("WaitBrawl");
                        if (isBrawl) state->requestAnimName("WaitBrawlFight");
                        else if (isSuper) state->requestAnimName("WaitSuperFight");
                    }
                    else
                        state->requestAnimName(special);
                }
                else {
                    if (isBrawl) state->requestAnimName("WaitBrawl");
                    else if (isSuper) state->requestAnimName("WaitSuper");
                }
            }
        }
    };

    struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            Orig(thisPtr);

            auto* anim   = thisPtr->mAnimator;
            auto* holder = thisPtr->mModelHolder;
            auto* model  = holder->findModelActor("Normal");
            auto* cape = al::tryGetSubActor(model, "ケープ");
            al::LiveActor* face = al::tryGetSubActor(model, "顔");

            PowerUps::executeMovement(thisPtr);

            al::HitSensor* sensorSpin = al::getHitSensor(thisPtr, "GalaxySpin");
            al::HitSensor* sensorDoubleSpin = al::getHitSensor(thisPtr, "DoubleSpin");
            al::HitSensor* sensorPunch = al::getHitSensor(thisPtr, "Punch");
            al::HitSensor* sensorHipDrop = al::getHitSensor(thisPtr, "HipDropKnockDown");

            if (sensorSpin && sensorSpin->mIsValid)
                thisPtr->attackSensor(sensorSpin, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
            
            if (sensorDoubleSpin && sensorDoubleSpin->mIsValid)
                thisPtr->attackSensor(sensorDoubleSpin, rs::tryGetCollidedWallSensor(thisPtr->mCollider));

            if (sensorPunch && sensorPunch->mIsValid)
                thisPtr->attackSensor(sensorPunch, rs::tryGetCollidedWallSensor(thisPtr->mCollider));

            if (sensorHipDrop && sensorHipDrop->mIsValid)
                thisPtr->attackSensor(sensorHipDrop, rs::tryGetCollidedGroundSensor(thisPtr->mCollider));
            
            if(galaxySensorRemaining > 0) {
                galaxySensorRemaining--;
                if(galaxySensorRemaining == 0) {
                    al::invalidateHitSensor(thisPtr, "GalaxySpin");
                    al::invalidateHitSensor(thisPtr, "DoubleSpin");
                    isGalaxySpin = false;
                    galaxySensorRemaining = -1;
                }
            }

            // Reset proximity flag
            isNearCollectible = false;
            isNearTreasure = false;
            isNearSwoonedEnemy = false;

            // Handle Mario's Carry sensor
            al::HitSensor* carrySensor = al::getHitSensor(thisPtr, "Carry");
            if (carrySensor && carrySensor->mIsValid) {
                // Check all sensors colliding with Carry sensor
                for (int i = 0; i < carrySensor->mSensorCount; i++) {
                    al::HitSensor* other = carrySensor->mSensors[i];
                    al::LiveActor* actor = al::getSensorHost(other);
                    
                    if (actor) {
                        if (al::isEqualSubString(typeid(*actor).name(), "Radish")
                            || al::isEqualSubString(typeid(*actor).name(), "Stake")
                            || al::isEqualSubString(typeid(*actor).name(), "BossRaidRivet")
                        ) {
                            isNearCollectible = true;
                            break;
                        } else if (al::isEqualSubString(typeid(*actor).name(), "TreasureBox")
                            && !al::isModelName(actor, "TreasureBoxWood")
                        ) {
                            isNearTreasure = true;
                            break;
                        } else if (al::isSensorEnemyBody(other)
                            && (al::isActionPlaying(actor, "SwoonStart")
                                || al::isActionPlaying(actor, "SwoonStartLand")
                                || al::isActionPlaying(actor, "SwoonLoop")
                                || al::isActionPlaying(actor, "Swoon"))
                        ) {
                            isNearSwoonedEnemy = true;
                            break;
                        }
                    }
                }
            }

            // Handle Koopa punch logic
            if (isKoopa && !al::isNear(thisPtr, isKoopa, 500.0f)) isFinalPunch = false;

            // Add attack to moves
            static bool wasAttackMove = false;
            const bool isAttackMove = thisPtr->mAnimator
                && (al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDrop")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDropPunch")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDropReaction")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDropPunchReaction")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinJumpDownFallL")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinJumpDownFallR")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SwimHipDrop")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SwimHipDropPunch")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SwimDive"));

            if (isAttackMove && !wasAttackMove) { al::validateHitSensor(thisPtr, "HipDropKnockDown"); hitBufferCount = 0;}
            else if (!isAttackMove && wasAttackMove) al::invalidateHitSensor(thisPtr, "HipDropKnockDown");

            wasAttackMove = isAttackMove;

            // Change animations
            if ((isBrawl || isSuper)
                && face && !al::isActionPlayingSubActor(model, "顔", "WaitAngry")) al::startActionSubActor(model, "顔", "WaitAngry");

            if (isBrawl && anim && anim->isAnim("WearEnd") && !anim->isAnim("WearEndBrawl")) anim->startAnim("WearEndBrawl");
            if (isSuper && anim && anim->isAnim("WearEnd") && !anim->isAnim("WearEndSuper")) anim->startAnim("WearEndSuper");

            if ((isMario && cape && al::isAlive(cape)) || isFeather || isBrawl || isSuper
            ) {
                if (anim && anim->isAnim("HipDropStart") && !anim->isAnim("HipDropPunchStart")) anim->startAnim("HipDropPunchStart");
                if (anim && anim->isAnim("HipDrop") && !anim->isAnim("HipDropPunch")) anim->startAnim("HipDropPunch");
                if (anim && anim->isAnim("HipDropLand") && !anim->isAnim("HipDropPunchLand")) anim->startAnim("HipDropPunchLand");
                if (anim && anim->isAnim("HipDropReaction") && !anim->isAnim("HipDropPunchReaction")) anim->startAnim("HipDropPunchReaction");

                if (anim && anim->isAnim("SwimHipDropStart") && !anim->isAnim("SwimHipDropPunchStart")) anim->startAnim("SwimHipDropPunchStart");
                if (anim && (anim->isAnim("SwimHipDrop") || anim->isAnim("SwimDive")) && !anim->isAnim("SwimHipDropPunch")) anim->startAnim("SwimHipDropPunch");
                if (anim && anim->isAnim("SwimHipDropLand") && !anim->isAnim("SwimHipDropPunchLand")) anim->startAnim("SwimHipDropPunchLand");

                if (anim && anim->isAnim("LandStiffen") && !anim->isAnim("LandSuper")) anim->startAnim("LandSuper");
                if (anim && anim->isAnim("MofumofuDemoOpening2") && !anim->isAnim("MofumofuDemoOpening2Super")) anim->startAnim("MofumofuDemoOpening2Super");
            }

            #ifdef ALLOW_TAUNT // Handle Taunt actions
                if (!thisPtr->mInput->isMove()
                    && (al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait))
                    || al::isNerve(thisPtr, getNerveAt(nrvHakoniwaSquat)))
                    && !al::isNerve(thisPtr, &TauntLeftNrv)
                    && !al::isNerve(thisPtr, &TauntRightNrv)
                    && !isFireThrowing()
                ) {
                    if (al::isPadTriggerLeft(-1)
                    ) {
                        al::setNerve(thisPtr, &TauntLeftNrv);
                        return;
                    }
                    if (al::isPadTriggerRight(-1)
                    ) {
                        tauntRightAlt = al::isPadHoldZR(-1) || al::isPadTriggerZR(-1) || al::isPadHoldZL(-1) || al::isPadTriggerZL(-1);
                        al::setNerve(thisPtr, &TauntRightNrv);
                        return;
                    }
                }
                if (al::isNerve(thisPtr, &TauntLeftNrv)
                ) {
                    if (anim->isAnim("WearEnd")
                        || anim->isAnim("WearEndBrawl")
                        || anim->isAnim("WearEndSuper")
                    ) {
                        al::tryStopSe(thisPtr, "WearEnd", -1, nullptr);
                        al::tryStopSe(thisPtr, "WearEndSetCostume", -1, nullptr);
                    }
                }
                if (!al::isNerve(thisPtr, &TauntLeftNrv)
                    && !al::isNerve(thisPtr, &TauntRightNrv)) al::tryDeleteEffect(model, "BonfireSuper");
            #endif
        }
    };

    inline void Install() {
        // Initialize player actor
        PlayerActorHakoniwaInitPlayer::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");

        // Change Mario's idle
        PlayerStateWaitExeWait::InstallAtSymbol("_ZN15PlayerStateWait7exeWaitEv");

        // Handles control/movement
        //PlayerControlHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa7controlEv");
        PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
    }
}