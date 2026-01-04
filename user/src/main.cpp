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
static al::LiveActor* isKoopa = nullptr; // Global pointer for Bowser

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
                        // only spin attack
                        state->mAnimator->startSubAnim("SpinSeparate");
                        state->mAnimator->startAnim("SpinSeparate");
                        al::validateHitSensor(state->mActor, "GalaxySpin");
                        galaxySensorRemaining = 21;

                        // only punch attack
                        /*if (isFinalPunch) {
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

                        isPunching = true; // Validate punch animations*/
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

PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;
PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;

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

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        auto* anim   = thisPtr->mAnimator;
        auto* holder = thisPtr->mModelHolder;
        auto* model  = holder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
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
        }
        
        // Handle attack and effects for Super suit
        if (isSuper) {
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

struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        if (isPadTriggerGalaxySpin(-1)
            && !thisPtr->mAnimator->isAnim("SpinSeparate")
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
        if (isPadTriggerGalaxySpin(-1)
            && !thisPtr->mAnimator->isAnim("SpinSeparate")
        ) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};

struct PlayerSeCtrlUpdateWearEnd : public mallow::hook::Inline<PlayerSeCtrlUpdateWearEnd> {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (isBrawl) ctx->X[20] = reinterpret_cast<u64>("WearEndBrawl");
        if (isSuper) ctx->X[20] = reinterpret_cast<u64>("WearEndSuper");
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    mallow::init::installHooks();
    // Modify triggers
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

    // Allow carrying an object during a GalaxySpin
    PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);
    PlayerCarryKeeperIsCarryDuringSwimSpin::InstallAtOffset(0x489EE8);
    PlayerCarryKeeperStartThrowNoSpin::InstallAtSymbol("_ZN17PlayerCarryKeeper10startThrowEb");

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

    // Handles control/movement
    //PlayerControlHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa7controlEv");
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");

    // Allow triggering spin on roll and squat
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");
    PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");

    // Handle WearEnd
    PlayerSeCtrlUpdateWearEnd::InstallAtOffset(0x463DE0);

    // Disable invincibility music patches
    exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
    invincibleStartPatcher.WriteInst(0x1F2003D5); // NOP
    exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
    invinciblePatcher.WriteInst(0x1F2003D5); // NOP

    // Remove Cappy eyes while ide
    exl::patch::CodePatcher eyePatcher(0x41F7E4);
    eyePatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W0, 0));
}