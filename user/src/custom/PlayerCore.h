#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"
#include "custom/_Nerves.h"
#include "custom/PowerUps.h"
#include "custom/PlayerFreeze.h"

namespace PlayerCore {

    struct PlayerActorHakoniwaInitPlayer : public mallow::hook::Trampoline<PlayerActorHakoniwaInitPlayer> {
        static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
            isHakoniwa = nullptr;
            isKoopa = nullptr;
            isNearTarget = nullptr;

            Orig(thisPtr, actorInfo, playerInfo);

            // Set Hakoniwa pointer
            isHakoniwa = thisPtr;

            // Check for Super suit costume and cap
            const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
            const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);

            #ifdef ALLOW_POWERUPS
                #ifdef ALLOW_MARIO
                    isMario = (costume && al::isEqualString(costume, "Mario"))
                        && (cap && al::isEqualString(cap, "Mario"));
                #endif

                isNoCap = (cap && al::isEqualString(cap, "MarioNoCap"));
                isFeather = (costume && al::isEqualString(costume, "MarioFeather"));
                isFire = (costume && al::isEqualString(costume, "MarioColorFire"))
                    && (cap && al::isEqualString(cap, "MarioColorFire"));
                isIce = (costume && al::isEqualString(costume, "MarioColorIce"))
                    && (cap && al::isEqualString(cap, "MarioColorIce"));
                isTanooki = (costume && al::isEqualString(costume, "MarioTanooki"))
                    && (cap && al::isEqualString(cap, "MarioTanooki"));
                isDrill = (costume && al::isEqualString(costume, "MarioDrill"))
                    && (cap && al::isEqualString(cap, "MarioDrill"));
                isMetal = (costume && al::isEqualString(costume, "MarioColorMetal"))
                    && (cap && al::isEqualString(cap, "MarioColorMetal"));
                isFly = (costume && al::isEqualString(costume, "MarioColorFly"))
                    && (cap && al::isEqualString(cap, "MarioColorFly"));
                isBrawl = (costume && al::isEqualString(costume, "MarioColorBrawl"))
                    && (cap && al::isEqualString(cap, "MarioColorBrawl"));
                isSuper = (costume && al::isEqualString(costume, "MarioColorSuper"))
                    && (cap && al::isEqualString(cap, "MarioColorSuper"));

                // Set Cap sounds
                if (isMetal && thisPtr->mHackCap) al::setSeKeeperPlayNamePrefix(thisPtr->mHackCap, "Iron");

                PowerUps::executeInitPlayer(thisPtr, actorInfo, playerInfo);
            #endif
        }
    };

    struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            Orig(thisPtr);

            auto* holder = thisPtr->mModelHolder;
            auto* model  = holder->findModelActor("Normal");
            al::LiveActor* face = al::tryGetSubActor(model, "顔");
            
            #ifdef ALLOW_POWERUPS
                PowerUps::executeMovement(thisPtr);
            #endif

            // Spin-type sensors all attack wall and ceiling contacts
            const char* attackSensorNames[] = {"GalaxySpin", "DoubleSpin", "Punch"};
            al::HitSensor* attackSensors[3] = {
                al::getHitSensor(thisPtr, attackSensorNames[0]),
                al::getHitSensor(thisPtr, attackSensorNames[1]),
                al::getHitSensor(thisPtr, attackSensorNames[2]),
            };
            for (auto* sensor : attackSensors) {
                if (sensor && sensor->mIsValid) {
                    thisPtr->attackSensor(sensor, rs::tryGetCollidedCeilingSensor(thisPtr->mCollider));
                    thisPtr->attackSensor(sensor, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
                }
            }

            al::HitSensor* sensorHipDrop = al::getHitSensor(thisPtr, "HipDropKnockDown");
            if (sensorHipDrop && sensorHipDrop->mIsValid) thisPtr->attackSensor(sensorHipDrop, rs::tryGetCollidedGroundSensor(thisPtr->mCollider));

            // Handle sensor invalidation after timer expires
            if (attackSensorRemaining > 0) {
                attackSensorRemaining--;

                bool animEnded = thisPtr->mAnimator->isAnimEnd();
                if (attackSensorRemaining == 0 || animEnded
                ) {
                    for (const char* name : attackSensorNames) al::invalidateHitSensor(thisPtr, name);
                    spin.isGalaxy = false;
                    attackSensorRemaining = -1;
                }
            }

            // Handle wall bounce for attacks
            al::HitSensor* activeSensor = nullptr;
            for (auto* sensor : attackSensors) {
                if (sensor && sensor->mIsValid) { activeSensor = sensor; break; }
            }
            // Handle wall bounce for hammer
            if (!activeSensor && isHammer && al::isAlive(isHammer)) {
                al::HitSensor* sensorHack = al::getHitSensor(isHammer, "AttackHack");
                if (sensorHack && sensorHack->mIsValid) activeSensor = sensorHack;
            }

            static int attackFrames = 0;
            if (activeSensor) attackFrames++;
            else attackFrames = 0;

            bool isHammerActive = isHammer && al::isAlive(isHammer);
            bool isHammerWall = isHammerActive && al::isCollidedWall(isHammer);
            bool wallHit = rs::isCollidedWall(thisPtr->mCollider) || isHammerWall;

            if (activeSensor && attackFrames >= 2 
                && wallHit && hitBufferCount == 0 
                && !isDrillAnim(thisPtr->mAnimator)
            ) {
                sead::Vector3f wallPos = isHammerWall ? al::getCollidedWallPos(isHammer)    : rs::getCollidedWallPos(thisPtr->mCollider);
                sead::Vector3f wallNormal = isHammerWall ? al::getCollidedWallNormal(isHammer) : rs::getCollidedWallNormal(thisPtr->mCollider);
                al::tryNormalizeOrZero(&wallNormal);

                al::tryEmitEffect(thisPtr, "HitSmall", &wallPos);
                al::tryStartSe(thisPtr, "HitImpact");
                if (isHammerActive) { al::tryEmitEffect(isHammer, "Break", &wallPos); al::tryStartSe(isHammer, "Hit"); }
                al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                al::setVelocity(thisPtr, wallNormal * (thisPtr->mInput->isMove() ? 15.0f : 5.0f) - al::getGravity(thisPtr) * 10.0f);
                attackFrames = 0;
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

            // Add attack to hipdrop
            static bool wasAttackMove = false;
            updateAttackSensor(thisPtr, "HipDropKnockDown", isHipDropAnim(thisPtr->mAnimator), wasAttackMove);

            // Change face animations
            if ((thisPtr->mAnimator->isAnim("BattleWait") || isBrawl || isSuper)
                && face && !al::isActionPlayingSubActor(model, "顔", "WaitAngry")) al::startActionSubActor(model, "顔", "WaitAngry");

            if (isMetal && face
                && !al::isActionPlayingSubActor(model, "顔", "AreaWaitFight")) al::startActionSubActor(model, "顔", "AreaWaitFight");

            #ifdef ALLOW_TAUNT // Handle Taunt actions
                if (!thisPtr->mInput->isMove()
                    && (al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait))
                    || al::isNerve(thisPtr, getNerveAt(nrvHakoniwaSquat)))
                    && !al::isNerve(thisPtr, &TauntLeftNrv)
                    && !al::isNerve(thisPtr, &TauntRightNrv)
                    && !isActionBusy()
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
                    if (thisPtr->mAnimator->isAnim("WearEnd")
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

    struct PlayerActorHakoniwaReceiveMsgHook : public mallow::hook::Trampoline<PlayerActorHakoniwaReceiveMsgHook> {
        static bool Callback(PlayerActorHakoniwa* thisPtr, const al::SensorMsg* msg, al::HitSensor* source, al::HitSensor* target) {
            if (drillStep != WallStick::Idle || drillSensorRemaining > 0) return false;

            if (PlayerFreeze::handleReceiveMsg(msg, source)) return false;

            bool isDamage = rs::isMsgPlayerDamage(msg)
                || al::isMsgHit(msg)
                || al::isMsgHitStrong(msg)
                || al::isMsgHitVeryStrong(msg)
                || rs::isMsgPlayerDamageBlowDown(msg)
                || al::isMsgExplosion(msg);

            if (thisPtr && isDamage
            ) {
                auto* anim = thisPtr->mAnimator;
                const float frame = anim->getAnimFrame();

                if ((al::isEqualSubString(anim->mCurAnim, "CapPunch")  && frame <= 7.0f)
                    || (al::isEqualSubString(anim->mCurAnim, "JumpPunch") && frame <= 17.0f)) return false;
                if (source && al::isEqualString(al::getSensorHost(source)->getName(), "MarioTankBullet")) return false;
                if (isHipDropAnim(anim) || isMetal || isSuper) return false;
            }
            return Orig(thisPtr, msg, source, target);
        }
    };

    // Protect sub-anims from being killed by game code
    struct EndSubAnimGuard : public mallow::hook::Trampoline<EndSubAnimGuard> {
        static void Callback(PlayerAnimator* anim) {
            if (isDrillAnim(anim) && !anim->isSubAnimEnd()) return;

            Orig(anim);
        }
    };

    struct TryEmitEffectHook : public mallow::hook::Trampoline<TryEmitEffectHook> {
        static bool Callback(al::EffectKeeper* keeper, const char* name, const sead::Vector3f* pos) {
            if (isConfig()->galaxySfx
                && al::isEqualString(name, "SpinCapStart2Right")
                && isHakoniwa && al::isEqualSubString(isHakoniwa->mAnimator->mCurAnim, "SpinSeparate")) return false;

            return Orig(keeper, name, pos);
        }
    };

    inline void Install() {
        // Initialize player actor
        PlayerActorHakoniwaInitPlayer::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");

        // Handles control/movement
        //PlayerControlHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa7controlEv");
        PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
        PlayerActorHakoniwaReceiveMsgHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa10receiveMsgEPKN2al9SensorMsgEPNS0_9HitSensorES5_");
        EndSubAnimGuard::InstallAtSymbol("_ZN14PlayerAnimator10endSubAnimEv");
        TryEmitEffectHook::InstallAtSymbol("_ZN2al13tryEmitEffectEPNS_16IUseEffectKeeperEPKcPKN4sead7Vector3IfEE");
    }
}