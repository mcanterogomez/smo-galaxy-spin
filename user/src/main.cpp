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

// Mod‑specific & custom actors
#include "actors/custom/CustomPlayerConst.h"
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
        case 'R':
            return al::isPadTriggerR(port);
        case 'X':
            return al::isPadTriggerX(port);
        case 'Y':
        default:
            return al::isPadTriggerY(port);
    }
}

struct PadTriggerYHook : public mallow::hook::Trampoline<PadTriggerYHook> {
    static bool Callback(int port) {
        
        if(port == 100) return Orig(-1);

        return false;
    };
};

struct InputIsTriggerActionXexclusivelyHook : public mallow::hook::Trampoline<InputIsTriggerActionXexclusivelyHook> {
    static bool Callback(const al::LiveActor* actor, int port) {
        if(port == 100)
            return Orig(actor, PlayerFunction::getPlayerInputPort(actor));
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
            case 'R':
                return al::isPadTriggerL(port);
        }
        return Orig(actor, port);
    }
};

al::LiveActor* hitBuffer[0x40];
int hitBufferCount = 0;

const uintptr_t spinCapNrvOffset = 0x1d78940;
const uintptr_t nrvSpinCapFall = 0x1d7ff70;
const uintptr_t nrvHakoniwaWait = 0x01D78918;
//const uintptr_t nrvHakoniwaSquat = 0x01D78920;
const uintptr_t nrvHakoniwaFall = 0x01d78910;
const uintptr_t nrvHakoniwaHipDrop = 0x1D78978;
const uintptr_t nrvHakoniwaJump = 0x1D78948;

bool isGalaxySpin = false;
bool canGalaxySpin = true;
bool canStandardSpin = true;
bool isGalaxyAfterStandardSpin = false;  // special case, as switching between spins resets isGalaxySpin and canStandardSpin
bool isStandardAfterGalaxySpin = false;
int galaxyFakethrowRemainder = -1;  // -1 = inactive, -2 = request to start, positive = remaining frames
bool triggerGalaxySpin = false;
bool prevIsCarry = false;

int galaxySensorRemaining = -1;

bool isPunching = false; // Global flag to track punch state
bool isPunchRight = false;

bool isSpinActive = false; // Global flag to track spin state
bool isNearCollectible = false; // Global flag to track if near a collectible
bool isNearTreasure = false; // Global flag to track if near a collectible
bool isNearSwoonedEnemy = false;  // Global flag to track if near a swooned enemy

// Global flags to track states
bool isMario = false;
bool isBrawl = false;
bool isFeather = false;
bool isSuper = false;

static PlayerActorHakoniwa* isHakoniwa = nullptr; // Global pointer for Hakoniwa

bool tauntRightAlt = false; // Global flag to alternate taunt direction
bool isDoubleJump = false; // Global flag to track double jump state
int isCapeActive = -1; // Global flag to track cape state

struct PlayerActorHakoniwaInitPlayer : public mallow::hook::Trampoline<PlayerActorHakoniwaInitPlayer> {
    static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        Orig(thisPtr, actorInfo, playerInfo);

        // Set Hakoniwa pointer
        isHakoniwa = thisPtr;

        // Check for Super suit costume and cap
        const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
        const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);

        isMario = (costume && al::isEqualString(costume, "Mario"))
                && (cap && al::isEqualString(cap, "Mario"));
        isBrawl = (costume && al::isEqualString(costume, "MarioColorBrawl"))
                && (cap && al::isEqualString(cap, "MarioColorBrawl"));
        isFeather = (costume && al::isEqualString(costume, "MarioFeather"));
        isSuper = (costume && al::isEqualString(costume, "MarioColorSuper"))
                && (cap && al::isEqualString(cap, "MarioColorSuper"));
    }
};

struct PlayerStateWaitExeWait : public mallow::hook::Trampoline<PlayerStateWaitExeWait> {
    static void Callback(PlayerStateWait* state) {
        Orig(state);

        if (!isBrawl && !isSuper)
        return;

        if (al::isFirstStep(state)) {
            const char* special = nullptr;
            if (state->tryGetSpecialStatusAnimName(&special)) {
                if (al::isEqualString(special, "BattleWait")
                ) {
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

    if (isPadTriggerGalaxySpin(-1)
        && !rs::is2D(player)
        && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)
    ) {
        if (player->mAnimator->isAnim("SpinSeparate")
        || player->mAnimator->isAnim("SpinAttackLeft")
        || player->mAnimator->isAnim("SpinAttackRight")
        || player->mAnimator->isAnim("SpinAttackAirLeft")
        || player->mAnimator->isAnim("SpinAttackAirRight")
        || player->mAnimator->isAnim("KoopaCapPunchL")
        || player->mAnimator->isAnim("KoopaCapPunchR")
        || player->mAnimator->isAnim("RabbitGet")) return -1;

        if (canGalaxySpin) triggerGalaxySpin = true;
        else { triggerGalaxySpin = true; galaxyFakethrowRemainder = -2; }
        return 1;
    }
    return 0; // fallthrough to Orig
}

static inline bool TryCapSpinPost(bool origRet) {
    if (origRet) { triggerGalaxySpin = false; return true; }
    return false;
}

struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack> {
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        int pre = TryCapSpinPre(player);
        if (pre != 0) return pre > 0;
        return TryCapSpinPost(Orig(player, a2));
    }
};

struct PlayerTryActionCapSpinAttackBindEnd : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttackBindEnd> {
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        int pre = TryCapSpinPre(player);
        if (pre != 0) return pre > 0;
        return TryCapSpinPost(Orig(player, a2));
    }
};

class PlayerStateSpinCapNrvGalaxySpinGround : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
        PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);
        
        bool isCarrying = player->mCarryKeeper->isCarry();
        bool isRotatingL = state->mAnimator->isAnim("SpinGroundL");
        bool isRotatingR = state->mAnimator->isAnim("SpinGroundR");
        bool didSpin = player->mInput->isSpinInput();
        int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;
        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");

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

                } else if (isRotatingL) {
                    state->mAnimator->startSubAnim("SpinAttackLeft");
                    state->mAnimator->startAnim("SpinAttackLeft");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    //galaxySensorRemaining = 41;

                } else if (isRotatingR) {
                    state->mAnimator->startSubAnim("SpinAttackRight");
                    state->mAnimator->startAnim("SpinAttackRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    //galaxySensorRemaining = 41;

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
                    // only Spin Attack
                    state->mAnimator->startSubAnim("SpinSeparate");
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;

                    // only Punch Attack
                    /*if (isPunchRight) {
                        state->mAnimator->startSubAnim("KoopaCapPunchRStart");
                        state->mAnimator->startAnim("KoopaCapPunchR");
                    } else {
                        state->mAnimator->startSubAnim("KoopaCapPunchLStart");
                        state->mAnimator->startAnim("KoopaCapPunchL");
                    }
                    // Make winding up invincible
                    al::invalidateHitSensor(state->mActor, "Foot");
                    al::invalidateHitSensor(state->mActor, "Body");
                    al::invalidateHitSensor(state->mActor, "Head");

                    isPunching = true; // Validate punch animations*/
                }
            }
        }
        
        if (!isSpinning && !isCarrying
            && !isNearCollectible && !isNearTreasure && !isNearSwoonedEnemy
            && !isRotatingL && !isRotatingR
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
                    
        state->updateSpinGroundNerve();

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

        // true only while that exact anim is playing
        bool isRotatingAirL  = state->mAnimator->isAnim("StartSpinJumpL")
                            || state->mAnimator->isAnim("RestartSpinJumpL");
        bool isRotatingAirR  = state->mAnimator->isAnim("StartSpinJumpR")
                            || state->mAnimator->isAnim("RestartSpinJumpR");
        bool didSpin = player->mInput->isSpinInput();
        int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;
        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");

        isSpinActive = true;
        
        if(al::isFirstStep(state)
        ) {
            if (!isSpinning) {
                if (didSpin) {
                    if (spinDir > 0) {
                        state->mAnimator->startAnim("SpinAttackAirLeft");
                    }
                    else {
                        state->mAnimator->startAnim("SpinAttackAirRight");
                    }
                    al::validateHitSensor(state->mActor, "DoubleSpin");

                } else if (isRotatingAirL) {
                    //state->mAnimator->startSubAnim("SpinAttackRight");
                    state->mAnimator->startAnim("SpinAttackAirLeft");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    //galaxySensorRemaining = 41;

                } else if (isRotatingAirR) {
                    //state->mAnimator->startSubAnim("SpinAttackAirRight");
                    state->mAnimator->startAnim("SpinAttackAirRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    //galaxySensorRemaining = 41;

                } else {
                    //state->mAnimator->startSubAnim("SpinSeparate");
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                }
            }
        }
        
        state->updateSpinAirNerve();

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

PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;
PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;

class PlayerActorHakoniwaNrvTauntLeft : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* anim = player->mAnimator;

        if (al::isFirstStep(player)
        ) {
            anim->endSubAnim();
            anim->startAnim("WearEnd");
            if (isBrawl) anim->startAnim("WearEndBrawl");
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
        auto* model = player->mModelHolder->findModelActor("Mario");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        auto* effect = static_cast<al::IUseEffectKeeper*>(model);

        if (al::isFirstStep(player)
        ) {
            anim->endSubAnim();
            anim->startAnim("TauntMario");
            if (tauntRightAlt) anim->startAnim("AreaWait64");
            if (isBrawl) anim->startAnim("TauntBrawl");
            if (isBrawl && tauntRightAlt) anim->startAnim("LandJump3");
            if (isFeather) anim->startAnim("TauntFeather");
            if (isFeather && tauntRightAlt) anim->startAnim("AreaWaitSayCheese");
            if (isSuper) anim->startAnim("TauntSuper");
        }
        if (isSuper && anim->isAnim("TauntSuper") && al::isStep(player, 14)
        ) {
            al::tryEmitEffect(player, "InvincibleStart", nullptr);
            al::tryEmitEffect(effect, "BonfireSuper", nullptr);
            al::tryEmitEffect(effect, "ChargeSuper", nullptr);
            al::tryStartSe(player, "StartInvincible");
            al::tryStartSe(player, "FireOn");
        }
        if (isBrawl
            && anim->isAnim("LandJump3")
            && cape && al::isDead(cape)
            && al::isStep(player, 25)
        ) {
            cape->appear();
            isCapeActive = 1200;
            al::tryEmitEffect(effect, "AppearBloom", nullptr);
            al::tryStartSe(player, "Bloom");
        }
        if (isBrawl && anim->isAnim("TauntBrawl") && al::isStep(player, 65))
            al::tryStartSe(player, "FireOn");
        if (isBrawl && anim->isAnim("TauntBrawl") && al::isStep(player, 160)
        ) {
            al::tryStopSe(player, "FireOn", -1, nullptr);
            al::tryStartSe(player, "FireOff");
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

PlayerActorHakoniwaNrvTauntLeft TauntLeftNrv;
PlayerActorHakoniwaNrvTauntRight TauntRightNrv;

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
                    -al::getGravity(state->mActor),
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
        canStandardSpin = true;
        canGalaxySpin = true;
        galaxyFakethrowRemainder = -1; 
        isPunching = false;
        isSpinActive = false;
        isNearCollectible = false;
        isNearTreasure = false;
        isNearSwoonedEnemy = false;

        al::invalidateHitSensor(state->mActor, "DoubleSpin");
        al::invalidateHitSensor(state->mActor, "GalaxySpin");
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
        bool isSpinSeparate = state->mAnimator->isAnim("SpinSeparate");

        // Allow canceling only if Mario is in the SpinSeparate move
        return Orig(state) || (al::isNerve(state, &GalaxySpinGround) && isSpinSeparate && al::isGreaterStep(state, 10));
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

struct PlayerStateSwimExeSwimHipDropHeadSliding : public mallow::hook::Trampoline<PlayerStateSwimExeSwimHipDropHeadSliding> {
    static void Callback(PlayerStateSwim* thisPtr) {
        Orig(thisPtr);
        if(isPadTriggerGalaxySpin(-1))
            if(((PlayerActorHakoniwa*)thisPtr->mActor)->tryActionCapSpinAttackImpl(true))
                thisPtr->startCapThrow();
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
        if(!isGalaxySpin && !triggerGalaxySpin) {
            Orig(thisPtr, animator);
            return;
        }

        if (isNearCollectible) animator->startAnim("RabbitGet");
        else if (isNearTreasure || isNearSwoonedEnemy) animator->startAnim("Kick");
        else animator->startAnim("SpinSeparateSwim");
    }
};

struct PlayerSpinCapAttackStartSpinSeparateSwim : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwim> {
    static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
        if(!isGalaxySpin && !triggerGalaxySpin) {
            Orig(thisPtr, animator);
            return;
        }

        if (isNearCollectible) animator->startAnim("RabbitGet");
        else if (isNearTreasure || isNearSwoonedEnemy) animator->startAnim("Kick");
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

    if(isGalaxySpin) {  // currently in GalaxySpin
        bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can only succeed for standard throw
        if(!trySpin)
            return;

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
    else {  // currently in standard spin
        bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can succeed for GalaxySpin and fakethrow
        if(!trySpin)
            return;

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

struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {

        if (!thisPtr || !source || !target) return;

        if (!al::isSensorName(source, "GalaxySpin")
            && !al::isSensorName(source, "Punch")
            && !al::isSensorName(source, "DoubleSpin")
        ) {
            Orig(thisPtr, source, target);
            return;
        }

        al::LiveActor* sourceHost = al::getSensorHost(source);
        al::LiveActor* targetHost = al::getSensorHost(target);

        if (!sourceHost || !targetHost) return;

        sead::Vector3f sourcePos = al::getSensorPos(source);
        sead::Vector3f targetPos = al::getSensorPos(target);
        sead::Vector3f spawnPos = (sourcePos + targetPos) * 0.5f;
        spawnPos.y += 20.0f;

        sead::Vector3 fireDir = al::getTrans(targetHost) - al::getTrans(sourceHost);
        fireDir.normalize();
   
        if (al::isActionPlaying(thisPtr->mModelHolder->findModelActor("Mario"), "MoveSuper")
            && al::isEqualSubString(typeid(*targetHost).name(), "FireBall")) return;
            
        if((al::isSensorName(source, "GalaxySpin")
            || al::isSensorName(source, "Punch")
            || al::isSensorName(source, "DoubleSpin"))
            && thisPtr->mAnimator
            && (al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinSeparate")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchR")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchL")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackLeft")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackRight")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackAirLeft")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackAirRight")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "RabbitGet")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "Rolling")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "RollingStart")
                || al::isActionPlaying(thisPtr->mModelHolder->findModelActor("Mario"), "MoveSuper")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "JumpBroad8")
                || al::isEqualString(thisPtr->mAnimator->mCurAnim, "CapeGlide")
                || isGalaxySpin)
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

                if (al::isSensorName(source, "Punch")
                    && !isPunching
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
            if (al::isSensorName(source, "GalaxySpin")
                || al::isSensorName(source, "DoubleSpin")
            ) {
                if (al::isEqualSubString(typeid(*targetHost).name(), "BlockQuestion")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BlockBrick")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BossForestBlock")
                ) {
                    rs::sendMsgHammerBrosHammerHackAttack(target, source);
                    return;
                }
            }
            if(!isInHitBuffer) {
                if (al::isEqualSubString(typeid(*targetHost).name(), "BlockHard")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BossForestBlock")
                    || al::isEqualSubString(typeid(*targetHost).name(), "GolemClimb")
                    || al::isEqualSubString(typeid(*targetHost).name(), "MarchingCubeBlock")
                ) {
                    if (rs::sendMsgHammerBrosHammerHackAttack(target, source)) hitBuffer[hitBufferCount++] = targetHost;
                    return;
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "BreakMapParts")
                    || al::isEqualSubString(typeid(*targetHost).name(), "BreakableWall")
                    || al::isEqualSubString(typeid(*targetHost).name(), "CatchBomb")
                    || al::isEqualSubString(typeid(*targetHost).name(), "DamageBall")
                    || al::isEqualSubString(typeid(*targetHost).name(), "KickStone")
                    || al::isEqualSubString(typeid(*targetHost).name(), "MoonBasement")
                    || al::isEqualSubString(typeid(*targetHost).name(), "PlayGuideBoard")
                    || (al::isEqualSubString(typeid(*targetHost).name(), "SignBoard")
                        && !al::isModelName(targetHost, "SignBoardNormal"))
                    || (al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                        && al::isModelName(targetHost, "TreasureBoxWood"))
                ) {
                    if (al::sendMsgExplosion(target, source, nullptr)
                        || rs::sendMsgKoopaHackPunch(target, source)
                        || rs::sendMsgKoopaHackPunchCollide(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        al::tryEmitEffect(sourceHost, "Hit", &spawnPos);
                    }
                    return;
                }
                if (al::isEqualSubString(typeid(*targetHost).name(), "BreedaWanwan")
                    || al::isEqualSubString(typeid(*targetHost).name(), "TRex")
                ) {
                    if (al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || al::sendMsgPlayerHipDrop(target, source, nullptr)) hitBuffer[hitBufferCount++] = targetHost;
                    return;
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
                    && !al::isSensorName(target,"Brake"))
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
                        || rs::sendMsgCapTouchWall(target, source, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})) hitBuffer[hitBufferCount++] = targetHost;
                    return;
                }
                if (al::isSensorNpc(target)
                    || al::isSensorRide(target)
                ) {
                    if (al::sendMsgPlayerSpinAttack(target, source, nullptr)
                        || rs::sendMsgCapReflect(target, source)
                        || rs::sendMsgCapAttack(target, source)
                        || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
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
                        return;
                    }
                }
                if (al::isSensorMapObj(target)
                    && !al::isEqualSubString(typeid(*targetHost).name(), "HipDrop")
                    && !al::isEqualSubString(typeid(*targetHost).name(), "TreasureBox")
                ) {
                    if (rs::sendMsgHackAttack(target, source)
                        || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                        || rs::sendMsgCapReflect(target, source)
                        || al::sendMsgPlayerHipDrop(target, source, nullptr)
                        || rs::sendMsgCapAttack(target, source)
                        || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                        || rs::sendMsgCapItemGet(target, source)
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        return;
                    }
                }
            }
        }
        Orig(thisPtr, source, target);
    }
};

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        al::HitSensor* sensorSpin = al::getHitSensor(thisPtr, "GalaxySpin");
        al::HitSensor* sensorDoubleSpin = al::getHitSensor(thisPtr, "DoubleSpin");
        al::HitSensor* sensorPunch = al::getHitSensor(thisPtr, "Punch");

        if (sensorSpin && sensorSpin->mIsValid)
            thisPtr->attackSensor(sensorSpin, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
        
        if (sensorDoubleSpin && sensorDoubleSpin->mIsValid)
            thisPtr->attackSensor(sensorDoubleSpin, rs::tryGetCollidedWallSensor(thisPtr->mCollider));

        if (sensorPunch && sensorPunch->mIsValid)
            thisPtr->attackSensor(sensorPunch, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
        
        if(galaxySensorRemaining > 0) {
            galaxySensorRemaining--;
            if(galaxySensorRemaining == 0) {
                al::invalidateHitSensor(thisPtr, "GalaxySpin");
                //al::invalidateHitSensor(thisPtr, "DoubleSpin");
                //al::invalidateHitSensor(thisPtr, "Punch");
                isGalaxySpin = false;
                galaxySensorRemaining = -1;
            }
        }

        // Grab model for effects
        auto* anim   = thisPtr->mAnimator;
        auto* holder = thisPtr->mModelHolder;
        auto* model  = holder->findModelActor("Mario");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        al::LiveActor* face = al::tryGetSubActor(model, "顔");
        al::IUseEffectKeeper* keeper = static_cast<al::IUseEffectKeeper*>(model);

        // Add attack to moves
        if (model) {
            static bool wasMoveRoll = false;
            const bool isMoveRoll = (al::isActionPlaying(model, "RollingStart") || anim->isAnim("Rolling"));

            if (isMoveRoll && !wasMoveRoll) { al::validateHitSensor(thisPtr, "GalaxySpin"); hitBufferCount = 0;}
            else if (!isMoveRoll && wasMoveRoll) al::invalidateHitSensor(thisPtr, "GalaxySpin");

            wasMoveRoll = isMoveRoll;
        }

        // Change face
        if ((isBrawl || isSuper)
        ) {
            if (face && !al::isActionPlayingSubActor(model, "顔", "WaitAngry"))
                al::startActionSubActor(model, "顔", "WaitAngry");

            // Activate properties for when Super
            if (isSuper) {
                static bool wasMoveSuper = false;
                const bool isMoveSuper =
                    model
                    && ((al::isActionPlaying(model, "MoveSuper")
                        && al::calcSpeedH(thisPtr) >= thisPtr->mConst->getDashFastBorderSpeed())
                        || anim->isAnim("JumpBroad8") || anim->isAnim("CapeGlide"));

                if (isMoveSuper && !wasMoveSuper) { al::validateHitSensor(thisPtr, "GalaxySpin"); hitBufferCount = 0; }
                else if (!isMoveSuper && wasMoveSuper) al::invalidateHitSensor(thisPtr, "GalaxySpin");

                wasMoveSuper = isMoveSuper;

                applyMoonMarioConst(thisPtr->mConst); // force Moon every tick
            }
            if (anim && anim->isAnim("LandStiffen") && !anim->isAnim("LandSuper")) anim->startAnim("LandSuper");
            if (anim && anim->isAnim("MofumofuDemoOpening2") && !anim->isAnim("MofumofuDemoOpening2Super")) anim->startAnim("MofumofuDemoOpening2Super");
            
            if (isBrawl && anim && anim->isAnim("WearEnd") && !anim->isAnim("WearEndBrawl")) anim->startAnim("WearEndBrawl");
            if (isSuper && anim && anim->isAnim("WearEnd") && !anim->isAnim("WearEndSuper")) anim->startAnim("WearEndSuper");
        }

        // Handle Taunt triggers
        /*if (!thisPtr->mInput->isMove()
            && (al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait))
            || al::isNerve(thisPtr, getNerveAt(nrvHakoniwaSquat)))
            && !al::isNerve(thisPtr, &TauntLeftNrv)
            && !al::isNerve(thisPtr, &TauntRightNrv)
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
        }*/

        // Reset proximity flag
        isNearCollectible = false;
        isNearTreasure = false;
        isNearSwoonedEnemy = false;

        // Get Mario's Carry sensor
        al::HitSensor* carrySensor = al::getHitSensor(thisPtr, "Carry");
        if (carrySensor && carrySensor->mIsValid) {
            // Check all sensors colliding with Carry sensor
            for (int i = 0; i < carrySensor->mSensorCount; i++) {
                al::HitSensor* other = carrySensor->mSensors[i];
                al::LiveActor* actor = al::getSensorHost(other);
                
                if (actor) {
                    // Check if sensor belongs to target object type
                    const char* typeName = typeid(*actor).name();
                    if (al::isEqualSubString(typeName, "Radish")
                        || al::isEqualSubString(typeName, "Stake")
                        || al::isEqualSubString(typeName, "BossRaidRivet")
                    ) {
                        isNearCollectible = true;
                        break;
                    } else if (al::isEqualSubString(typeName, "TreasureBox")
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
        
        // Handle cape logic for Brawl Suit
        bool isGliding =
            al::isActionPlaying(model, "CapeGlide")
            || al::isActionPlaying(model, "CapeGlideFloat")
            || al::isActionPlaying(model, "CapeGlideFloatStart")
            || al::isActionPlaying(model, "CapeGlideFloatSuper")
            || al::isActionPlaying(model, "JumpBroad8");
        
        if ((isMario || isBrawl)
            && cape
        ) {
            if (al::isDead(cape)) isCapeActive = -1;
            else if (!isGliding && isCapeActive > 0) {
                if (--isCapeActive == 0) {
                    cape->kill();
                    if (!al::isEffectEmitting(keeper, "AppearBloom")) al::tryEmitEffect(keeper, "AppearBloom", nullptr);
                    al::tryStartSe(thisPtr, "Bloom");
                    isCapeActive = -1;
                }
            }
        }

        // Apply or remove invincibility
        PlayerDamageKeeper* damagekeep = thisPtr->mDamageKeeper;
        bool damageBlink = damagekeep && damagekeep->mIsPreventDamage && (damagekeep->mRemainingInvincibility > 0);
        bool hacked = thisPtr->mHackKeeper && thisPtr->mHackKeeper->mCurrentHackActor;

        if (isSuper && !hacked
            && (!al::isHideModel(model) || damageBlink)
        ) {
            if (!al::isEffectEmitting(keeper, "Bonfire")) al::tryEmitEffect(keeper, "Bonfire", nullptr);
            if (!damagekeep->mIsPreventDamage) {
                damagekeep->activatePreventDamage();
                damagekeep->mRemainingInvincibility = INT_MAX;
            }
        } else {
            if (hacked || !damageBlink) {
                if (al::isEffectEmitting(keeper, "Bonfire")) al::tryDeleteEffect(keeper, "Bonfire");
                damagekeep->mRemainingInvincibility = 0;
            }
        }
    }
};

struct PlayerActorHakoniwaExeJump : public mallow::hook::Trampoline<PlayerActorHakoniwaExeJump> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {

        auto* anim   = thisPtr->mAnimator;
        auto* model = thisPtr->mModelHolder->findModelActor("Mario");
        auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

        if (!isBrawl && !isFeather) {
            Orig(thisPtr);
            return;
        }

        bool wasGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool wasWater = al::isInWater(thisPtr);

        Orig(thisPtr);

        bool isGround =  rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool isWater  =  al::isInWater(thisPtr);
        bool isAir     = !isGround && !isWater;

        if (wasWater || (wasGround && isAir)) isDoubleJump = false;

        if (isAir && !isDoubleJump && (al::isPadTriggerA(-1) || al::isPadTriggerB(-1))
        ) {
            isDoubleJump = true;
            if (isBrawl) al::tryEmitEffect(keeper, "DoubleJump", nullptr);
            if (isFeather) { al::tryEmitEffect(keeper, "AppearBloom", nullptr); al::tryStartSe(thisPtr, "Bloom"); }
            al::setNerve(thisPtr, getNerveAt(nrvHakoniwaJump));
        }

        if (isBrawl && al::isFirstStep(thisPtr) && isDoubleJump) anim->startAnim("PoleHandStandJump");
        if (isFeather && al::isFirstStep(thisPtr) && isDoubleJump) anim->startAnim("JumpDashFast");
    }
};

struct PlayerStateJumpTryCountUp : public mallow::hook::Trampoline<PlayerStateJumpTryCountUp> {
    static void Callback(PlayerStateJump* state, PlayerContinuousJump* cont) {

        if (isBrawl) return;

        Orig(state, cont);
    }
};

struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {

        if(isPadTriggerGalaxySpin(-1)
            && !thisPtr->mAnimator->isAnim("SpinSeparate")
            && canGalaxySpin
        ) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};
struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {

        if(isPadTriggerGalaxySpin(-1)
            && !thisPtr->mAnimator->isAnim("SpinSeparate")
            && canGalaxySpin
        ) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};

struct PlayerActorHakoniwaExeHeadSliding : public mallow::hook::Trampoline<PlayerActorHakoniwaExeHeadSliding> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);
                
        auto* anim   = thisPtr->mAnimator;
        auto* model = thisPtr->mModelHolder->findModelActor("Mario");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

        if (!isMario && !isBrawl && !isFeather && !isSuper) return;

        float vy = al::getVelocity(thisPtr).y;
        if (vy < -2.5f)
        al::setVelocityY(thisPtr, -2.5f);

        float speed = al::calcSpeed(thisPtr);

        if (al::isFirstStep(thisPtr)
        ) {
            anim->endSubAnim();
            if ((isMario || isBrawl) 
                && cape && al::isDead(cape)
            ) {
                cape->appear();
                al::tryEmitEffect(keeper, "AppearBloom", nullptr);
                al::tryStartSe(thisPtr, "Bloom");
            }
            anim->startAnim("JumpBroad8");
        }
        else if (anim->isAnim("JumpBroad8") && anim->isAnimEnd()) anim->startAnim("CapeGlide");
        else if (speed < 10.f) {
            if (anim->isAnim("CapeGlide")) anim->startAnim("CapeGlideFloatStart");
            if (anim->isAnim("CapeGlideFloatStart")
                && anim->isAnimEnd()
            ) {
                anim->startAnim("CapeGlideFloat");
                if (isSuper) anim->startAnim("CapeGlideFloatSuper");
            }
        }

        if (al::isGreaterStep(thisPtr, 25)) {
        
            if (al::isPadTriggerA(-1)
            || al::isPadTriggerB(-1)) {
                if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaFall))) {
                al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                }
            }

            if (al::isPadTriggerZL(-1)
            || al::isPadTriggerZR(-1)) {
                if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop))) {
                al::setNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop));
                }
            }

            if (isPadTriggerGalaxySpin(-1)
            && canGalaxySpin) {
                if (!al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))) {
                triggerGalaxySpin = true;
                al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                }
            }

            if (!isPadTriggerGalaxySpin(-1)
            && (al::isPadTriggerX(-1)
            || al::isPadTriggerY(-1))) {
                if (!al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))) {
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

struct PlayerConstGetSpinAirSpeedMax : public mallow::hook::Trampoline<PlayerConstGetSpinAirSpeedMax> {
    static float Callback(PlayerConst* playerConst) {

        if(isGalaxySpin && !isPunching)
            return playerConst->getNormalMaxSpeed();
        return Orig(playerConst);
    }
};

struct PlayerConstGetSpinBrakeFrame : public mallow::hook::Trampoline<PlayerConstGetSpinBrakeFrame> {
    static s32 Callback(PlayerConst* playerConst) {

        if(isGalaxySpin && !isPunching)
            return 0;
        return Orig(playerConst);
    }
};

struct PlayerConstGetNormalMaxSpeed : public mallow::hook::Trampoline<PlayerConstGetNormalMaxSpeed> {
    static float Callback(const PlayerConst* thisPtr) {

        float speed = Orig(thisPtr);
        if (isSuper
            && !(isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor)) speed *= 2.0f;
        return speed;
    }
};

struct PlayerConstGetHeadSlidingSpeed : public mallow::hook::Trampoline<PlayerConstGetHeadSlidingSpeed> {
    static float Callback(const PlayerConst* thisPtr) {

        float speed = Orig(thisPtr);
        if (isSuper
            && !(isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor)) speed *= 1.5f;
        return speed;
    }
};

struct PlayerAnimControlRunUpdate : public mallow::hook::Inline<PlayerAnimControlRunUpdate> {
    static void Callback(exl::hook::InlineCtx* ctx) {

        if (isSuper
            && !(isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor))
                *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("MoveSuper"); //mMoveAnimName in PlayerAnimControlRun
    }
};

struct PlayerSeCtrlUpdateMove : public mallow::hook::Inline<PlayerSeCtrlUpdateMove> {
    static void Callback(exl::hook::InlineCtx* ctx) {

        if (isSuper
            && !(isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor))
                ctx->X[8] = reinterpret_cast<u64>("MoveSuper");
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
    // Disable Y button for everything else
    // PadTriggerYHook::InstallAtSymbol("_ZN2al13isPadTriggerYEi");

    // Disable R Reset Camera
    InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");
    InputIsTriggerActionCameraResetHook::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");

    // Initialize player actor
    PlayerActorHakoniwaInitPlayer::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");

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

    // Send out attack messages during spins
    PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");

    // Mario's control that checks every frame
    //PlayerControlHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa7controlEv");
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");

    // Allow carrying an object during a GalaxySpin
    PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);
    PlayerCarryKeeperIsCarryDuringSwimSpin::InstallAtOffset(0x489EE8);
    PlayerCarryKeeperStartThrowNoSpin::InstallAtSymbol("_ZN17PlayerCarryKeeper10startThrowEb");

    // Allow triggering spin on roll and squat
    PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");

    // Handles Mario's double jump
    PlayerActorHakoniwaExeJump::InstallAtSymbol("_ZN19PlayerActorHakoniwa7exeJumpEv");
    PlayerStateJumpTryCountUp::InstallAtSymbol("_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump");
    
    // Handles Mario's glide
    PlayerActorHakoniwaExeHeadSliding::InstallAtSymbol("_ZN19PlayerActorHakoniwa14exeHeadSlidingEv");
    PlayerHeadSlidingKill::InstallAtSymbol("_ZN22PlayerStateHeadSliding4killEv");

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

    // Disable invincibility music patches
    exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
    invincibleStartPatcher.WriteInst(0x1F2003D5); // NOP
    exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
    invinciblePatcher.WriteInst(0x1F2003D5); // NOP

    // Do not cancel momentum on spin
    //PlayerConstGetSpinAirSpeedMax::InstallAtSymbol("_ZNK11PlayerConst18getSpinAirSpeedMaxEv");
    //PlayerConstGetSpinBrakeFrame::InstallAtSymbol("_ZNK11PlayerConst17getSpinBrakeFrameEv");

    // Modify Mario's Player settings
    //PlayerAnimControlRun::InstallAtOffset(0x42C5E0);
    PlayerAnimControlRunUpdate::InstallAtOffset(0x42C6BC);
    PlayerSeCtrlUpdateMove::InstallAtOffset(0x463038);
    PlayerSeCtrlUpdateWearEnd::InstallAtOffset(0x463DE0);
    PlayerConstGetNormalMaxSpeed::InstallAtSymbol("_ZNK11PlayerConst17getNormalMaxSpeedEv");
    PlayerConstGetHeadSlidingSpeed::InstallAtSymbol("_ZNK11PlayerConst19getHeadSlidingSpeedEv");

    // Running on water
    StartWaterSurfaceRunJudge::InstallAtSymbol("_ZNK31PlayerJudgeStartWaterSurfaceRun5judgeEv");
    WaterSurfaceRunJudge::InstallAtSymbol("_ZNK26PlayerJudgeWaterSurfaceRun5judgeEv");
    RunWaterSurfaceDisableSink::InstallAtOffset(0x48023C);
    WaterSurfaceRunDisableSlowdown::InstallAtOffset(0x4184C0);
    RsIsTouchDeadCode::InstallAtSymbol("_ZN2rs15isTouchDeadCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChangerPK13IUseDimensionf");
    RsIsTouchDamageFireCode::InstallAtSymbol("_ZN2rs21isTouchDamageFireCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChanger");

    // Remove Cappy eyes while ide
    exl::patch::CodePatcher eyePatcher(0x41F7E4);
    eyePatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W0, 0));
}