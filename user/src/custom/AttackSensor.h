#pragma once
#include "custom/_Globals.h"
#include "custom/_Nerves.h"

namespace AttackSensor {

    struct HackCapAttackSensorHook : public mallow::hook::Trampoline<HackCapAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {

            if (!thisPtr || !source || !target) return;

            al::LiveActor* sourceHost = al::getSensorHost(source);
            al::LiveActor* targetHost = al::getSensorHost(target);

            if (!sourceHost || !targetHost) return;

            if (al::isEqualSubString(typeid(*targetHost).name(), "KoopaCap")
                && al::isModelName(targetHost, "KoopaCap")) return;

            Orig(thisPtr, source, target);
        }
    };

    struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {

            if (!thisPtr || !source || !target) return;

            al::LiveActor* sourceHost = al::getSensorHost(source);
            al::LiveActor* targetHost = al::getSensorHost(target);

            if (!sourceHost || !targetHost) return;

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

            sead::Vector3f sourcePos = al::getSensorPos(source);
            sead::Vector3f targetPos = al::getSensorPos(target);
            sead::Vector3f spawnPos = (sourcePos + targetPos) * 0.5f;
            spawnPos.y += 20.0f;

            sead::Vector3 fireDir = al::getTrans(targetHost) - al::getTrans(sourceHost);
            fireDir.normalize();
    
            if (al::isActionPlaying(thisPtr->mModelHolder->findModelActor("Normal"), "MoveSuper")
                && al::isEqualSubString(typeid(*targetHost).name(), "FireBall")) return;

            bool isSpinAttack = al::isSensorName(source, "GalaxySpin") && thisPtr->mAnimator
                    && (al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinSeparate")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinSeparateSwim")
                        || al::isActionPlaying(thisPtr->mModelHolder->findModelActor("Normal"), "MoveSuper")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "JumpBroad8")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "Glide")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "CapeAttack")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "TailAttack"));

            bool isDoubleSpinAttack = al::isSensorName(source, "DoubleSpin") && thisPtr->mAnimator
                    && (al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackLeft")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackRight")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackAirLeft")
                        || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackAirRight"));

            bool isSpinFallback = isGalaxySpin
                && (al::isSensorName(source, "GalaxySpin") || al::isSensorName(source, "DoubleSpin"));

            bool isPunchAttack = al::isSensorName(source, "Punch") && thisPtr->mAnimator
                && (al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchL")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchR")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchFinishL")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchFinishR")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "RabbitGet")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "Kick"));

            bool isHipDrop = al::isSensorName(source, "HipDropKnockDown") && thisPtr->mAnimator
                && (al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDrop")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDropPunch")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDropReaction")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDropPunchReaction")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinJumpDownFallL")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinJumpDownFallR")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SwimHipDrop")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SwimHipDropPunch")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SwimDive"));

            bool isPhysicalContact = al::calcDistance(source, target) < 130.0f;
            bool isHipDropAttack = isHipDrop && ((al::isSensorEnemyBody(target) && isPhysicalContact) || rs::isCollidedGround(thisPtr->mCollider));

            if(isSpinAttack || isDoubleSpinAttack 
                || isPunchAttack || isHipDropAttack
                || isSpinFallback
            ) {
                bool isInHitBuffer = false;
                for(int i = 0; i < hitBufferCount; i++) {
                    if(hitBuffer[i] == targetHost) {
                        isInHitBuffer = true;
                        break;
                    }
                }
                if (!targetHost->getNerveKeeper()) return;

                if(targetHost && targetHost->getNerveKeeper()
                ) {
                    const al::Nerve* sourceNrv = targetHost->getNerveKeeper()->getCurrentNerve();
                    isInHitBuffer |= sourceNrv == getNerveAt(0x1D03268); // GrowPlantSeedNrvHold
                    isInHitBuffer |= sourceNrv == getNerveAt(0x1D00EC8); // GrowFlowerSeedNrvHold
                    isInHitBuffer |= sourceNrv == getNerveAt(0x1D22B78); // RadishNrvHold

                    if (isPunchAttack && !isPunching
                    ) {
                        if (al::isEqualSubString(typeid(*targetHost).name(),"Stake")
                            && sourceNrv == getNerveAt(0x1D36D20)
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            al::setNerve(targetHost, getNerveAt(0x1D36D30));
                            al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
                            return;
                        }
                        if (al::isEqualSubString(typeid(*targetHost).name(),"Radish")
                            && sourceNrv == getNerveAt(0x1D22B70)
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            al::setNerve(targetHost, getNerveAt(0x1D22BD8));
                            al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
                            return;
                        }
                        if (al::isEqualSubString(typeid(*targetHost).name(),"BossRaidRivet")
                            && sourceNrv == getNerveAt(0x1C5F330)
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            al::setNerve(targetHost, getNerveAt(0x1C5F338));
                            al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
                            return;
                        }
                        if (al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                            && !al::isModelName(targetHost, "TreasureBoxWood")
                        ) {
                            if (al::sendMsgExplosion(target, source, nullptr)
                            ) {
                                hitBuffer[hitBufferCount++] = targetHost;
                                al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
                                return;
                            }
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
                if (al::isEqualSubString(typeid(*targetHost).name(), "Koopa")
                    && al::isModelName(targetHost, "KoopaBig")
                ) {
                    const char* koopaAct = al::getActionName(targetHost);

                    if (koopaAct && ((al::isEqualSubString(koopaAct, "AttackTail")
                        && !al::isEqualSubString(koopaAct, "After") && !al::isEqualSubString(koopaAct, "End"))
                        || al::isEqualSubString(koopaAct, "DownLand") || al::isEqualSubString(koopaAct, "Jump"))) return;

                    isKoopa = targetHost;

                    static int guardCount = 0;
                    static bool wasGuard = false;
                    bool startGuard = al::isActionPlaying(targetHost, "Guard1");
                    bool isGuard = al::isActionPlaying(targetHost, "Guard5");

                    if (isGuard && !wasGuard) guardCount++;
                    wasGuard = isGuard;

                    if (startGuard) {
                        wasGuard = false;
                        guardCount = 0;
                        return;
                    }
                    if (isGuard && guardCount == 4) {
                        isFinalPunch = true;
                        return;
                    }
                    if (isGuard && guardCount >= 5) {
                        rs::sendMsgKoopaCapPunchFinishL(target, source);
                        guardCount = 0;
                        return;
                    }
                    if (!isInHitBuffer) {
                        bool isKnockback = rs::sendMsgKoopaCapPunchKnockBackL(target, source);
                        if (isKnockback || rs::sendMsgKoopaCapPunchL(target, source)
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            if (isKnockback) al::tryStartSe(thisPtr, "DamageHit");
                            if (!al::isEffectEmitting(targetHost, "Guard")) al::tryEmitEffect(sourceHost, "KoopaHit", &spawnPos);
                            return;
                        }
                    }
                }
                if(!isInHitBuffer
                ) {
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
                            al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
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
                        al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
                        return;
                    }
                    if (al::isEqualSubString(typeid(*targetHost).name(), "CapSwitchTimer")
                    ) {
                        al::setNerve(targetHost, getNerveAt(0x1CE4338));
                        al::invalidateClipping(targetHost);
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
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
                            al::tryStartSe(thisPtr, "BlowHit");
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
                            al::tryStartSe(thisPtr, "BlowHit");
                            return;
                        }
                    }
                    if (al::isSensorMapObj(target)
                        && !al::isEqualSubString(typeid(*targetHost).name(), "HipDrop")
                        && !al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                    ) {
                        bool isBlowHit = false;
                        if (rs::sendMsgHackAttack(target, source)
                            || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                            || rs::sendMsgCapReflect(target, source)
                            || al::sendMsgPlayerHipDrop(target, source, nullptr)
                            || rs::sendMsgCapAttack(target, source)
                            || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                            || (isBlowHit = rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero))
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            if (!isBlowHit) al::tryStartSe(thisPtr, "BlowHit");
                            return;
                        }
                    }
                }
            }
            Orig(thisPtr, source, target);
        }
    };

    struct HammerAttackSensorHook : public mallow::hook::Trampoline<HammerAttackSensorHook> {
        static void Callback(HammerBrosHammer* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            if (!al::isNerve(isHakoniwa, &HammerNrv)
            ) {
                Orig(thisPtr, source, target);
                return;
            }
            
            al::LiveActor* sourceHost = al::getSensorHost(source);
            al::LiveActor* targetHost = al::getSensorHost(target);
            
            if (!sourceHost || !targetHost) return;
            if (targetHost == isHakoniwa) return;

            sead::Vector3f sourcePos = al::getSensorPos(source);
            sead::Vector3f targetPos = al::getSensorPos(target);
            sead::Vector3f spawnPos = (sourcePos + targetPos) * 0.5f;
            spawnPos.y += 20.0f;
            
            sead::Vector3 fireDir = al::getTrans(targetHost) - al::getTrans(sourceHost);
            fireDir.normalize();

            if(al::isSensorName(source, "AttackHack")
            ) {
                bool isInHitBuffer = false;
                for(int i = 0; i < hitBufferCount; i++) {
                    if(hitBuffer[i] == targetHost) {
                        isInHitBuffer = true;
                        break;
                    }
                }
                if(!isInHitBuffer
                ) {
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
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            if (!al::isEqualSubString(typeid(*targetHost).name(), "BossForestBlock")) al::tryEmitEffect(sourceHost, "HammerHit", &spawnPos);
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
                            al::tryEmitEffect(sourceHost, "HammerHit", &spawnPos);
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
                            al::tryEmitEffect(sourceHost, "KoopaFinishHit", &spawnPos);
                            return;
                        }
                    }
                    if (al::isEqualSubString(typeid(*targetHost).name(), "TRex")
                    ) {
                        if (al::sendMsgPlayerHipDrop(target, source, nullptr)
                            || rs::sendMsgSeedAttackBig(target, source)
                        ) {
                            hitBuffer[hitBufferCount++] = targetHost;
                            al::tryEmitEffect(sourceHost, "HammerHit", &spawnPos);
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
            }
            Orig(thisPtr, source, target);
        }
    };
    
    struct FireballAttackSensorHook : public mallow::hook::Trampoline<FireballAttackSensorHook> {
        static void Callback(FireBrosFireBall* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            const char* name = thisPtr->getName();
            bool isFireball = al::isEqualString(name, "MarioFireBall");
            bool isIceball  = al::isEqualString(name, "MarioIceBall");

            if (!isIceball) Orig(thisPtr, source, target);
            if (!isFireball && !isIceball) return;
            
            al::LiveActor* sourceHost = al::getSensorHost(source);
            al::LiveActor* targetHost = al::getSensorHost(target);

            if (!sourceHost || !targetHost) return;
            if (targetHost == isHakoniwa) return;

            sead::Vector3f sourcePos = al::getSensorPos(source);
            sead::Vector3f targetPos = al::getSensorPos(target);
            sead::Vector3f spawnPos = (sourcePos + targetPos) * 0.5f;
            spawnPos.y += 20.0f;

            if(al::isSensorName(source, "AttackHack")
            ) {
                bool isInHitBuffer = false;
                for(int i = 0; i < hitBufferCount; i++) {
                    if(hitBuffer[i] == targetHost) {
                        isInHitBuffer = true;
                        break;
                    }
                }
                if(!isInHitBuffer
                ) {
                    if (rs::sendMsgHackAttack(target, source)
                        || al::sendMsgExplosion(target, source, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        if (!al::isEffectEmitting(sourceHost, "Hit")) al::tryEmitEffect(isHakoniwa, "Hit", &spawnPos);
                        return;
                    }
                }
            }
        }
    };
    
    inline void Install() {
        #ifndef ALLOW_CAPPY_ONLY
            HackCapAttackSensorHook::InstallAtSymbol("_ZN7HackCap12attackSensorEPN2al9HitSensorES2_");
            PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");
        #endif
        
        HammerAttackSensorHook::InstallAtSymbol("_ZN16HammerBrosHammer12attackSensorEPN2al9HitSensorES2_");
        FireballAttackSensorHook::InstallAtSymbol("_ZN16FireBrosFireBall12attackSensorEPN2al9HitSensorES2_");
    }
}