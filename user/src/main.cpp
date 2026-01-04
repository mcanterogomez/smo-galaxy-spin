// C++ Standard Library
#include <cstddef>
#include <limits.h>

// External hooking & config libraries
#include <exl/hook/base.hpp>
#include <mallow/config.hpp>
#include <mallow/init/initLogging.hpp>
#include <mallow/logging/logger.hpp>
#include <mallow/mallow.hpp>

// Core game system
#include "System/GameDataFunction.h"

// Engine / “Library” headers
#include "Library/Base/StringUtil.h"
#include "Library/Controller/InputFunction.h"
#include "Library/Controller/SpinInputAnalyzer.h"
#include "Library/Effect/EffectKeeper.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/LiveActor/ActorCollisionFunction.h"
#include "Library/LiveActor/ActorClippingFunction.h"
#include "Library/LiveActor/ActorFlagFunction.h"
#include "Library/LiveActor/ActorModelFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorSensorFunction.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/LiveActorFlag.h"
#include "Library/LiveActor/LiveActorFunction.h"
#include "Library/LiveActor/LiveActorGroup.h"
#include "Library/Math/MathUtil.h"
#include "Library/Nature/NatureUtil.h"
#include "Library/Nature/WaterSurfaceFinder.h"
#include "Library/Nerve/NerveSetupUtil.h"
#include "Library/Nerve/NerveUtil.h"
#include "Library/Placement/PlacementFunction.h"
#include "Library/Player/PlayerUtil.h"
#include "Library/Se/SeFunction.h"
#include "Library/Shadow/ActorShadowUtil.h"

// Game‑specific utilities
#include "Project/HitSensor/HitSensor.h"
#include "Util/PlayerCollisionUtil.h"
#include "Util/PlayerUtil.h"
#include "Util/SensorMsgFunction.h"

// Player actor & state headers
#include "Player/IUsePlayerCollision.h"
#include "Player/PlayerActionGroundMoveControl.h"
#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerColliderHakoniwa.h"
#include "Player/PlayerCounterForceRun.h"
#include "Player/PlayerEquipmentUser.h"
#include "Player/PlayerFunction.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerInput.h"
#include "Player/PlayerJudgeStartWaterSurfaceRun.h"
#include "Player/PlayerJudgeWaterSurfaceRun.h"
#include "Player/PlayerModelHolder.h"
#include "Player/PlayerStateHeadSliding.h"
#include "Player/PlayerStateSpinCap.h"
#include "Player/PlayerStateSwim.h"
#include "Player/PlayerTrigger.h"
#include "Player/HackCap.h"

// Mod‑specific & custom actors
#include "actors/custom/CustomPlayerConst.h"
#include "actors/custom/FireBall.h"
#include "actors/custom/HammerBrosHammer.h"
#include "actors/custom/PlayerAnimator.h"
#include "actors/custom/PlayerDamageKeeper.h"
#include "actors/custom/PlayerJudgeWallHitDown.h"
#include "actors/custom/PlayerStateJump.h"
#include "actors/custom/PlayerStateWait.h"
#include "ModOptions.h"
#include "math/seadVectorFwd.h"

namespace rs {
    bool is2D(const IUseDimension*);    
    al::HitSensor* tryGetCollidedWallSensor(IUsePlayerCollision const* collider);
    al::HitSensor* tryGetCollidedGroundSensor(IUsePlayerCollision const* collider);
}

namespace PlayerEquipmentFunction {
    bool isEquipmentNoCapThrow(const PlayerEquipmentUser*);
}

class PlayerCarryKeeper {
public:
    bool isCarry() const;
};

using mallow::log::logLine;

// Mod code

const al::Nerve* getNerveAt(uintptr_t offset)
{
    return (const al::Nerve*)((((u64)malloc) - 0x00724b94) + offset);
}

bool isPadTriggerGalaxySpin(int port) {
    switch (mallow::config::getConfg<ModOptions>()->spinButton) {
        case 'L':
            return al::isPadTriggerL(port);
        /*case 'R':
            return al::isPadTriggerR(port);*/
        case 'X':
            return al::isPadTriggerX(port);
        case 'Y':
        default:
            return al::isPadTriggerY(port);
    }
}

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

struct TriggerCameraReset : public mallow::hook::Trampoline<TriggerCameraReset> {
    static bool Callback(al::LiveActor* actor, int port) {
        if (al::isPadTriggerR(-1)) return false;
        return Orig(actor, port);
    }
};

al::LiveActor* hitBuffer[0x40];
int hitBufferCount = 0;

const uintptr_t spinCapNrvOffset = 0x1d78940;
const uintptr_t nrvSpinCapFall = 0x1d7ff70;
const uintptr_t nrvHakoniwaWait = 0x01D78918;
const uintptr_t nrvHakoniwaSquat = 0x01D78920;
const uintptr_t nrvHakoniwaFall = 0x01d78910;
const uintptr_t nrvHakoniwaHipDrop = 0x1D78978;
const uintptr_t nrvHakoniwaJump = 0x1D78948;
//const uintptr_t nrvHakoniwaDamage = 0x1D789B0;

bool isGalaxySpin = false;
bool canGalaxySpin = true;
bool canStandardSpin = true;
bool isGalaxyAfterStandardSpin = false;  // special case, as switching between spins resets isGalaxySpin and canStandardSpin
bool isStandardAfterGalaxySpin = false;
int galaxyFakethrowRemainder = -1;  // -1 = inactive, -2 = request to start, positive = remaining frames
bool triggerGalaxySpin = false;
bool prevIsCarry = false;

bool isSpinRethrow = false; // true only while tryCapSpinAndRethrow is calling tryActionCapSpinAttackImpl
int galaxySensorRemaining = -1;

bool isPunching = false; // Global flag to track punch state
bool isPunchRight = false; // Global flag to track punch direction
bool isFinalPunch = false; // Global flag to track final punch

bool isSpinActive = false; // Global flag to track spin state
bool isNearCollectible = false; // Global flag to track if near a collectible
bool isNearTreasure = false; // Global flag to track if near a collectible
bool isNearSwoonedEnemy = false;  // Global flag to track if near a swooned enemy

// Global flags to track states
bool isMario = false;
bool isNoCap = false;
bool isFeather = false;
bool isFire = false;
bool isTanooki = false;
bool isBrawl = false;
bool isSuper = false;

static PlayerActorHakoniwa* isHakoniwa = nullptr; // Global pointer for Hakoniwa
static HammerBrosHammer* isHammer = nullptr; // Global pointer for hammer attack
static al::LiveActor* isKoopa = nullptr; // Global pointer for Bowser

al::LiveActorGroup* fireBalls = nullptr; // Global pointer for fireballs
bool nextThrowLeft = true; // Global flag to track next throw direction
bool canFireball = false; // Global flag to track fireball trigger
int fireStep = -1;
static inline bool isFireThrowing() { return fireStep >= 0; }

bool tauntRightAlt = false; // Global flag to alternate taunt direction
bool isDoubleJump = false; // Global flag to track double jump state
bool isDoubleJumpConsume=false; // Global flag to track double jump consumed
int isCapeActive = -1; // Global flag to track cape state

// Custom Nerves
class PlayerStateSpinCapNrvGalaxySpinAir; 
extern PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir; 

class PlayerStateSpinCapNrvGalaxySpinGround : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
        PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        bool isCape = (isMario && cape && al::isAlive(cape)) || isFeather;

        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");
        bool isRotatingL = state->mAnimator->isAnim("SpinGroundL");
        bool isRotatingR = state->mAnimator->isAnim("SpinGroundR");
        bool isCarrying = player->mCarryKeeper->isCarry();
        bool isFinish = state->mAnimator->isAnim("KoopaCapPunchFinishL")
            || state->mAnimator->isAnim("KoopaCapPunchFinishR");
        bool didSpin = player->mInput->isSpinInput();
        int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;

        isSpinActive = true;

        if (al::isFirstStep(state)
        ) {
            state->mAnimator->endSubAnim();
            isPunchRight = !isPunchRight;

            if (!isSpinning) {
                if (didSpin) {
                    if (spinDir > 0) {
                        state->mAnimator->startSubAnim("SpinAttackLeft");
                        state->mAnimator->startAnim ("SpinAttackLeft");
                    }
                    else {
                        state->mAnimator->startSubAnim("SpinAttackRight");
                        state->mAnimator->startAnim ("SpinAttackRight");
                    }
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingL) {
                    state->mAnimator->startSubAnim("SpinAttackLeft");
                    state->mAnimator->startAnim("SpinAttackLeft");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingR) {
                    state->mAnimator->startSubAnim("SpinAttackRight");
                    state->mAnimator->startAnim("SpinAttackRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isCarrying) {
                    state->mAnimator->startSubAnim("SpinSeparate");
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else if (isNearCollectible) {
                    state->mAnimator->startAnim("RabbitGet");
                    al::validateHitSensor(state->mActor, "Punch");
                } else if (isNearTreasure || isNearSwoonedEnemy) {
                    state->mAnimator->startAnim("Kick");
                    al::validateHitSensor(state->mActor, "Punch");
                } else {
                    if (isCape) {
                        al::setNerve(state, reinterpret_cast<al::Nerve*>(&GalaxySpinAir));
                        return;
                    } else if (isTanooki) {
                        state->mAnimator->startSubAnim("TailAttack");
                        state->mAnimator->startAnim("TailAttack");
                        al::validateHitSensor(state->mActor, "GalaxySpin");
                        galaxySensorRemaining = 21;
                    } else {
                        if (isFinalPunch) {
                            if (isPunchRight) {
                                state->mAnimator->startSubAnim("KoopaCapPunchFinishRStart");
                                state->mAnimator->startAnim("KoopaCapPunchFinishR");
                            } else {
                                state->mAnimator->startSubAnim("KoopaCapPunchFinishLStart");
                                state->mAnimator->startAnim("KoopaCapPunchFinishL");
                            }
                            isFinalPunch = false;
                        } else {
                            if (isPunchRight) {
                                state->mAnimator->startSubAnim("KoopaCapPunchRStart");
                                state->mAnimator->startAnim("KoopaCapPunchR");
                            } else {
                                state->mAnimator->startSubAnim("KoopaCapPunchLStart");
                                state->mAnimator->startAnim("KoopaCapPunchL");
                            }
                        }
                        // Make winding up invincible
                        al::invalidateHitSensor(state->mActor, "Foot");
                        al::invalidateHitSensor(state->mActor, "Body");
                        al::invalidateHitSensor(state->mActor, "Head");

                        isPunching = true; // Validate punch animations
                    }
                }
            }
        }
        
        if (!isSpinning && !isCarrying
            && !isNearCollectible && !isNearTreasure && !isNearSwoonedEnemy
            && !isRotatingL && !isRotatingR
            && !isTanooki
        ) {
            if (al::isStep(state, 3)) {
                // Reduce Mario's existing momentum by 50%
                sead::Vector3 currentVelocity = al::getVelocity(player);
                currentVelocity *= 0.5f;
                al::setVelocity(player, currentVelocity);
    
                // Apply a small forward movement during the punch
                sead::Vector3f forward;
                al::calcQuatFront(&forward, player);
                forward.normalize();
                forward *= 5.0f;
                al::addVelocity(player, forward);
            }
            if (al::isStep(state, 6)) {
                // Make Mario vulnerable again
                al::validateHitSensor(state->mActor, "Foot");
                al::validateHitSensor(state->mActor, "Body");
                al::validateHitSensor(state->mActor, "Head");
                al::validateHitSensor(state->mActor, "Punch");
                //galaxySensorRemaining = 15;
            }
        }
        
        if (isFinish) al::setVelocity(player, sead::Vector3f::zero);
        else state->updateSpinGroundNerve();

        if (al::isGreaterStep(state, 41)) al::invalidateHitSensor(state->mActor, "DoubleSpin");
        if (al::isGreaterStep(state, 21)) al::invalidateHitSensor(state->mActor, "GalaxySpin");
        if (al::isGreaterStep(state, 15)) al::invalidateHitSensor(state->mActor, "Punch");

        if (state->mAnimator->isAnimEnd()) {
            state->kill();
            isSpinActive = false;
        }
    }
};

class PlayerStateSpinCapNrvGalaxySpinAir : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
        PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        bool isCape = (isMario && cape && al::isAlive(cape)) || isFeather;

        bool isRotatingAirL  = state->mAnimator->isAnim("StartSpinJumpL")
            || state->mAnimator->isAnim("RestartSpinJumpL");
        bool isRotatingAirR  = state->mAnimator->isAnim("StartSpinJumpR")
            || state->mAnimator->isAnim("RestartSpinJumpR");
        bool isCarrying = player->mCarryKeeper->isCarry();
        bool didSpin = player->mInput->isSpinInput();
        int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;
        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");

        isSpinActive = true;

        if (state->mAnimator->isAnim("CapeAttack")
            && cape && al::isDead(cape)
        ) {
            state->mAnimator->startAnim("SpinSeparate");
            al::validateHitSensor(state->mActor, "GalaxySpin"); 
            galaxySensorRemaining = 21; 
        }
        
        if(al::isFirstStep(state)
        ) {
            const char* cur = state->mAnimator->mCurAnim.cstr();
            if (!al::isEqualSubString(cur, "SpinCap")) state->mAnimator->endSubAnim();
            
            if (!isSpinning) {
                if (didSpin) {
                    if (spinDir > 0) state->mAnimator->startAnim("SpinAttackAirLeft");
                    else state->mAnimator->startAnim("SpinAttackAirRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingAirL) {
                    state->mAnimator->startAnim("SpinAttackAirLeft");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingAirR) {
                    state->mAnimator->startAnim("SpinAttackAirRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isCarrying) {
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else if (isCape) {
                    state->mAnimator->startAnim("CapeAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else if (isTanooki) {
                    state->mAnimator->startAnim("TailAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else {
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                }
            }
        }
        
        state->updateSpinAirNerve();

        if ((isCape || isTanooki)
            && state->mAnimator->isAnimEnd()
        ) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
        if (!isSpinning
            && al::isGreaterStep(state, 41)
        ) {
            al::invalidateHitSensor(state->mActor, "DoubleSpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
        if (isSpinning
            && al::isGreaterStep(state, 21)
        ) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
    }
};

class PlayerActorHakoniwaNrvHammer : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* hammer = al::tryGetSubActor(model, "Hammer");

        bool isGround = rs::isOnGround(player, player->mCollider);
        bool isWater = al::isInWater(player);
        bool isSurface = player->mWaterSurfaceFinder->isFoundSurface();

        /*auto* judge = reinterpret_cast<PlayerJudgeWallHitDown*>(player->mJudgeWallHitDown);
        auto* equip = player->mEquipmentUser;
        auto* counter = player->mCounterForceRun;*/

        const sead::Matrix34f* mL = al::getJointMtxPtr(model, "ArmL2");
        const sead::Matrix34f* mR = al::getJointMtxPtr(model, "ArmR2");

        sead::Vector3 posL = mL->getTranslation();
        sead::Vector3 posR = mR->getTranslation();
        sead::Vector3 mid  = (posL + posR) * 0.5f;

        sead::Quatf qMid;
        sead::Quatf qL; mL->toQuat(qL);
        sead::Quatf qR; mR->toQuat(qR);
        al::slerpQuat(&qMid, qL, qR, 0.5f);

        static sead::Matrix34f hammerMtx;
        hammerMtx.makeQT(qMid, mid);
               
        if (al::isFirstStep(player)
        ) {
            player->mAnimator->endSubAnim();
            hitBufferCount = 0;

            if (hammer) al::hideModelIfShow(hammer);

            isHammer->makeActorAlive();
            isHammer->attach(
            &hammerMtx,
            sead::Vector3(0.0f, -12.5f, -37.5f),
            sead::Vector3(0.0f, sead::Mathf::deg2rad(-90.0f), 0.0f),
            nullptr);

            al::onCollide(isHammer);
            al::invalidateClipping(isHammer);
            al::showShadow(isHammer);

            if (!isGround) {
                player->mAnimator->startAnim("RollingStart");
                al::validateHitSensor(isHammer, "AttackHack");
            } else {
                player->mAnimator->startAnim("HammerAttack");
            }
        }
        if (player->mAnimator->isAnimEnd()
            && player->mAnimator->isAnim("RollingStart")
        ) {
            player->mAnimator->startAnim("Rolling");
            al::tryStartAction(isHammer, "Spin");
        }
        if (isGround
            && (player->mAnimator->isAnim("RollingStart") || player->mAnimator->isAnim("Rolling"))
        ) {
            player->mAnimator->endSubAnim();
            al::tryStartAction(isHammer, "Hammer");
            player->mAnimator->startAnim("HammerAttack");
        }
        if (al::isStep(player, 3)
        ) {
            sead::Vector3 currentVelocity = al::getVelocity(player);
            if (isGround) {
                currentVelocity *= 0.5f;
            } else {
                if (currentVelocity.y > 0.0f) currentVelocity.y = 0.0f;
                //currentVelocity += al::getGravity(player);
            }
            al::setVelocity(player, currentVelocity);
        }

        if (!isGround) al::addVelocity(player, (al::getGravity(player) * 0.5f));

        if (al::isStep(player, 6)) al::validateHitSensor(isHammer, "AttackHack");
        
        if (player->mAnimator->isAnimEnd()
        ) {
            isHammer->makeActorDead();
            if (hammer) al::showModelIfHide(hammer);
            al::offCollide(isHammer);
            al::invalidateHitSensor(isHammer, "AttackHack");
            al::setNerve(player, getNerveAt(nrvHakoniwaFall));
            return;
        }
        else if (isWater && !isSurface
        ) {
            isHammer->makeActorDead();
            if (hammer) al::showModelIfHide(hammer);
            al::offCollide(isHammer);
            al::invalidateHitSensor(isHammer, "AttackHack");
            al::setNerve(player, getNerveAt(nrvHakoniwaFall));
            al::tryEmitEffect(isHammer, "Break", nullptr);
            return;
        }
        if (hammer && al::isDead(isHammer)) al::showModelIfHide(hammer);
    }

    void executeOnEnd(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* model  = player->mModelHolder->findModelActor("Normal");
        auto* hammer = al::tryGetSubActor(model, "Hammer");

        if (hammer) al::showModelIfHide(hammer);

        if (isHammer) {
            al::offCollide(isHammer);
            al::invalidateHitSensor(isHammer, "AttackHack");
            isHammer->makeActorDead();
        }
    }
};

class PlayerActorHakoniwaNrvTauntLeft : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* anim = player->mAnimator;

        al::setVelocity(player, sead::Vector3f::zero);

        if (al::isFirstStep(player)
        ) {
            anim->endSubAnim();
            anim->startAnim("WearEnd");
            if (isFire || isBrawl) anim->startAnim("WearEndBrawl");
            if (isSuper) anim->startAnim("WearEndSuper");
        }
        if (anim->isAnimEnd()
        ) {
            al::setNerve(player, getNerveAt(nrvHakoniwaWait));
            return;
        }
    }
};

class PlayerActorHakoniwaNrvTauntRight : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* anim = player->mAnimator;
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        auto* effect = static_cast<al::IUseEffectKeeper*>(model);

        al::setVelocity(player, sead::Vector3f::zero);

        if (al::isFirstStep(player)
        ) {
            anim->endSubAnim();
            anim->startAnim("TauntMario");
            if (tauntRightAlt) anim->startAnim("AreaWait64");
            if (isFire || isBrawl || isSuper) anim->startAnim("TauntBrawl");
            if (isBrawl && tauntRightAlt) {
                if (cape && al::isDead(cape)) anim->startAnim("LandJump3");
                else anim->startAnim("TauntFeather");
            }
            if (isFeather || isTanooki) anim->startAnim("TauntFeather");
            if ((isFeather || isTanooki) && tauntRightAlt) anim->startAnim("AreaWaitSayCheese");
            if ((isFire || isSuper) && tauntRightAlt) anim->startAnim("TauntSuper");
        }
        if (anim->isAnim("TauntSuper") && al::isStep(player, 14)
        ) {
            if (isFire || isSuper) {
                al::tryEmitEffect(effect, "BonfireSuper", nullptr);
                al::tryStartSe(player, "FireOn");
            }
            if (isSuper) {
                al::tryEmitEffect(player, "InvincibleStart", nullptr);
                al::tryEmitEffect(effect, "ChargeSuper", nullptr);
                al::tryStartSe(player, "StartInvincible");
            }
        }
        if (isBrawl
            && cape && al::isDead(cape)
            && al::isStep(player, 25)
            && anim->isAnim("LandJump3")
        ) {
            cape->appear();
            isCapeActive = 1200;
            al::tryEmitEffect(effect, "AppearBloom", nullptr);
            al::tryStartSe(player, "Bloom");
        }
        if ((isFire || isBrawl || isSuper)
            && anim->isAnim("TauntBrawl")
        ) {
            if (al::isStep(player, 65)) al::tryStartSe(player, "FireOn");
            if (al::isStep(player, 160)
            ) {
                al::tryStopSe(player, "FireOn", -1, nullptr);
                al::tryStartSe(player, "FireOff");
            }
        }
        if (anim->isAnimEnd()
        ) {
            tauntRightAlt = false;
            al::tryDeleteEffect(effect, "BonfireSuper");
            al::tryStopSe(player, "FireOn", -1, nullptr);
            al::setNerve(player, getNerveAt(nrvHakoniwaWait));
            return;
        }
    }
};

PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;
PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;
PlayerActorHakoniwaNrvHammer HammerNrv;
PlayerActorHakoniwaNrvTauntLeft TauntLeftNrv;
PlayerActorHakoniwaNrvTauntRight TauntRightNrv;

struct PlayerActorHakoniwaInitPlayer : public mallow::hook::Trampoline<PlayerActorHakoniwaInitPlayer> {
    static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        Orig(thisPtr, actorInfo, playerInfo);

        // Set Hakoniwa pointer
        isHakoniwa = thisPtr;
        auto* model = thisPtr->mModelHolder->findModelActor("Normal");

        isHammer = new HammerBrosHammer("HammerBrosHammer", model, "PlayerHammer", true);
        al::initCreateActorNoPlacementInfo(isHammer, *actorInfo);

        // Create and hide fireballs
        fireBalls = new al::LiveActorGroup("FireBrosFireBall", 4);
        while (!fireBalls->isFull()) {
            auto* fb = new FireBrosFireBall("FireBall", model);
            al::initCreateActorNoPlacementInfo(fb, *actorInfo);
            fireBalls->registerActor(fb);
        }
        fireBalls->makeActorDeadAll();

        // Check for Super suit costume and cap
        const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
        const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);

        isMario = (costume && al::isEqualString(costume, "Mario"))
            && (cap && al::isEqualString(cap, "Mario"));
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

struct PlayerActorHakoniwaInitAfterPlacement : public mallow::hook::Trampoline<PlayerActorHakoniwaInitAfterPlacement> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        if (fireBalls) fireBalls->makeActorDeadAll();
        if (isHammer) isHammer->makeActorDead();
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

// used in swimming, which also calls tryActionCapSpinAttack before, so just assume isGalaxySpin is properly set up
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

struct PlayerCarryKeeperStartCarry : public mallow::hook::Trampoline<PlayerCarryKeeperStartCarry> {
    static void Callback(PlayerCarryKeeper* thisPtr, al::HitSensor* sensor) {
        // if in hammer nerve block carry start
        if (isHakoniwa
            && isHakoniwa->getNerveKeeper()
            && isHakoniwa->getNerveKeeper()->getCurrentNerve() == &HammerNrv
        ) return;
        
        Orig(thisPtr, sensor);
    }
};

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

struct FireballAttackSensorHook : public mallow::hook::Trampoline<FireballAttackSensorHook> {
    static void Callback(FireBrosFireBall* thisPtr, al::HitSensor* source, al::HitSensor* target) {
        if (!thisPtr || !source || !target) return;

        al::LiveActor* sourceHost = al::getSensorHost(source);
        al::LiveActor* targetHost = al::getSensorHost(target);
        
        if (!sourceHost || !targetHost) return;
        if (targetHost == isHakoniwa) return;

        sead::Vector3f sourcePos = al::getSensorPos(source);
        sead::Vector3f targetPos = al::getSensorPos(target);
        sead::Vector3f spawnPos = (sourcePos + targetPos) * 0.5f;
        spawnPos.y += 20.0f;

        Orig(thisPtr, source, target);

        if(thisPtr && al::isSensorName(source, "AttackHack")
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
                    al::tryStartSe(isHakoniwa, "BlowHit");
                    return;
                }
            }
        }
        Orig(thisPtr, source, target);
    }
};

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        auto* anim   = thisPtr->mAnimator;
        auto* holder = thisPtr->mModelHolder;
        auto* model  = holder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        auto* tail = al::tryGetSubActor(model, "尻尾");
        al::LiveActor* face = al::tryGetSubActor(model, "顔");
        al::IUseEffectKeeper* keeper = static_cast<al::IUseEffectKeeper*>(model);

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

        // Handle life recovery
		static int stillFrames = 0;
		static int healFrames = 0;

        if (isMario || isNoCap) {
            if (GameDataFunction::isPlayerHitPointMax(thisPtr)) { stillFrames = 0; healFrames = 0; }
            else {
                bool still = rs::isOnGround(thisPtr, thisPtr->mCollider) && thisPtr->mInput && !thisPtr->mInput->isMove();

                if (!still) { stillFrames = 0; healFrames = 0; }
                else {
                    if (stillFrames < 120) stillFrames++;
                    int interval = (stillFrames >= 120) ? 60 : 600;

                    if (++healFrames >= interval) { GameDataFunction::recoveryPlayer(thisPtr); healFrames = 0; }
                }
            }
        } else { stillFrames = 0; healFrames = 0; }

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

        if (isBrawl && anim && anim->isAnim("JumpDashFast") && !anim->isAnim("Jump")) anim->startAnim("Jump");
        if (isBrawl && anim && anim->isAnim("WearEnd") && !anim->isAnim("WearEndBrawl")) anim->startAnim("WearEndBrawl");

        if (isSuper && anim && anim->isAnim("WearEnd") && !anim->isAnim("WearEndSuper")) anim->startAnim("WearEndSuper");

        if ((isMario && cape && al::isAlive(cape)) || isFeather || isBrawl || isSuper) {
            if (anim && anim->isAnim("HipDropStart") && !anim->isAnim("HipDropPunchStart")) anim->startAnim("HipDropPunchStart");
            if (anim && anim->isAnim("HipDrop") && !anim->isAnim("HipDropPunch")) anim->startAnim("HipDropPunch");
            if (anim && anim->isAnim("HipDropLand") && !anim->isAnim("HipDropPunchLand")) anim->startAnim("HipDropPunchLand");
            if (anim && anim->isAnim("HipDropReaction") && !anim->isAnim("HipDropPunchReaction")) anim->startAnim("HipDropPunchReaction");

            if (anim && anim->isAnim("SwimHipDropStart") && !anim->isAnim("SwimHipDropPunchStart")) anim->startAnim("SwimHipDropPunchStart");
            if (anim && (anim->isAnim("SwimHipDrop") || anim->isAnim("SwimDive")) && !anim->isAnim("SwimHipDropPunch")) anim->startAnim("SwimHipDropPunch");
            if (anim && anim->isAnim("SwimHipDropLand") && !anim->isAnim("SwimHipDropPunchLand")) anim->startAnim("SwimHipDropPunchLand");

            if (anim && anim->isAnim("LandStiffen") && !anim->isAnim("LandSuper")) anim->startAnim("LandSuper");
            if (anim && anim->isAnim("MofumofuDemoOpening2") && !anim->isAnim("MofumofuDemoOpening2Super")) anim->startAnim("MofumofuDemoOpening2Super");
        } else if (!isTanooki) {
            if (anim && anim->isAnim("JumpDashFast") && !anim->isAnim("JumpDashFastClassic")) anim->startAnim("JumpDashFastClassic");
        }
        
        // Handle Taunt actions
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
            && !al::isNerve(thisPtr, &TauntRightNrv)) al::tryDeleteEffect(keeper, "BonfireSuper");

        // Handle hammer attack
        if (isHammer
            && al::isAlive(isHammer)
            && !al::isNerve(thisPtr, &HammerNrv)
        ) {
            isHammer->makeActorDead();
            al::invalidateHitSensor(isHammer, "AttackHack");
        }

        // Handle fireball attack
        const char* jointName = nextThrowLeft ? "HandL" : "HandR";
        const char* fireAnim  = nextThrowLeft ? "FireL" : "FireR";

        FireBrosFireBall* fireBall = (FireBrosFireBall*) fireBalls->getDeadActor();
        bool isMove = thisPtr->mInput->isMove();
        bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool isWater = al::isInWater(thisPtr);
        bool isSurface = thisPtr->mWaterSurfaceFinder->isFoundSurface();

        bool isFullBody = (!isMove && onGround && (!isWater || isSurface));
        bool isFloating = al::isActionPlaying(model, "GlideFloat")
            || al::isActionPlaying(model, "GlideFloatSuper");

        if (isMario || isFire || isBrawl || isSuper
        ) {
            if (fireStep < 0
                && (canFireball || isFloating)
                && al::isPadTriggerR(-1)
            ) {
                if (fireBall && al::isDead(fireBall)
                ) {
                    fireStep = 0;
                    canFireball = false;

                    anim->startUpperBodyAnim(fireAnim);
                    if (isFullBody) anim->startAnim(fireAnim);
                }
            }
            if (fireStep >= 0
            ) {
                bool isShooting = anim->isUpperBodyAnim("FireL") || anim->isUpperBodyAnim("FireR")
                    || anim->isAnim("FireL") || anim->isAnim("FireR");

                if (!isShooting) { fireStep = -1; return; }
                if (fireStep == 2
                ) {
                    hitBufferCount = 0;

                    sead::Vector3f startPos;
                    al::calcJointPos(&startPos, model, jointName);
                    sead::Vector3f offset(0.0f, 0.0f, 0.0f);
                    
                    if (isSuper) fireBall->shoot(startPos, al::getQuat(model), offset, true, 0, true);
                    else fireBall->shoot(startPos, al::getQuat(model), offset, true, 0, false);
                    al::tryStartSe(thisPtr, "FireBallShoot");

                    nextThrowLeft = !nextThrowLeft;
                }
                if (isFullBody ? anim->isAnimEnd() : anim->isUpperBodyAnimEnd()
                ) {
                    if (isFullBody) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                    anim->clearUpperBodyAnim();
                    fireStep = -1;
                }
                else fireStep++;
            }
        }
        canFireball = false;

        // Apply effects for dash
        bool isMoving =
            al::isActionPlaying(model, "Move")
            || al::isActionPlaying(model, "MoveClassic")
            || al::isActionPlaying(model, "MoveBrawl")
            || al::isActionPlaying(model, "MoveSuper");

        static bool wasDash = false;
        bool isDashNow = al::isPadHoldR(-1)
            && isMoving && !isFireThrowing()
            && al::calcSpeedH(thisPtr) >= 14.0f
            && rs::isOnGround(thisPtr, thisPtr->mCollider);

        if (isDashNow && !wasDash
        ) {
            const char* fx = isSuper ? "AccelSecond" : "Accel";
            if (!al::isEffectEmitting(keeper, fx)) { al::tryStartSe(thisPtr, fx); al::tryEmitEffect(model, fx, nullptr); }
        }
        wasDash = isDashNow;

        // Handle cape logic for Mario/Brawl suit
        bool isGliding =
            al::isActionPlaying(model, "Glide")
            || al::isActionPlaying(model, "GlideAlt")
            || al::isActionPlaying(model, "GlideFloatStart")
            || al::isActionPlaying(model, "JumpBroad8")
            || al::isActionPlaying(model, "JumpBroad8Alt")
            || isFloating;
        
        if ((isMario || isBrawl)
            && cape
        ) {
            if (al::isDead(cape)) isCapeActive = -1;
            else if (!isGliding && isCapeActive > 0) {
                if (--isCapeActive == 0) {
                    cape->kill();
                    al::tryEmitEffect(keeper, "AppearBloom", nullptr);
                    al::tryStartSe(thisPtr, "Bloom");
                    isCapeActive = -1;
                }
            }
        }

        // Handle tail logic for Tanooki suit
        if (isTanooki
            && tail && al::isAlive(tail)
        ) {
            if (isGliding) {
                if (!al::isActionPlaying(tail, "TailSpin")
                ) {
                    al::tryStartAction(tail, "TailSpin");
                    al::tryEmitEffect(keeper, "TailSpin", nullptr);
                    al::tryStartSe(thisPtr, "SpinJumpDownFall");
                }
            } else {
                if (al::isActionPlaying(tail, "TailSpin")
                ) {
                    al::tryStartAction(tail, "Wait");
                    al::tryDeleteEffect(keeper, "TailSpin");
                    al::tryStopSe(thisPtr, "SpinJumpDownFall", -1, nullptr);
                }
            }
        }
        
        // Handle attack and effects for Super suit
        if (isSuper) {
            static bool wasMoveSuper = false;
            const bool isMoveSuper =
                al::calcSpeedH(thisPtr) >= thisPtr->mConst->getDashFastBorderSpeed()
                || anim->isAnim("JumpBroad8") || anim->isAnim("Glide");

            if (isMoveSuper && !wasMoveSuper) { al::validateHitSensor(thisPtr, "GalaxySpin"); hitBufferCount = 0; }
            else if (!isMoveSuper && wasMoveSuper) al::invalidateHitSensor(thisPtr, "GalaxySpin");

            wasMoveSuper = isMoveSuper;
            applyMoonMarioConst(thisPtr->mConst); // force Moon every tick

            // Apply effects for DashFastSuper
            if (al::isPadHoldR(-1) && !isFireThrowing()
                && al::isActionPlaying(model, "MoveSuper")
                && al::calcSpeedH(thisPtr) >= 14.0f) al::tryEmitEffect(model, "DashSuper", nullptr);
            else if (al::isActionPlaying(model, "Glide")
                && !isFireThrowing()) al::tryEmitEffect(model, "DashSuperGlide", nullptr);
            else {
                al::tryDeleteEffect(model, "DashSuper");
                al::tryDeleteEffect(model, "DashSuperGlide");
            }

            // Apply effects for invincibility
            PlayerDamageKeeper* damagekeep = thisPtr->mDamageKeeper;
            bool damageBlink = damagekeep && damagekeep->mIsPreventDamage && (damagekeep->mRemainingInvincibility > 0);
            bool hacked = thisPtr->mHackKeeper && thisPtr->mHackKeeper->mCurrentHackActor;

            if (!hacked && (!al::isHideModel(model) || damageBlink)
            ) {
                al::tryEmitEffect(keeper, "Bonfire", nullptr);
                if (!damagekeep->mIsPreventDamage) {
                    damagekeep->activatePreventDamage();
                    damagekeep->mRemainingInvincibility = INT_MAX;
                }
            } else {
                if (hacked || !damageBlink) {
                    al::tryDeleteEffect(keeper, "Bonfire");
                    damagekeep->mRemainingInvincibility = 0;
                }
            }
        }
    }
};

struct LiveActorMovementHook : public mallow::hook::Trampoline<LiveActorMovementHook> {
    static void Callback(al::LiveActor* actor) {
        Orig(actor);

        static bool hammerEffect = false;

        if (actor != isHammer) return;

        if (!al::isAlive(isHammer)
        ) { 
            hammerEffect = false;
            return;
        }

        al::HitSensor* sensorHammer = al::getHitSensor(isHammer, "AttackHack");
        if (!sensorHammer || !sensorHammer->mIsValid) return;

        if (auto* sensorWall = al::tryGetCollidedWallSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorWall);
        if (auto* sensorCeiling = al::tryGetCollidedCeilingSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorCeiling);
        if (auto* sensorGround = al::tryGetCollidedGroundSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorGround);

        if (!hammerEffect
            && isHakoniwa->mAnimator->isAnim("HammerAttack")
            && al::isCollidedGround(isHammer)
        ) {
            al::tryEmitEffect(isHakoniwa, "HammerLandHit", nullptr);
            al::tryStartSe(isHakoniwa, "HammerLandHit");
            hammerEffect = true;
        }
    }
};

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

struct PlayerActorHakoniwaExeJump : public mallow::hook::Trampoline<PlayerActorHakoniwaExeJump> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        auto* anim = thisPtr->mAnimator;
        auto* model = thisPtr->mModelHolder->findModelActor("Normal");
        auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

        bool wasGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool wasWater = al::isInWater(thisPtr);

        Orig(thisPtr);

        if (!isBrawl) return;

        bool isGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool isWater = al::isInWater(thisPtr);
        bool isAir = !isGround && !isWater;

        if (wasWater || (wasGround && isAir)
        ) { 
            isDoubleJump = false; 
            isDoubleJumpConsume = false;
        }
        if (isAir && !isDoubleJump
            && (al::isPadTriggerA(-1) || al::isPadTriggerB(-1))
        ) {
            isDoubleJump = true;
            isDoubleJumpConsume = true;
            //if (isFeather || isTanooki) { al::tryEmitEffect(keeper, "AppearBloom", nullptr); al::tryStartSe(thisPtr, "Bloom"); }
            if (isBrawl) al::tryEmitEffect(keeper, "DoubleJump", nullptr);
            al::setNerve(thisPtr, getNerveAt(nrvHakoniwaJump));
        }
        if (isDoubleJumpConsume
            && al::isFirstStep(thisPtr)
        ) {
            //if (isFeather || isTanooki) anim->startAnim("JumpDashFast");
            if (isBrawl) anim->startAnim("PoleHandStandJump");
            isDoubleJumpConsume = false;
        }
    }
};

struct PlayerStateJumpTryCountUp : public mallow::hook::Trampoline<PlayerStateJumpTryCountUp> {
    static void Callback(PlayerStateJump* state, PlayerContinuousJump* cont) {
        if (isBrawl) return;

        Orig(state, cont);
    }
};

struct PlayerActorHakoniwaExeHeadSliding : public mallow::hook::Trampoline<PlayerActorHakoniwaExeHeadSliding> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);
                
        auto* anim   = thisPtr->mAnimator;
        auto* model = thisPtr->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

        if (!isMario && !isFeather && !isTanooki && !isBrawl && !isSuper) return;

        float vy = al::getVelocity(thisPtr).y;
        if (vy < -2.5f) al::setVelocityY(thisPtr, -2.5f);

        float speed = al::calcSpeed(thisPtr);

        const char* jumpBroadAnim = isTanooki ? "JumpBroad8Alt" : "JumpBroad8";
        const char* glideAnim = isTanooki ? "GlideAlt" : "Glide";
        const char* glideFloatAnim = isSuper ? "GlideFloatSuper" : "GlideFloat";

        if (al::isFirstStep(thisPtr)
        ) {
            if ((isMario || isBrawl) 
                && cape && al::isDead(cape)
            ) {
                cape->appear();
                al::tryEmitEffect(keeper, "AppearBloom", nullptr);
                al::tryStartSe(thisPtr, "Bloom");
            }
            anim->startAnim(jumpBroadAnim);
        }
        else if (anim->isAnimEnd() && anim->isAnim(jumpBroadAnim)) anim->startAnim(glideAnim);
        else if (speed < 10.f) {
            if (anim->isAnim(glideAnim)) anim->startAnim("GlideFloatStart");
            if (anim->isAnimEnd() && anim->isAnim("GlideFloatStart")) anim->startAnim(glideFloatAnim);
        }
        if (al::isGreaterStep(thisPtr, 25)
        ) {
            if (al::isPadTriggerA(-1)
                || al::isPadTriggerB(-1)
            ) {
                if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaFall))) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
            }

            if (al::isPadTriggerZL(-1)
                || al::isPadTriggerZR(-1)
            ) {
                if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop))) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop));
            }
            if (isPadTriggerGalaxySpin(-1)
            ) {
                if (!al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))
                ) {
                    canGalaxySpin = true;
                    canStandardSpin = true;
                    isGalaxyAfterStandardSpin = false;
                    isStandardAfterGalaxySpin = false;

                    triggerGalaxySpin = true;
                    al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                }
            }
            else if (al::isPadTriggerX(-1) || al::isPadTriggerY(-1)
            ) {
                if (!al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))
                ) {
                    canGalaxySpin = true;
                    canStandardSpin = true;
                    isGalaxyAfterStandardSpin = false;
                    isStandardAfterGalaxySpin = false;

                    triggerGalaxySpin = false;
                    al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                }
            }
        }
    }
};

struct PlayerHeadSlidingKill : public mallow::hook::Trampoline<PlayerHeadSlidingKill> {
    static void Callback(PlayerStateHeadSliding * state) {
        isCapeActive = 1200;
        if (state->mAnimator) state->mAnimator->clearUpperBodyAnim();
        Orig(state);
    }
};

struct PlayerConstGetHeadSlidingSpeed : public mallow::hook::Trampoline<PlayerConstGetHeadSlidingSpeed> {
    static float Callback(const PlayerConst* thisPtr) {
        float speed = Orig(thisPtr);

        if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return speed;
        if (isSuper) speed *= 1.5f;
        return speed;
    }
};

struct PlayerInputFunctionIsHoldAction : public mallow::hook::Trampoline<PlayerInputFunctionIsHoldAction> {
    static bool Callback(const al::LiveActor* actor, s32 port) {
        bool isFlying = isHakoniwa && isHakoniwa->mHackCap && isHakoniwa->mHackCap->isFlying();

        return Orig(actor, port) || (al::isPadHoldR(port) && !isFlying);
    }
};

struct PlayerActionGroundMoveControlUpdate : public mallow::hook::Trampoline<PlayerActionGroundMoveControlUpdate> {
    static float Callback(PlayerActionGroundMoveControl* thisPtr) {
        float update = Orig(thisPtr);

        if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return update;
        PlayerConst* playerConst = const_cast<PlayerConst*>(thisPtr->mConst);

        bool isDash = al::isPadHoldR(-1) && !isFireThrowing();

        if (isSuper && isDash
        ) {
            playerConst->mNormalMaxSpeed = 28.0f;
            thisPtr->mMaxSpeed = 28.0f;
        }
        else if (isDash
        ) {
            playerConst->mNormalMaxSpeed = 21.0f;
            thisPtr->mMaxSpeed = 21.0f;
        }
        else {
            playerConst->mNormalMaxSpeed = 14.0f;
            thisPtr->mMaxSpeed = 14.0f;
        }
        return update;
    }
};

struct PlayerAnimControlRunUpdate : public mallow::hook::Inline<PlayerAnimControlRunUpdate> {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return;
        if (isSuper) *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("MoveSuper"); //mMoveAnimName in PlayerAnimControlRun
        else if (isBrawl) *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("MoveBrawl");
        else if (isFeather || isTanooki) *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("Move");
        else *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("MoveClassic");
    }
};

struct PlayerSeCtrlUpdateMove : public mallow::hook::Inline<PlayerSeCtrlUpdateMove> {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return;
        if (isSuper) ctx->X[8] = reinterpret_cast<u64>("MoveSuper");
        else if (isBrawl) ctx->X[8] = reinterpret_cast<u64>("MoveBrawl");
        else if (isFeather || isTanooki) ctx->X[8] = reinterpret_cast<u64>("Move");
        else ctx->X[8] = reinterpret_cast<u64>("MoveClassic");
    }
};

struct PlayerSeCtrlUpdateWearEnd : public mallow::hook::Inline<PlayerSeCtrlUpdateWearEnd> {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (isBrawl) ctx->X[20] = reinterpret_cast<u64>("WearEndBrawl");
        if (isSuper) ctx->X[20] = reinterpret_cast<u64>("WearEndSuper");
    }
};

// Handling running on water while Super
bool isSuperRunningOnSurface = false;
const f32 MIN_SPEED_RUN_ON_WATER = 15.0f;

struct StartWaterSurfaceRunJudge : public mallow::hook::Trampoline<StartWaterSurfaceRunJudge> {
    static bool Callback(const PlayerJudgeStartWaterSurfaceRun* thisPtr) {
        if (isSuper) {
            return thisPtr->mWaterSurfaceFinder->isFoundSurface()
                && al::isNearZeroOrGreater(thisPtr->mWaterSurfaceFinder->getDistance())
                && al::getGravity(thisPtr->mPlayer).dot(al::getVelocity(thisPtr->mPlayer)) >= 0.0f
                && al::calcSpeedH(thisPtr->mPlayer) >= MIN_SPEED_RUN_ON_WATER;
        }
        else {
            return Orig(thisPtr);
        }
    }
};

struct WaterSurfaceRunJudge : public mallow::hook::Trampoline<WaterSurfaceRunJudge> {
    static bool Callback(const PlayerJudgeWaterSurfaceRun* thisPtr) {
        isSuperRunningOnSurface = false;

        if (isSuper) {
            bool result = thisPtr->mWaterSurfaceFinder->isFoundSurface()
                && al::isNearZeroOrGreater(thisPtr->mWaterSurfaceFinder->getDistance())
                && al::calcSpeedH(thisPtr->mPlayer) >= MIN_SPEED_RUN_ON_WATER;
            isSuperRunningOnSurface = result;
            return result;
        }
        else {
            return Orig(thisPtr);
        }
    }
};

struct RunWaterSurfaceDisableSink : public mallow::hook::Inline<RunWaterSurfaceDisableSink> {
    static void Callback(exl::hook::InlineCtx* ctx) {
        // move value > 0 into W8 to cause skipping the "sink" part
        if (isSuperRunningOnSurface) ctx->W[8] = 1;
    }
};

// Without this, entering deep water without shallow water first will cause slowing down the player
// Until complete stop without properly starting to run on water, no idea why this works, but it does
struct WaterSurfaceRunDisableSlowdown : public mallow::hook::Inline<WaterSurfaceRunDisableSlowdown> {
    static void Callback(exl::hook::InlineCtx* ctx) {
        // used to redirect `turnVecToVecRate` into this location instead of the proper one, so the call is basically ignored
        static sead::Vector3f garbageVec;
        if (isSuperRunningOnSurface) ctx->X[0] = reinterpret_cast<u64>(&garbageVec);
    }
};

struct RsIsTouchDeadCode : public mallow::hook::Trampoline<RsIsTouchDeadCode> {
    static bool Callback(const al::LiveActor* actor, const IUsePlayerCollision* coll, const IPlayerModelChanger* changer, const IUseDimension* dim, float f) {
        if (isSuper) return false;
        return Orig(actor, coll, changer, dim, f);
    }
};

struct RsIsTouchDamageFireCode : public mallow::hook::Trampoline<RsIsTouchDamageFireCode> {
    static bool Callback(const al::LiveActor* actor, const IUsePlayerCollision* coll, const IPlayerModelChanger* changer) {
        if (isSuper) return false;
        return Orig(actor, coll, changer);
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    mallow::init::installHooks();
    // Modify triggers
    InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");
    InputIsTriggerActionCameraResetHook::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");
    TriggerCameraReset::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");

    // Initialize player actor
    PlayerActorHakoniwaInitPlayer::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");
    PlayerActorHakoniwaInitAfterPlacement::InstallAtSymbol("_ZN19PlayerActorHakoniwa18initAfterPlacementEv");

    // Change Mario's idle
    PlayerStateWaitExeWait::InstallAtSymbol("_ZN15PlayerStateWait7exeWaitEv");
    
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

    // Allow carrying an object during a GalaxySpin
    PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);
    PlayerCarryKeeperIsCarryDuringSwimSpin::InstallAtOffset(0x489EE8);
    PlayerCarryKeeperStartThrowNoSpin::InstallAtSymbol("_ZN17PlayerCarryKeeper10startThrowEb");
    PlayerCarryKeeperStartCarry::InstallAtSymbol("_ZN17PlayerCarryKeeper10startCarryEPN2al9HitSensorE");

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

    // Handles attack sensors
    HackCapAttackSensorHook::InstallAtSymbol("_ZN7HackCap12attackSensorEPN2al9HitSensorES2_");
    PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");
    FireballAttackSensorHook::InstallAtSymbol("_ZN16FireBrosFireBall12attackSensorEPN2al9HitSensorES2_");
    HammerAttackSensorHook::InstallAtSymbol("_ZN16HammerBrosHammer12attackSensorEPN2al9HitSensorES2_");

    // Handles control/movement
    //PlayerControlHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa7controlEv");
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
    LiveActorMovementHook::InstallAtSymbol("_ZN2al9LiveActor8movementEv");

    // Allow triggering spin on roll and squat
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");
    PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");

    // Handles Double Jump
    PlayerActorHakoniwaExeJump::InstallAtSymbol("_ZN19PlayerActorHakoniwa7exeJumpEv");
    PlayerStateJumpTryCountUp::InstallAtSymbol("_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump");
    
    // Handles Glide
    PlayerActorHakoniwaExeHeadSliding::InstallAtSymbol("_ZN19PlayerActorHakoniwa14exeHeadSlidingEv");
    PlayerHeadSlidingKill::InstallAtSymbol("_ZN22PlayerStateHeadSliding4killEv");
    PlayerConstGetHeadSlidingSpeed::InstallAtSymbol("_ZNK11PlayerConst19getHeadSlidingSpeedEv");

    // Handle Dash and WearEnd
    PlayerInputFunctionIsHoldAction::InstallAtSymbol("_ZN19PlayerInputFunction12isHoldActionEPKN2al9LiveActorEi");
    PlayerActionGroundMoveControlUpdate::InstallAtSymbol("_ZN29PlayerActionGroundMoveControl6updateEv");
    PlayerAnimControlRunUpdate::InstallAtOffset(0x42C6BC);
    PlayerSeCtrlUpdateMove::InstallAtOffset(0x463038);
    PlayerSeCtrlUpdateWearEnd::InstallAtOffset(0x463DE0);

    // Handle running on water
    StartWaterSurfaceRunJudge::InstallAtSymbol("_ZNK31PlayerJudgeStartWaterSurfaceRun5judgeEv");
    WaterSurfaceRunJudge::InstallAtSymbol("_ZNK26PlayerJudgeWaterSurfaceRun5judgeEv");
    RunWaterSurfaceDisableSink::InstallAtOffset(0x48023C);
    WaterSurfaceRunDisableSlowdown::InstallAtOffset(0x4184C0);
    RsIsTouchDeadCode::InstallAtSymbol("_ZN2rs15isTouchDeadCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChangerPK13IUseDimensionf");
    RsIsTouchDamageFireCode::InstallAtSymbol("_ZN2rs21isTouchDamageFireCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChanger");

    // Disable invincibility music patches
    exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
    invincibleStartPatcher.WriteInst(0x1F2003D5); // NOP
    exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
    invinciblePatcher.WriteInst(0x1F2003D5); // NOP

    // Remove Cappy eyes while ide
    exl::patch::CodePatcher eyePatcher(0x41F7E4);
    eyePatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W0, 0));
}