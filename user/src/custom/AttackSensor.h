#pragma once
#include "custom/_Globals.h"
#include "custom/_Nerves.h"
#include "custom/PlayerFreeze.h"
#include "headers/PlayerIceCube.h"

// Check if has sensor type
inline bool hasSensor(al::LiveActor* actor, bool(*check)(const al::HitSensor*)) {
	al::HitSensorKeeper* keeper = actor->getHitSensorKeeper();
	for (s32 i = 0; keeper && i < keeper->getSensorNum(); i++) {
		if (check(keeper->getSensor(i))) return true;
	}
	return false;
}

// Guard Mario against attacks
inline bool isValidAttackTarget(al::HitSensor* target) {
    al::LiveActor* targetHost = al::getSensorHost(target);
    return targetHost && targetHost != isHakoniwa;
}

// Handle stacked enemies
inline bool handleStacked(al::LiveActor*& actor, al::HitSensor* target, al::HitSensor* source) {
	if (!actor->getNerveKeeper()) return false;
    if (isType(actor, "BreedaWanwan")) {
        static bool(*tryBlowCap)(al::LiveActor*, al::HitSensor*) = nullptr;
        if (!tryBlowCap) nn::ro::LookupSymbol(reinterpret_cast<uintptr_t*>(&tryBlowCap), "_ZN12BreedaWanwan10tryBlowCapEPN2al9HitSensorE");
        while (tryBlowCap(actor, source)) {}
        return true;
    }
	if (isType(actor, "KuriboHack")) {
		rs::sendMsgYoshiTongueEatBind(target, source, nullptr, nullptr, nullptr);
		al::setNerve(actor, getNerveAt(0x1C9D888));
		return true;
	}
	if (isType(actor, "StackerCap")) {
        if (!al::isNerve(actor, getNerveAt(0x1C7B7F0)) && !al::isNerve(actor, getNerveAt(0x1C7B7F8))) return false; // OnHead, OnHeadAttack
        al::LiveActor* host = *reinterpret_cast<al::LiveActor**>((char*)actor + 0x108);
        int count = host && al::isAlive(host) ? *reinterpret_cast<int*>((char*)host + 0x154) : 0;
        if (count <= 0) return false;
        actor = (*reinterpret_cast<al::LiveActor***>(*reinterpret_cast<char**>((char*)host + 0x108) + 0x18))[count - 1]; // topCap
        static bool(*blowCapOnHead)(al::LiveActor*, const sead::Vector3f&, const sead::Vector3f&) = nullptr;
        if (!blowCapOnHead) nn::ro::LookupSymbol(reinterpret_cast<uintptr_t*>(&blowCapOnHead), "_ZN7Stacker13blowCapOnHeadERKN4sead7Vector3IfEES4_");
        blowCapOnHead(host, al::getSensorPos(source), sead::Vector3f::zero);
        return true;
    }
	return false;
}

// Handle swooning enemies
inline bool trySwoon(al::LiveActor* actor, bool isSwoon = true) {
	if (isType(actor, "FireBros") || isType(actor, "HammerBros")) { if (isSwoon) al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x130), getNerveAt(0x1C85DD0)); return true; }
	if (isType(actor, "Imomu")) { if (isSwoon) al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x118), getNerveAt(0x1C94EB0)); return true; }
	if (isType(actor, "Senobi")) { if (isSwoon) al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x128), getNerveAt(0x1CA83A8)); return true; }
	if (isType(actor, "BreedaWanwan")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C621D0)); return true; }
	if (isType(actor, "Bull")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C87D18)); return true; }
	if (isType(actor, "Byugo")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C887B0)); return true; }
	if (isType(actor, "Frog")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1D58028)); return true; }
	if (isType(actor, "Gamane")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C8ED38)); return true; }
	if (isType(actor, "Hosui")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C938E0)); return true; }
	if (isType(actor, "Kakku")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C99D68)); return true; }
	if (isType(actor, "KaronWing")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9A920)); return true; }
	if (isType(actor, "Killer")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9B248)); return true; }
	if (isType(actor, "KuriboHack")) { if (!actor->getNerveKeeper() || al::isNerve(actor, getNerveAt(0x1C9D8B0))) return false; if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9D888)); return true; }
	if (isType(actor, "KuriboWing")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9F298)); return true; }
	if (isType(actor, "Megane")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA0D68)); return true; }
	if (isType(actor, "PackunFire")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA35F0)); return true; }
	if (isType(actor, "Pukupuku")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA6028)); return true; }
	if (isType(actor, "Tank")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA9430)); return true; }
	if (isType(actor, "Tsukkun")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CAE360)); return true; }
	if (isType(actor, "Wanwan")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CAFD68)); return true; }
	return false;
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

            if (!al::isSensorName(source, "GalaxySpin") && !al::isSensorName(source, "DoubleSpin")
                && !al::isSensorName(source, "Punch") && !al::isSensorName(source, "HipDropKnockDown")) { Orig(thisPtr, source, target); return; }

            al::LiveActor* targetHost = al::getSensorHost(target);
            if (!isValidAttackTarget(target) || al::isSensorName(target, "Brake")
                || isType(targetHost, "KoopaCap", "KoopaCap")
                || (isType(targetHost, "FireBall") && al::calcSpeedH(thisPtr) >= thisPtr->mConst->getDashFastBorderSpeed())) return;

            if (isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }

            setupHitEffect(source, target);
            sead::Vector3f fireDir = getFireDir(thisPtr, targetHost);

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

            if (isSpinAttack || isDoubleSpinAttack || isPunchAttack
                || isHipDropAttack || isSpinFallback
            ) {
                if (!isPunchAttack) {
                    if (isAnyType(targetHost, "BlockQuestion", "BlockBrick", "BossForestBlock")) {
                        hitBuffer[hitBufferCount++] = nullptr; // Prevent wall bounce, keep hittable
                        rs::sendMsgHammerBrosHammerHackAttack(target, source);
                        return;
                    }
                    rs::sendMsgPaint(target, source, paintClear, 150, 0);
                }
                // Handle ice cubes
                if (isType(targetHost, "PlayerIceCube")) {
                    hitBuffer[hitBufferCount++] = targetHost; // Prevent wall bounce
                    ((PlayerIceCube*)targetHost)->markHit(source);
                    return;
                }
                if (isType(targetHost, "Koopa", "KoopaBig")) { KoopaBattle::attack(thisPtr, source, target); return; }

                const al::Nerve* sourceNrv = targetHost->getNerveKeeper() ? targetHost->getNerveKeeper()->getCurrentNerve() : nullptr;
                bool isStake = isType(targetHost, "Stake") && sourceNrv == getNerveAt(0x1D36D20);
                bool isRadish = isType(targetHost, "Radish") && sourceNrv == getNerveAt(0x1D22B70);
                bool isRivet = isType(targetHost, "BossRaidRivet") && sourceNrv == getNerveAt(0x1C5F330);
                bool isSwitch = isType(targetHost, "CapSwitch");
                bool isTimer = isType(targetHost, "CapSwitchTimer");

                if (isStake || isRadish || isRivet) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    if (targetHost == isNearTarget && thisPtr->mAnimator->isAnim("RabbitGet")
                    ) {
                        if (isRivet) { al::invalidateCollisionParts(targetHost); al::setVelocity(targetHost, al::getGravity(targetHost) * -44.0f); }
                        al::setNerve(targetHost, isStake ? getNerveAt(0x1D36D30) : isRadish ? getNerveAt(0x1D22BD8) : getNerveAt(0x1C5F338));
                        isHitEffect(thisPtr, targetHost);
                        isNearCollectible = false;
                        isNearTarget = nullptr;
                    }
                    return;
                }
                if (isSwitch || isTimer) {
                    if (isTimer) al::invalidateClipping(targetHost);
                    al::setNerve(targetHost, isSwitch ? getNerveAt(0x1CE3E18) : getNerveAt(0x1CE4338));
                    hitBuffer[hitBufferCount++] = targetHost;
                    isHitEffect(thisPtr, targetHost);
                    return;
                }
                if (thisPtr->mAnimator->isAnim("SpinLow")) {
                    if (trySwoon(targetHost, false) || isAnyType(targetHost, "Ball", "Bomb", "Togezo")
                    ) {
                        bool isHit = rs::sendMsgCapAttack(target, source) || rs::sendMsgCapReflect(target, source);
                        if (isHit) {
                            if (trySwoon(targetHost, false)) trySwoon(targetHost);
                            handleStacked(targetHost, target, source);
                            hitBuffer[hitBufferCount++] = targetHost;
                            isHitEffect(thisPtr, targetHost);
                            return;
                        }
                        if (!trySwoon(targetHost, false)) return;
                    }
                }
                bool isBlock = isAnyType(targetHost, "BlockHard", "Marching");
                if (isBlock || isAnyType(targetHost, "Ball", "Board", "Bomb", "Break", "Bull", "Church", "Golem", "KickStone", "Moon", "Souvenir", "TreasureBox")
                ) {
                    if ((!isBlock || al::isSensorCollision(target))
                        && (rs::sendMsgHammerBrosHammerHackAttack(target, source) || al::sendMsgExplosion(target, source, nullptr)
                            || rs::sendMsgBullHackAttack(target, source) || rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true)
                            || al::sendMsgPlayerObjHipDrop(target, source, nullptr) || rs::sendMsgCapTouchWall(target, source, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})
                            || rs::sendMsgKoopaCapPunchL(target, source) || rs::sendMsgKoopaHackPunchCollide(target, source))
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        isHitEffect(thisPtr, targetHost);
                        return;
                    } else if (isBlock) return;
                }
                if (rs::sendMsgHackAttack(target, source) || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                    || rs::sendMsgCapReflect(target, source) || rs::sendMsgCapReflectCollide(target, source)
                    || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                    || rs::sendMsgCapAttack(target, source) || rs::sendMsgCapAttackCollide(target, source)
                    || rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)
                ) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    if (hasSensor(targetHost, al::isSensorEnemyBody) && hasSensor(targetHost, al::isSensorEnemyAttack)) al::addVelocity(targetHost, fireDir * 25.0f);
                    if (!hasSensor(targetHost, al::isSensorMapObj) || (hasSensor(targetHost, al::isSensorCollision) && !hasSensor(targetHost, al::isSensorEnemyAttack))) al::tryStartSe(thisPtr, "HitImpact");
                    return;
                }
            }
        }
    };

    struct HammerAttackSensorHook : public mallow::hook::Trampoline<HammerAttackSensorHook> {
        static void Callback(HammerBrosHammer* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            if (!al::isNerve(isHakoniwa, &HammerNrv)
                || !al::isSensorName(source, "AttackHack")) { Orig(thisPtr, source, target); return; }

            al::LiveActor* targetHost = al::getSensorHost(target);
            if (!isValidAttackTarget(target) || al::isSensorName(target, "Brake")
                || isType(targetHost, "KoopaCap", "KoopaCap")) return;

            if (isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }

            setupHitEffect(source, target);
            sead::Vector3f fireDir = getFireDir(thisPtr, targetHost);

            rs::sendMsgPaint(target, source, paintClear, 300, 0);

            bool isBlock = isAnyType(targetHost, "BlockHard", "Marching");
            if (isBlock || isAnyType(targetHost, "Ball", "Board", "Bomb", "Break", "Cactus", "Church", "Golem", "Koopa", "KickStone", "Moon", "Souvenir", "TreasureBox", "TRex", "Wanwan")
            ) {
                if ((!isBlock || al::isSensorCollision(target))
                    && (rs::sendMsgSeedAttackBig(target, source) || rs::sendMsgWanwanReboundAttack(target, source)
                        || rs::sendMsgTRexAttack(target, source) || rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true)
                        || rs::sendMsgHammerBrosHammerHackAttack(target, source) || al::sendMsgExplosion(target, source, nullptr)
                        || rs::sendMsgSphinxRideAttackTouchThrough(target, source, fireDir, fireDir) || rs::sendMsgStatueDrop(target, source)
                        || rs::sendMsgCapTouchWall(target, source, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})
                        || rs::sendMsgKoopaCapPunchFinishL(target, source) || rs::sendMsgKoopaCapPunchL(target, source)
                        || rs::sendMsgKoopaHackPunchCollide(target, source))
                ) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    isHitEffect(thisPtr, targetHost);
                    return;
                } else if (isBlock) return;
            }
            if (rs::sendMsgHackAttack(target, source) || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                || al::sendMsgPlayerHipDrop(target, source, nullptr) || al::sendMsgPlayerObjHipDrop(target, source, nullptr)
                || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr) || rs::sendMsgPlayerHipDropHipDropSwitch(target, source)
                || rs::sendMsgCapReflect(target, source) || rs::sendMsgCapReflectCollide(target, source)
                || rs::sendMsgCapAttack(target, source) || rs::sendMsgCapAttackCollide(target, source)
                || rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)
            ) {
                hitBuffer[hitBufferCount++] = targetHost;
                return;
            }
        }
    };

    struct FireballAttackSensorHook : public mallow::hook::Trampoline<FireballAttackSensorHook> {
        static void Callback(FireBrosFireBall* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            if (!al::isSensorName(source, "AttackHack")
                || !al::isEqualString(thisPtr->getName(), "MarioIceBall")) { Orig(thisPtr, source, target); return; }

            al::LiveActor* targetHost = al::getSensorHost(target);
            if (!isValidAttackTarget(target) || al::isEqualString(targetHost->getName(), "MarioIceBall")) return;

            if (isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }

            sead::Vector3f sourcePos = al::getSensorPos(source);

            if (isType(targetHost, "PlayerIceCube")
            ) {
                ((PlayerIceCube*)targetHost)->markHit(source);
                al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                thisPtr->kill();
                return;
            }
            if (isAnyType(targetHost, "FireSwitch", "Candlestand")
            ) {
                if (rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)) {
                    al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                    thisPtr->kill();
                }
                return;
            }
            if (!al::isHideModel(targetHost) && !isAnyType(targetHost, "Boss", "Breeda", "Koopa")
                && (al::isSensorEnemyBody(target) || isType(targetHost, "Rabbit"))
            ) {
                handleStacked(targetHost, target, source);
                hitBuffer[hitBufferCount++] = targetHost;
                PlayerFreeze::freezeActor(targetHost, 1800);
                al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                thisPtr->kill();
                return;
            }
        }
    };

    struct FireballAttackSensorInline : public mallow::hook::Inline<FireballAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* fireball = reinterpret_cast<FireBrosFireBall*>(ctx->X[19]);
            auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[20]);
            auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[21]);

            al::LiveActor* targetHost = al::getSensorHost(target);
            if (!isValidAttackTarget(target) || isType(targetHost, "KoopaCap", "KoopaCap")            
                || (!al::isEqualString(fireball->getName(), "MarioFireBall")
                    && !al::isEqualString(fireball->getName(), "MarioIceBall"))) return;

            setupHitEffect(source, target);

            if (!ctx->W[0] && (rs::sendMsgCapAttack(target, source)
                || al::sendMsgExplosion(target, source, nullptr)
                || al::sendMsgKickStoneAttackReflect(target, source)
                || rs::sendMsgBullHackAttack(target, source)
                || rs::sendMsgKoopaCapPunchL(target, source))
            ) {
                ctx->W[0] = true;
                isHitEffect(fireball, targetHost);
            }
        }
    };

    struct TankBulletAttackSensorInline : public mallow::hook::Inline<TankBulletAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* bullet = reinterpret_cast<TankBullet*>(ctx->X[19]);
            auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[22]);
            auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[21]);
            
            al::LiveActor* targetHost = al::getSensorHost(target);
            if (!isValidAttackTarget(target) || isType(targetHost, "KoopaCap", "KoopaCap")            
                || !al::isEqualString(bullet->getName(), "MarioTankBullet")) return;

            rs::sendMsgSeedAttackBig(target, source);

            ctx->W[0] = ctx->W[0]
                || al::sendMsgPlayerFireBallAttack(target, source)
                || rs::sendMsgCapAttack(target, source)
                || al::sendMsgKickStoneAttackReflect(target, source)
                || rs::sendMsgBullHackAttack(target, source)
                || rs::sendMsgKoopaCapPunchL(target, source)
                || rs::sendMsgKoopaHackPunch(target, source);

            rs::sendMsgWeaponItemGet(target, source);
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

    inline void Install() {
        #ifndef ALLOW_CAPPY_ONLY
            HackCapAttackSensorHook::InstallAtSymbol("_ZN7HackCap12attackSensorEPN2al9HitSensorES2_");
            PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");
        #endif
        HammerAttackSensorHook::InstallAtSymbol("_ZN16HammerBrosHammer12attackSensorEPN2al9HitSensorES2_");
        FireballAttackSensorInline::InstallAtOffset(0x100E70);
        FireballAttackSensorHook::InstallAtSymbol("_ZN16FireBrosFireBall12attackSensorEPN2al9HitSensorES2_");
        TankBulletAttackSensorInline::InstallAtOffset(0x189C7C);
        MotorcycleAttackSensorInline::InstallAtOffset(0x2C77EC);
    }
}