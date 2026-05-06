#pragma once
#include "custom/_Globals.h"
#include "custom/_Nerves.h"
#include "custom/PlayerFreeze.h"
#include "headers/PlayerIceCube.h"

// Guard Mario against attacks
inline bool isValidAttackTarget(al::HitSensor* target) {
    al::LiveActor* targetHost = al::getSensorHost(target);
    return targetHost && targetHost != isHakoniwa;
}

namespace AttackSensor {

    struct HackCapAttackSensorHook : public mallow::hook::Trampoline<HackCapAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;
            if (!isValidAttackTarget(target)) return;

            al::LiveActor* targetHost = al::getSensorHost(target);

            if (al::isEqualSubString(typeid(*targetHost).name(), "KoopaCap")
                && al::isModelName(targetHost, "KoopaCap")) return;

            Orig(thisPtr, source, target);
        }
    };

    struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;
            if (!isValidAttackTarget(target)) return;

            al::LiveActor* targetHost = al::getSensorHost(target);

            if (al::isEqualSubString(typeid(*targetHost).name(), "KoopaCap")
                && al::isModelName(targetHost, "KoopaCap")) return;
            
            if (!al::isSensorName(source, "GalaxySpin")
                && !al::isSensorName(source, "DoubleSpin")
                && !al::isSensorName(source, "Punch")
                && !al::isSensorName(source, "HipDropKnockDown")
            ) {
                Orig(thisPtr, source, target);
                return;
            }

            sead::Vector3f spawnPos = getHitSpawnPos(source, target);
            sead::Vector3f fireDir = getFireDir(thisPtr, targetHost);

            if (al::isEqualSubString(typeid(*targetHost).name(), "FireBall")
                && al::calcSpeedH(thisPtr) >= thisPtr->mConst->getDashFastBorderSpeed()) return;

            bool isSpinAttack = al::isSensorName(source, "GalaxySpin")
                && (isBaseSpinAnim(thisPtr->mAnimator)
                    || isJumpPunchAnim(thisPtr->mAnimator)
                    || isDrillAnim(thisPtr->mAnimator) // Allow drill attacks
                    || al::isActionPlaying(thisPtr->mModelHolder->findModelActor("Normal"), "MoveSuper")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "JumpBroad8") || al::isEqualString(thisPtr->mAnimator->mCurAnim, "Glide"));

            bool isDoubleSpinAttack = al::isSensorName(source, "DoubleSpin")
                && isDoubleSpinAnim(thisPtr->mAnimator);

            bool isSpinFallback = spin.isGalaxy
                && (al::isSensorName(source, "GalaxySpin") || al::isSensorName(source, "DoubleSpin"));

            bool isPunchAttack = al::isSensorName(source, "Punch")
                && isPunchAnim(thisPtr->mAnimator);

            bool isHipDrop = al::isSensorName(source, "HipDropKnockDown")
                && isHipDropAnim(thisPtr->mAnimator);

            al::HitSensor* foot = al::getHitSensor(thisPtr, "Foot");
            bool canTrample = rs::isEnableSendTrampleMsg(thisPtr, foot, target);
            bool isHipDropAttack = isHipDrop && !canTrample;

            if (isSpinAttack || isDoubleSpinAttack) rs::sendMsgPaint(target, source, paintClear, 150, 0);

            if(isSpinAttack || isDoubleSpinAttack 
                || isPunchAttack || isHipDropAttack
                || isSpinFallback
            ) {
                bool inBuffer = isInHitBuffer(targetHost);

                // Handle ice cubes
                if (al::isEqualSubString(typeid(*targetHost).name(), "PlayerIceCube")
                ) {
                    ((PlayerIceCube*)targetHost)->markHit(source);
                    return;
                }
                if (!targetHost->getNerveKeeper()) return;

                const al::Nerve* sourceNrv = targetHost->getNerveKeeper()->getCurrentNerve();
                inBuffer |= sourceNrv == getNerveAt(0x1D03268); // GrowPlantSeedNrvHold
                inBuffer |= sourceNrv == getNerveAt(0x1D00EC8); // GrowFlowerSeedNrvHold
                inBuffer |= sourceNrv == getNerveAt(0x1D22B78); // RadishNrvHold

                if (isPunchAttack && !isPunchActive
                ) {
                    if (al::isEqualSubString(typeid(*targetHost).name(),"Stake")
                        && sourceNrv == getNerveAt(0x1D36D20)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::setNerve(targetHost, getNerveAt(0x1D36D30));
                        al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                        return;
                    }
                    if (al::isEqualSubString(typeid(*targetHost).name(),"Radish")
                        && sourceNrv == getNerveAt(0x1D22B70)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::setNerve(targetHost, getNerveAt(0x1D22BD8));
                        al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                        return;
                    }
                    if (al::isEqualSubString(typeid(*targetHost).name(),"BossRaidRivet")
                        && sourceNrv == getNerveAt(0x1C5F330)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::invalidateCollisionParts(targetHost);
                        al::setVelocity(targetHost, al::getGravity(targetHost) * -44.0f);
                        al::setNerve(targetHost, getNerveAt(0x1C5F338));
                        al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                        return;
                    }
                    if (al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                        && !al::isModelName(targetHost, "TreasureBoxWood")
                    ) {
                        if (rs::sendMsgCapAttack(target, source)
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            if (!al::isEffectEmitting(thisPtr, "Hit")) al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                            return;
                        }
                    }
                }

                if (isSpinAttack || isDoubleSpinAttack || isSpinFallback
                ) {
                    if (al::isEqualSubString(typeid(*targetHost).name(), "BlockQuestion")
                        || al::isEqualSubString(typeid(*targetHost).name(), "BlockBrick")
                        || al::isEqualSubString(typeid(*targetHost).name(), "BossForestBlock")
                    ) {
                        rs::sendMsgHammerBrosHammerHackAttack(target, source);
                        return;
                    }
                }
                if (inBuffer) { Orig(thisPtr, source, target); return; }

                if (al::isEqualSubString(typeid(*targetHost).name(), "Koopa")
                    && al::isModelName(targetHost, "KoopaBig")
                ) {
                    KoopaBattle::attack(thisPtr, source, target);
                    return;
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "BlockHard")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BossForestBlock")
                    || al::isEqualSubString(typeid(*targetHost).name(), "GolemClimb")
                    || al::isEqualSubString(typeid(*targetHost).name(), "MarchingCubeBlock")
                ) {
                    if (rs::sendMsgHammerBrosHammerHackAttack(target, source)
                    ){
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "BreakMapParts")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BreakableWall")
                    || al::isEqualSubString(typeid(*targetHost).name(), "CatchBomb")
                    || al::isEqualSubString(typeid(*targetHost).name(), "DamageBall")
                    || al::isEqualSubString(typeid(*targetHost).name(), "KickStone")
                    || al::isEqualSubString(typeid(*targetHost).name(), "KoopaDamageBall")
                    || al::isEqualSubString(typeid(*targetHost).name(), "MoonBasement")
                    || al::isEqualSubString(typeid(*targetHost).name(), "PlayGuideBoard")
                    || (al::isEqualSubString(typeid(*targetHost).name(), "SignBoard")
                        && !al::isModelName(targetHost, "SignBoardNormal"))
                    || (al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                        && al::isModelName(targetHost, "TreasureBoxWood"))
                ) {
                    if (al::sendMsgExplosion(target, source, nullptr)
                        || rs::sendMsgStatueDrop(target, source)
                        || rs::sendMsgKoopaCapPunchL(target, source)
                        || rs::sendMsgKoopaHackPunch(target, source)
                        || rs::sendMsgKoopaHackPunchCollide(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "BreedaWanwan")
                    || al::isEqualSubString(typeid(*targetHost).name(), "TRex")
                ) {
                    if (al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || al::sendMsgPlayerHipDrop(target, source, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "CapSwitch")
                ) {
                    al::setNerve(targetHost, getNerveAt(0x1CE3E18));
                    hitBuffer[hitBufferCount++] = targetHost;
                    al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                    return;
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "CapSwitchTimer")
                ) {
                    al::setNerve(targetHost, getNerveAt(0x1CE4338));
                    al::invalidateClipping(targetHost);
                    hitBuffer[hitBufferCount++] = targetHost;
                    al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                    return;
                }
                if ((al::isEqualSubString(typeid(*targetHost).name(), "Car")
                    && (al::isModelName(targetHost, "Car") || al::isModelName(targetHost, "CarBreakable"))
                    && !al::isSensorName(target, "Brake"))
                    || al::isEqualSubString(typeid(*targetHost).name(), "ChurchDoor")
                    || al::isEqualSubString(typeid(*targetHost).name(), "CollapseSandHill")
                    || al::isEqualSubString(typeid(*targetHost).name(), "Doshi")
                    || al::isEqualSubString(typeid(*targetHost).name(), "ReactionObject")
                    || (al::isEqualSubString(typeid(*targetHost).name(), "SignBoard")
                        && al::isModelName(targetHost, "SignBoardNormal"))
                ) {
                    if (rs::sendMsgCapReflect(target, source)
                        || rs::sendMsgCapAttack(target, source)
                        || rs::sendMsgCapAttackCollide(target, source)
                        || rs::sendMsgCapReflectCollide(target, source)
                        || rs::sendMsgCapTouchWall(target, source, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "YoshiFruit")
                ) {
                    if (al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (al::isSensorNpc(target) || al::isSensorRide(target)
                ) {
                    if (al::sendMsgPlayerSpinAttack(target, source, nullptr)
                        || rs::sendMsgCapReflect(target, source)
                        || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || rs::sendMsgCapAttack(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryStartSe(thisPtr, "HitImpact");
                        return;
                    }
                }
                if (al::isSensorEnemyBody(target)
                ) {
                    if (rs::sendMsgHackAttack(target, source)
                        || rs::sendMsgCapReflect(target, source)
                        || rs::sendMsgCapAttack(target, source)
                        || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryStartSe(thisPtr, "HitImpact");
                        return;
                    }
                }
                if (al::isSensorMapObj(target)
                    && !al::isEqualSubString(typeid(*targetHost).name(), "HipDrop")
                    && !al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                ) {
                    bool isHitImpact = false;
                    if (rs::sendMsgHackAttack(target, source)
                        || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                        || rs::sendMsgCapReflect(target, source)
                        || al::sendMsgPlayerHipDrop(target, source, nullptr)
                        || rs::sendMsgCapAttack(target, source)
                        || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || (isHitImpact = rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero))
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        if (!isHitImpact) al::tryStartSe(thisPtr, "HitImpact");
                        return;
                    }
                }
            }
            Orig(thisPtr, source, target);
        }
    };

    struct HammerAttackSensorHook : public mallow::hook::Trampoline<HammerAttackSensorHook> {
        static void Callback(HammerBrosHammer* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            if (!al::isNerve(isHakoniwa, &HammerNrv)) { Orig(thisPtr, source, target); return; }
            
            if (!isValidAttackTarget(target)) return;
            al::LiveActor* targetHost = al::getSensorHost(target);

            sead::Vector3f spawnPos = getHitSpawnPos(source, target);
            sead::Vector3f fireDir = getFireDir(thisPtr, targetHost);

            if(al::isSensorName(source, "AttackHack")
            ) {
                rs::sendMsgPaint(target, source, paintClear, 300, 0);

                if (isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }
                
                if (al::isEqualSubString(typeid(*targetHost).name(), "BlockHard")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BossForestBlock")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BreakMapParts")
                    || al::isEqualSubString(typeid(*targetHost).name(), "CatchBomb")
                    || al::isEqualSubString(typeid(*targetHost).name(), "DamageBall")
                    || al::isEqualSubString(typeid(*targetHost).name(), "FrailBox")
                    || al::isEqualSubString(typeid(*targetHost).name(), "KoopaDamageBall")
                    || al::isEqualSubString(typeid(*targetHost).name(), "MarchingCubeBlock")
                    || al::isEqualSubString(typeid(*targetHost).name(), "MoonBasement")
                    || al::isEqualSubString(typeid(*targetHost).name(), "PlayGuideBoard")
                    || (al::isEqualSubString(typeid(*targetHost).name(), "ReactionObject")
                        && al::isSensorCollision(target))
                    || (al::isEqualSubString(typeid(*targetHost).name(), "SignBoard")
                        && !al::isModelName(targetHost, "SignBoardNormal"))
                    || al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                ) {
                    if (al::sendMsgExplosion(target, source, nullptr)
                        || rs::sendMsgStatueDrop(target, source)
                        || rs::sendMsgKoopaCapPunchL(target, source)
                        || rs::sendMsgKoopaHackPunch(target, source)
                        || rs::sendMsgKoopaHackPunchCollide(target, source)
                        || rs::sendMsgCapAttack(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        if (!al::isEqualSubString(typeid(*targetHost).name(), "BossForestBlock")) al::tryEmitEffect(thisPtr, "HammerHit", &spawnPos);
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "Car")
                    && (al::isModelName(targetHost, "Car") || al::isModelName(targetHost, "CarBreakable"))
                    && !al::isSensorName(target,"Brake")
                ) {
                    if (rs::sendMsgPlayerTouchFloorJumpCode(target, source)
                        || al::sendMsgExplosion(target, source, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(thisPtr, "HammerHit", &spawnPos);
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "CollapseSandHill")
                    || al::isEqualSubString(typeid(*targetHost).name(), "Doshi")
                    || (al::isEqualSubString(typeid(*targetHost).name(), "SignBoard")
                        && al::isModelName(targetHost, "SignBoardNormal"))
                ) {
                    if (rs::sendMsgCapAttack(target, source)
                        || rs::sendMsgCapAttackCollide(target, source)
                        || rs::sendMsgCapReflectCollide(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "Koopa")
                    && al::isModelName(targetHost, "KoopaBig")
                ) {
                    if (rs::sendMsgKoopaCapPunchFinishL(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(thisPtr, "KoopaFinishHit", &spawnPos);
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "TRex")
                ) {
                    if (al::sendMsgPlayerHipDrop(target, source, nullptr)
                        || rs::sendMsgSeedAttackBig(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(thisPtr, "HammerHit", &spawnPos);
                        return;
                    }
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "Wanwan")
                ) {
                    if (rs::sendMsgWanwanReboundAttack(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (rs::sendMsgTRexAttack(target, source)
                    || al::sendMsgPlayerHipDrop(target, source, nullptr)
                    || al::sendMsgPlayerObjHipDrop(target, source, nullptr)
                    || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                    || rs::sendMsgPlayerHipDropHipDropSwitch(target, source)
                    || rs::sendMsgHackAttack(target, source)
                    || rs::sendMsgSphinxRideAttackTouchThrough(target, source, fireDir, fireDir)
                    || rs::sendMsgCapReflect(target, source)
                    || (!al::isEqualSubString(typeid(*targetHost).name(),"Souvenir")
                        && rs::sendMsgCapAttack(target, source))
                    || (!al::isEqualSubString(typeid(*targetHost).name(),"ReactionObject")
                        && rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true))
                    || al::sendMsgExplosion(target, source, nullptr)
                ) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    return;
                }
            }
            Orig(thisPtr, source, target);
        }
    };

    struct FireballAttackSensorHook : public mallow::hook::Trampoline<FireballAttackSensorHook> {
        static void Callback(FireBrosFireBall* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            bool isIceball = al::isEqualString(thisPtr->getName(), "MarioIceBall");
            if (!isIceball) { Orig(thisPtr, source, target); return; }

            if (!al::isSensorName(source, "AttackHack")) { Orig(thisPtr, source, target); return; }

            if (!isValidAttackTarget(target)) return;
            al::LiveActor* targetHost = al::getSensorHost(target);
            if (al::isEqualString(targetHost->getName(), "MarioIceBall")) return;

            sead::Vector3f sourcePos = al::getSensorPos(source);

            if (al::isEqualSubString(typeid(*targetHost).name(), "PlayerIceCube")
            ) {
                ((PlayerIceCube*)targetHost)->markHit(source);
                al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                thisPtr->kill();
                return;
            }

            if (isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }

            if (al::isEqualSubString(typeid(*targetHost).name(), "FireSwitch")
                || al::isEqualSubString(typeid(*targetHost).name(), "Candlestand")
            ) {
                if (rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)) {
                    al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                    thisPtr->kill();
                }
                return;
            }
            if ((al::isSensorEnemyBody(target) || al::isEqualSubString(typeid(*targetHost).name(), "Rabbit"))
                && !al::isHideModel(targetHost)
                && !al::isEqualSubString(typeid(*targetHost).name(), "Boss") && !al::isEqualSubString(typeid(*targetHost).name(), "Koopa")
                && !(al::isEqualSubString(typeid(*targetHost).name(), "Stacker") && al::isNoCollide(targetHost))
            ) {
                hitBuffer[hitBufferCount++] = targetHost;
                PlayerFreeze::freezeActor(targetHost, 1800);
                al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                thisPtr->kill();
                return;
            }

            Orig(thisPtr, source, target);
        }
    };

    struct FireballAttackSensorInline : public mallow::hook::Inline<FireballAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* thisPtr = reinterpret_cast<FireBrosFireBall*>(ctx->X[19]);
            auto* source  = reinterpret_cast<al::HitSensor*>(ctx->X[20]);
            auto* target  = reinterpret_cast<al::HitSensor*>(ctx->X[21]);

            bool isFireball = al::isEqualString(thisPtr->getName(), "MarioFireBall");
            bool isIceball = al::isEqualString(thisPtr->getName(), "MarioIceBall");
            if (!isFireball && !isIceball) return;
            if (!isValidAttackTarget(target)) return;

            sead::Vector3f spawnPos = getHitSpawnPos(source, target);

            if (!ctx->W[0]) {
                if (al::sendMsgExplosion(target, source, nullptr)
                    || al::sendMsgKickStoneAttackReflect(target, source)
                ) {
                    ctx->W[0] = true;
                    if (!al::isEffectEmitting(thisPtr, "Hit")) al::tryEmitEffect(isHakoniwa, "Hit", &spawnPos);
                }
            }
        }
    };

    struct MotorcycleAttackSensorInline : public mallow::hook::Inline<MotorcycleAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[19]);
            auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[20]);

            if (!isValidAttackTarget(target)) return;

            rs::sendMsgSphinxRideAttack(target, source)
            || rs::sendMsgSphinxRideAttackReflect(target, source)
            || rs::sendMsgHackAttack(target, source);
        }
    };

    struct TankBulletAttackSensorInline : public mallow::hook::Inline<TankBulletAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* bullet = reinterpret_cast<TankBullet*>(ctx->X[19]);
            if (!al::isEqualString(bullet->getName(), "MarioTankBullet")) return;

            auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[22]);
            auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[21]);

            if (!isValidAttackTarget(target)) return;

            #ifndef ALLOW_CAPPY_ONLY
                al::LiveActor* targetHost = al::getSensorHost(target);
                if (al::isEqualSubString(typeid(*targetHost).name(), "KoopaCap")
                    && al::isModelName(targetHost, "KoopaCap")) return;
            #endif
            
            rs::sendMsgSeedAttackBig(target, source);

            ctx->W[0] = ctx->W[0]
                || al::sendMsgPlayerFireBallAttack(target, source)
                || rs::sendMsgCapAttack(target, source)
                || al::sendMsgKickStoneAttackReflect(target, source);

            rs::sendMsgWeaponItemGet(target, source);
        }
    };

    inline void Install() {
        #ifndef ALLOW_CAPPY_ONLY
            HackCapAttackSensorHook::InstallAtSymbol("_ZN7HackCap12attackSensorEPN2al9HitSensorES2_");
            PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");
        #endif
        
        HammerAttackSensorHook::InstallAtSymbol("_ZN16HammerBrosHammer12attackSensorEPN2al9HitSensorES2_");
        FireballAttackSensorInline::InstallAtOffset(0x100E70);
        FireballAttackSensorHook::InstallAtSymbol("_ZN16FireBrosFireBall12attackSensorEPN2al9HitSensorES2_");
        MotorcycleAttackSensorInline::InstallAtOffset(0x2C77EC);
        TankBulletAttackSensorInline::InstallAtOffset(0x189C7C);
    }
}