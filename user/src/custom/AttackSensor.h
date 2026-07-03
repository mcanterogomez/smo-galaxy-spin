#pragma once
#include "custom/_Globals.h"
#include "custom/_Nerves.h"
#include "custom/PlayerFreeze.h"
#include "headers/PlayerIceCube.h"

template<typename... Models>
inline bool isType(al::LiveActor* actor, const char* name, Models... models) {
    if (!al::isEqualSubString(typeid(*actor).name(), name)) return false;
    if constexpr (sizeof...(models) == 0) return true;
    else return ((models[0] == '!' ? !al::isModelName(actor, models + 1) : al::isModelName(actor, models)) || ...);
}

template<typename... Names>
inline bool isAnyType(al::LiveActor* actor, Names... names) {
    const char* type = typeid(*actor).name();
    return (al::isEqualSubString(type, names) || ...);
}

// Guard Mario against attacks
inline bool isValidAttackTarget(al::HitSensor* target) {
    al::LiveActor* targetHost = al::getSensorHost(target);
    return targetHost && targetHost != isHakoniwa;
}

// Handle stacked enemies
inline bool handleStacked(al::LiveActor*& actor, al::HitSensor* target, al::HitSensor* source) {
	if (!actor->getNerveKeeper()) return false;
	if (isType(actor, "KuriboHack")) {
		rs::sendMsgYoshiTongueEatBind(target, source, nullptr, nullptr, nullptr);
		al::setNerve(actor, getNerveAt(0x1C9D888));
		return true;
	}
	if (isType(actor, "StackerCap")) {
        if (!al::isNerve(actor, getNerveAt(0x1C7B7F0)) && !al::isNerve(actor, getNerveAt(0x1C7B7F8))) return false; // OnHead, OnHeadAttack
        al::LiveActor* host = *reinterpret_cast<al::LiveActor**>((char*)actor + 0x108);
        if (!host || !al::isAlive(host)) return false;
        int count = *reinterpret_cast<int*>((char*)host + 0x154);
        if (count <= 0) return false;
        actor = (*reinterpret_cast<al::LiveActor***>(*reinterpret_cast<char**>((char*)host + 0x108) + 0x18))[count - 1]; // topCap
        using BlowFn = void(*)(al::LiveActor*, const sead::Vector3f&, const sead::Vector3f&);
        reinterpret_cast<BlowFn>(reinterpret_cast<uintptr_t>(getNerveAt(0)) + 0xBCC3C)(host, al::getSensorPos(source), sead::Vector3f::zero); // Stacker::blowCapOnHead
        return true;
    }
	return false;
}

// Handle swooning enemies
inline bool trySwoon(al::LiveActor* actor) {
	if (isType(actor, "FireBros") || isType(actor, "HammerBros")) { al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x130), getNerveAt(0x1C85DD0)); return true; }
	if (isType(actor, "Imomu")) { al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x118), getNerveAt(0x1C94EB0)); return true; }
	if (isType(actor, "Senobi")) { al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x128), getNerveAt(0x1CA83A8)); return true; }
	if (isType(actor, "BreedaWanwan")) { al::setNerve(actor, getNerveAt(0x1C621D0)); return true; }
	if (isType(actor, "Bull")) { al::setNerve(actor, getNerveAt(0x1C87D18)); return true; }
	if (isType(actor, "Byugo")) { al::setNerve(actor, getNerveAt(0x1C887B0)); return true; }
	if (isType(actor, "Frog")) { al::setNerve(actor, getNerveAt(0x1D58028)); return true; }
	if (isType(actor, "Gamane")) { al::setNerve(actor, getNerveAt(0x1C8ED38)); return true; }
	if (isType(actor, "Hosui")) { al::setNerve(actor, getNerveAt(0x1C938E0)); return true; }
	if (isType(actor, "Kakku")) { al::setNerve(actor, getNerveAt(0x1C99D68)); return true; }
	if (isType(actor, "KaronWing")) { al::setNerve(actor, getNerveAt(0x1C9A920)); return true; }
	if (isType(actor, "Killer")) { al::setNerve(actor, getNerveAt(0x1C9B248)); return true; }
    if (isType(actor, "KuriboHack")) { if (!actor->getNerveKeeper() || al::isNerve(actor, getNerveAt(0x1C9D8B0))) return false; al::setNerve(actor, getNerveAt(0x1C9D888)); return true; }    if (isType(actor, "KuriboWing")) { al::setNerve(actor, getNerveAt(0x1C9F298)); return true; }
	if (isType(actor, "Megane")) { al::setNerve(actor, getNerveAt(0x1CA0D68)); return true; }
	if (isType(actor, "PackunFire")) { al::setNerve(actor, getNerveAt(0x1CA35F0)); return true; }
	if (isType(actor, "Pukupuku")) { al::setNerve(actor, getNerveAt(0x1CA6028)); return true; }
	if (isType(actor, "Tank")) { al::setNerve(actor, getNerveAt(0x1CA9430)); return true; }
	if (isType(actor, "Tsukkun")) { al::setNerve(actor, getNerveAt(0x1CAE360)); return true; }
	if (isType(actor, "Wanwan")) { al::setNerve(actor, getNerveAt(0x1CAFD68)); return true; }
	return false;
}

inline bool checkIsCar(al::LiveActor* targetHost, al::HitSensor* target) {
    return !al::isSensorName(target, "Brake") && isType(targetHost, "Car", "Car", "CarBreakable");
}

namespace AttackSensor {

    struct HackCapAttackSensorHook : public mallow::hook::Trampoline<HackCapAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;
            if (!isValidAttackTarget(target)) return;

            al::LiveActor* targetHost = al::getSensorHost(target);

            if (isType(targetHost, "KoopaCap", "KoopaCap")) return;

            Orig(thisPtr, source, target);
        }
    };

    struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;
            if (!isValidAttackTarget(target)) return;

            al::LiveActor* targetHost = al::getSensorHost(target);

            if (isType(targetHost, "KoopaCap", "KoopaCap")) return;
            
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

            if (isType(targetHost, "FireBall")
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

            if(isSpinAttack || isDoubleSpinAttack 
                || isPunchAttack || isHipDropAttack
                || isSpinFallback
            ) {
                bool inBuffer = isInHitBuffer(targetHost);

                // Handle ice cubes
                if (isType(targetHost, "PlayerIceCube")
                ) {
                    ((PlayerIceCube*)targetHost)->markHit(source);
                    if (!inBuffer) hitBuffer[hitBufferCount++] = targetHost; // Prevent wall bounce
                    return;
                }

                if (isSpinAttack || isDoubleSpinAttack || isSpinFallback) {
                    if (isAnyType(targetHost, "BlockQuestion", "BlockBrick", "BossForestBlock")
                    ) {
                        if (!inBuffer) hitBuffer[hitBufferCount++] = targetHost; // Prevent wall bounce
                        rs::sendMsgHammerBrosHammerHackAttack(target, source);
                        return;
                    }
                    rs::sendMsgPaint(target, source, paintClear, 150, 0);
                }

                if (inBuffer) { Orig(thisPtr, source, target); return; }

                if (!targetHost->getNerveKeeper()) return;

                const al::Nerve* sourceNrv = targetHost->getNerveKeeper()->getCurrentNerve();
                inBuffer |= sourceNrv == getNerveAt(0x1D03268); // GrowPlantSeedNrvHold
                inBuffer |= sourceNrv == getNerveAt(0x1D00EC8); // GrowFlowerSeedNrvHold
                inBuffer |= sourceNrv == getNerveAt(0x1D22B78); // RadishNrvHold

                bool isStake = isType(targetHost, "Stake") && sourceNrv == getNerveAt(0x1D36D20);
                bool isRadish = isType(targetHost, "Radish") && sourceNrv == getNerveAt(0x1D22B70);
                bool isRivet = isType(targetHost, "BossRaidRivet") && sourceNrv == getNerveAt(0x1C5F330);
                if (isStake || isRadish || isRivet
                ) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    if (thisPtr->mAnimator->isAnim("RabbitGet") && targetHost == isNearTarget
                    ) {
                        if (isRivet) { al::invalidateCollisionParts(targetHost); al::setVelocity(targetHost, al::getGravity(targetHost) * -44.0f); }
                        al::setNerve(targetHost, isStake ? getNerveAt(0x1D36D30) : isRadish ? getNerveAt(0x1D22BD8) : getNerveAt(0x1C5F338));
                        al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                        isNearCollectible = false;
                        isNearTarget = nullptr;
                    }
                    return;
                }
                if (isType(targetHost, "Koopa", "KoopaBig")) { KoopaBattle::attack(thisPtr, source, target); return; }

                if (isAnyType(targetHost, "BlockHard", "GolemClimb", "MarchingCubeBlock")
                ) {
                    if (rs::sendMsgHammerBrosHammerHackAttack(target, source)
                    ){
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (isAnyType(targetHost, "BreakMapParts", "BreakableWall", "CatchBomb", "DamageBall", "KickStone", "KoopaDamageBall", "MoonBasement", "PlayGuideBoard")
                    || isType(targetHost, "SignBoard", "!SignBoardNormal")
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
                if (isAnyType(targetHost, "BreedaWanwan", "TRex", "YoshiFruit")
                ) {
                    if (al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || al::sendMsgPlayerHipDrop(target, source, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                bool isCapSwitch = isType(targetHost, "CapSwitch");
                bool isCapSwitchTimer = isType(targetHost, "CapSwitchTimer");
                if (isCapSwitch || isCapSwitchTimer) {
                    if (isCapSwitchTimer) al::invalidateClipping(targetHost);
                    al::setNerve(targetHost, isCapSwitch ? getNerveAt(0x1CE3E18) : getNerveAt(0x1CE4338));
                    hitBuffer[hitBufferCount++] = targetHost;
                    al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                    return;
                }
                if (checkIsCar(targetHost, target) || isAnyType(targetHost, "ChurchDoor", "CollapseSandHill", "Doshi", "ReactionObject")
                    || isType(targetHost, "SignBoard", "SignBoardNormal")
                ) {
                    if (rs::sendMsgCapReflect(target, source)
                        || rs::sendMsgCapReflectCollide(target, source)
                        || rs::sendMsgCapAttack(target, source)
                        || rs::sendMsgCapAttackCollide(target, source)
                        || rs::sendMsgCapTouchWall(target, source, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (isType(targetHost, "TreasureBox")
                ) {
                    bool isWood = al::isModelName(targetHost, "TreasureBoxWood");
                    bool isExplosion = isWood && al::sendMsgExplosion(target, source, nullptr);
                    if (isExplosion || rs::sendMsgCapAttack(target, source)
                        || rs::sendMsgCapTouchWall(target, source, sead::Vector3f::zero, sead::Vector3f::zero)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        if (isExplosion) al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
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
                    if (thisPtr->mAnimator->isAnim("SpinLow")
                    ) {
                        bool capHit = rs::sendMsgCapReflect(target, source) || rs::sendMsgCapAttack(target, source);
                        handleStacked(targetHost, target, source);
                        if (trySwoon(targetHost) || capHit) {
                            if (!capHit) { al::addVelocity(targetHost, fireDir * 12.5f - al::getGravity(targetHost) * 25.0f); al::tryEmitEffect(thisPtr, "Hit", &spawnPos); }
                            hitBuffer[hitBufferCount++] = targetHost;
                            return;
                        }
                    }
                    bool isHit = false;
                    const al::Nerve* nrvBefore = targetHost->getNerveKeeper()->getCurrentNerve();
                    if (rs::sendMsgHackAttack(target, source)
                        || rs::sendMsgCapReflect(target, source)
                        || rs::sendMsgCapAttack(target, source)
                        || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || (isHit = rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true)
                            || rs::sendMsgBullHackAttack(target, source))
                    ) {
                        tryKnockback(targetHost, nrvBefore, fireDir, 25.0f);
                        hitBuffer[hitBufferCount++] = targetHost;
                        if(isHit) al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                        al::tryStartSe(thisPtr, "HitImpact");
                        return;
                    }
                }
                if (al::isSensorMapObj(target) && !isType(targetHost, "HipDrop")
                ) {
                    bool isHitImpact = false;
                    if (rs::sendMsgHackAttack(target, source)
                        || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                        || rs::sendMsgCapReflect(target, source)
                        || rs::sendMsgCapReflectCollide(target, source)
                        || al::sendMsgPlayerHipDrop(target, source, nullptr)
                        || rs::sendMsgCapAttack(target, source)
                        || rs::sendMsgCapAttackCollide(target, source)
                        || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || (isHitImpact = rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero))
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        if (!isHitImpact && !isType(targetHost, "Target")) al::tryStartSe(thisPtr, "HitImpact");
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
                
                bool isBlock = isType(targetHost, "BossForestBlock");
                if (isBlock || isAnyType(targetHost, "BlockHard", "BreakMapParts", "CatchBomb", "DamageBall", "FrailBox", "KoopaDamageBall", "MarchingCubeBlock", "MoonBasement", "PlayGuideBoard")
                    || (isType(targetHost, "ReactionObject") && al::isSensorCollision(target))
                    || isType(targetHost, "SignBoard", "!SignBoardNormal")
                ) {
                    if (al::sendMsgExplosion(target, source, nullptr)
                        || rs::sendMsgStatueDrop(target, source)
                        || rs::sendMsgKoopaCapPunchL(target, source)
                        || rs::sendMsgKoopaHackPunch(target, source)
                        || rs::sendMsgKoopaHackPunchCollide(target, source)
                        || rs::sendMsgCapAttack(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        if (!isBlock) al::tryEmitEffect(thisPtr, "HammerHit", &spawnPos);
                        return;
                    }
                }
                if (checkIsCar(targetHost, target)
                ) {
                    if (rs::sendMsgPlayerTouchFloorJumpCode(target, source)
                        || al::sendMsgExplosion(target, source, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(thisPtr, "HammerHit", &spawnPos);
                        return;
                    }
                }
                if (isAnyType(targetHost, "CollapseSandHill", "Doshi") || isType(targetHost, "SignBoard", "SignBoardNormal")
                ) {
                    if (rs::sendMsgCapAttack(target, source)
                        || rs::sendMsgCapAttackCollide(target, source)
                        || rs::sendMsgCapReflectCollide(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                if (isType(targetHost, "Koopa", "KoopaBig")
                ) {
                    if (rs::sendMsgKoopaCapPunchFinishL(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(thisPtr, "KoopaFinishHit", &spawnPos);
                        return;
                    }
                }
                if (isType(targetHost, "TreasureBox")
                ) {
                    bool isWood = al::isModelName(targetHost, "TreasureBoxWood");
                    bool isExplosion = isWood && al::sendMsgExplosion(target, source, nullptr);
                    if (isExplosion || rs::sendMsgCapAttack(target, source)
                        || rs::sendMsgCapTouchWall(target, source, sead::Vector3f::zero, sead::Vector3f::zero)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        if (isExplosion) al::tryEmitEffect(thisPtr, "Hit", &spawnPos);
                        return;
                    }
                }
                if (isType(targetHost, "TRex")
                ) {
                    if (al::sendMsgPlayerHipDrop(target, source, nullptr)
                        || rs::sendMsgSeedAttackBig(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(thisPtr, "HammerHit", &spawnPos);
                        return;
                    }
                }
                if (isType(targetHost, "Wanwan")
                ) {
                    if (rs::sendMsgWanwanReboundAttack(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
                bool isSouvenir = isType(targetHost, "Souvenir");
                bool isReaction = isType(targetHost, "ReactionObject");
                if (rs::sendMsgTRexAttack(target, source)
                    || al::sendMsgPlayerHipDrop(target, source, nullptr)
                    || al::sendMsgPlayerObjHipDrop(target, source, nullptr)
                    || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                    || rs::sendMsgPlayerHipDropHipDropSwitch(target, source)
                    || rs::sendMsgHackAttack(target, source)
                    || rs::sendMsgSphinxRideAttackTouchThrough(target, source, fireDir, fireDir)
                    || rs::sendMsgCapReflect(target, source)
                    || rs::sendMsgCapReflectCollide(target, source)
                    || (!isSouvenir && (rs::sendMsgCapAttack(target, source) || rs::sendMsgCapAttackCollide(target, source)))
                    || (!isReaction && rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true))
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

            if (isType(targetHost, "PlayerIceCube")
            ) {
                ((PlayerIceCube*)targetHost)->markHit(source);
                al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                thisPtr->kill();
                return;
            }

            if (isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }

            if (isAnyType(targetHost, "FireSwitch", "Candlestand")
            ) {
                if (rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)) {
                    al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                    thisPtr->kill();
                }
                return;
            }
            if ((al::isSensorEnemyBody(target) || isType(targetHost, "Rabbit"))
                && !al::isHideModel(targetHost) && !isAnyType(targetHost, "Boss", "Breeda", "Koopa")
            ) {
                handleStacked(targetHost, target, source);
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
                if (rs::sendMsgCapAttack(target, source)
                    || al::sendMsgExplosion(target, source, nullptr)
                    || al::sendMsgKickStoneAttackReflect(target, source)
                    || rs::sendMsgBullHackAttack(target, source)
                    || rs::sendMsgKoopaCapPunchL(target, source)
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

            rs::sendMsgCapAttack(target, source)
            || rs::sendMsgSphinxRideAttack(target, source)
            || rs::sendMsgSphinxRideAttackReflect(target, source)
            || rs::sendMsgHackAttack(target, source)
            || rs::sendMsgBullHackAttack(target, source)
            || rs::sendMsgKoopaCapPunchL(target, source);
        }
    };

    struct TankBulletAttackSensorInline : public mallow::hook::Inline<TankBulletAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* bullet = reinterpret_cast<TankBullet*>(ctx->X[19]);
            if (!al::isEqualString(bullet->getName(), "MarioTankBullet")) return;

            auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[22]);
            auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[21]);

            if (!isValidAttackTarget(target)) return;
            
            rs::sendMsgSeedAttackBig(target, source);

            ctx->W[0] = ctx->W[0]
                || al::sendMsgPlayerFireBallAttack(target, source)
                || rs::sendMsgCapAttack(target, source)
                || al::sendMsgKickStoneAttackReflect(target, source)
                || rs::sendMsgBullHackAttack(target, source)
                || rs::sendMsgKoopaCapPunchL(target, source);

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