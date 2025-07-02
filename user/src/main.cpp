// external libraries
#include <limits.h>
#include <exl/hook/base.hpp>
#include <mallow/config.hpp>
#include <mallow/init/initLogging.hpp>
#include <mallow/logging/logger.hpp>
#include <mallow/mallow.hpp>

// core system
#include "Library/Nature/NatureUtil.h"
#include "System/GameDataFunction.h"

// third‑party / project libs
#include "Library/Base/StringUtil.h"
#include "Library/Controller/InputFunction.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/LiveActor/ActorFlagFunction.h"
#include "Library/LiveActor/ActorModelFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorSensorFunction.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/LiveActorFunction.h"
#include "Library/LiveActor/LiveActorGroup.h"
#include "Library/Math/MathUtil.h"
#include "Library/Nerve/NerveSetupUtil.h"
#include "Library/Nerve/NerveUtil.h"
#include "Library/Placement/PlacementFunction.h"

// game‑specific utilities
#include "Project/HitSensor/HitSensor.h"
#include "Util/PlayerCollisionUtil.h"
#include "Util/PlayerUtil.h"
#include "Util/SensorMsgFunction.h"

// player actor & state headers
#include "Player/IUsePlayerCollision.h"
#include "Player/PlayerActionGroundMoveControl.h"
#include "Player/PlayerAnimator.h"
#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerColliderHakoniwa.h"
#include "Player/PlayerDamageKeeper.h"
#include "Player/PlayerFunction.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerInput.h"
#include "Player/PlayerModelHolder.h"
#include "Player/PlayerStateHeadSliding.h"
#include "Player/PlayerStateSpinCap.h"
#include "Player/PlayerStateSwim.h"
#include "Player/PlayerStateWait.h"
#include "Player/PlayerTrigger.h"

// mod‑specific
#include "actors/custom/FireBall.h"
#include "actors/custom/Hammer.h"
#include "actors/custom/PlayerStateJump.h"
#include "ModOptions.h"

namespace rs {
    bool is2D(const IUseDimension*);
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

al::LiveActor* hitBuffer[0x40];
int hitBufferCount = 0;

const uintptr_t spinCapNrvOffset = 0x1d78940;
const uintptr_t nrvSpinCapFall = 0x1d7ff70;
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

// Global flags to track suits
bool isBrawl = false;
bool isSuper = false;
bool isFeather = false;

bool isDoubleJump = false; // Global flag to track double jump state

int isCapeActive = -1; // Global flag to track cape state

al::LiveActorGroup* fireBalls = nullptr; // Global pointer for fireballs
bool nextThrowLeft = true; // Global flag to track next throw direction
bool canFireball = false; // Global flag to track fireball trigger

static HammerBrosHammer* hammerAttack = nullptr; // Global pointer for hammer attack

struct PlayerActorHakoniwaInitPlayer : public mallow::hook::Trampoline<PlayerActorHakoniwaInitPlayer> {
    static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        Orig(thisPtr, actorInfo, playerInfo);

        auto* model = thisPtr->mModelHolder->findModelActor("Mario");

        hammerAttack = new HammerBrosHammer("HammerBrosHammer", model, "HammerBrosHammer", true);
        hammerAttack->init(*actorInfo);
        
        // Create and hide fireballs
        fireBalls = new al::LiveActorGroup("FireBrosFireBall", 2);
        while (!fireBalls->isFull()) {
            auto* fb = new FireBrosFireBall("FireBall", model);
            fb->init(*actorInfo);
            al::invalidateHitSensors(fb);
            fireBalls->registerActor(fb);
        }
        fireBalls->makeActorDeadAll();

        // Check for Super suit costume and cap
        const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
        const char* cap     = GameDataFunction::getCurrentCapTypeName(thisPtr);

        isBrawl = (costume && al::isEqualString(costume, "MarioColorBrawl"))
                && (cap && al::isEqualString(cap, "MarioColorBrawl"));

        isSuper = (costume && al::isEqualString(costume, "MarioColorSuper"))
                && (cap && al::isEqualString(cap, "MarioColorSuper"));

        isFeather = (costume && al::isEqualString(costume, "MarioFeather"));
    }
};

struct PlayerActorHakoniwaInitAfterPlacement : public mallow::hook::Trampoline<PlayerActorHakoniwaInitAfterPlacement> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        if (fireBalls) fireBalls->makeActorDeadAll();
        if (hammerAttack) hammerAttack->makeActorDead();
    }
};

struct PlayerStateWaitExeWait : public mallow::hook::Trampoline<PlayerStateWaitExeWait>{
    static void Callback(PlayerStateWait* state) {
        Orig(state);

        if (!isSuper && !isBrawl)
        return;

        if (al::isFirstStep(state)) {
            const char* special = nullptr;
            if (state->tryGetSpecialStatusAnimName(&special)) {
                if (al::isEqualString(special, "BattleWait"))
                    state->requestAnimName("AngryWaitFight");
                else
                    state->requestAnimName(special);
            }
            else {
                if (isBrawl) state->requestAnimName("AngryWait");
                else if (isSuper) state->requestAnimName("AngryWaitSuper");
            }
        }
    }
};

struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack>{
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        // do not allow Y to trigger both pickup and spin on seeds (for picking up rocks, this function is not called)
        bool newIsCarry = player->mCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) {
            prevIsCarry = newIsCarry;
            return false;
        }
        prevIsCarry = newIsCarry;

        if (isPadTriggerGalaxySpin(-1) && !rs::is2D(player) && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) {

            if (player->mAnimator->isAnim("SpinSeparate") || 
            player->mAnimator->isAnim("SpinAttackLeft") || 
            player->mAnimator->isAnim("SpinAttackRight") || 
            player->mAnimator->isAnim("SpinAttackAirLeft") || 
            player->mAnimator->isAnim("SpinAttackAirRight") || 
            player->mAnimator->isAnim("KoopaCapPunchL") ||
            player->mAnimator->isAnim("KoopaCapPunchR") || 
            player->mAnimator->isAnim("RabbitGet"))
            return false;
    
            if (canGalaxySpin) {
                triggerGalaxySpin = true;
            }
            else {
                triggerGalaxySpin = true;
                galaxyFakethrowRemainder = -2;
            }
            return true;
        }

        if (al::isPadTriggerR(-1) && !rs::is2D(player) && !player->mCarryKeeper->isCarry() && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) {
            canFireball = true;
        }

        if(Orig(player, a2)) {
            triggerGalaxySpin = false;
            return true;
        }
        return false;
    }
};

struct PlayerTryActionCapSpinAttackBindEnd : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttackBindEnd>{
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        // do not allow Y to trigger both pickup and spin on seeds (for picking up rocks, this function is not called)
        bool newIsCarry = player->mCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) {
            prevIsCarry = newIsCarry;
            return false;
        }
        prevIsCarry = newIsCarry;

        if (isPadTriggerGalaxySpin(-1) && !rs::is2D(player) && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) {

            if (player->mAnimator->isAnim("SpinSeparate") || 
            player->mAnimator->isAnim("SpinAttackLeft") || 
            player->mAnimator->isAnim("SpinAttackRight") || 
            player->mAnimator->isAnim("SpinAttackAirLeft") || 
            player->mAnimator->isAnim("SpinAttackAirRight") || 
            player->mAnimator->isAnim("KoopaCapPunchL") ||
            player->mAnimator->isAnim("KoopaCapPunchR") || 
            player->mAnimator->isAnim("RabbitGet"))
            return false;
    
            if (canGalaxySpin) {
                triggerGalaxySpin = true;
            }
            else {
                triggerGalaxySpin = true;
                galaxyFakethrowRemainder = -2;
            }
            return true;
        }

        if (al::isPadTriggerR(-1) && !rs::is2D(player) && !player->mCarryKeeper->isCarry() && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) {
            canFireball = true;
        }

        if(Orig(player, a2)) {
            triggerGalaxySpin = false;
            return true;
        }
        return false;
    }
};

class PlayerStateSpinCapNrvGalaxySpinGround : public al::Nerve {
    public:
        void execute(al::NerveKeeper* keeper) const override {
            PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
            PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);
            
            bool isCarrying = player->mCarryKeeper->isCarry();
            bool isRotatingL = state->mAnimator->isAnim("SpinGroundL")
                || player->mInput->isSpinInput();
            bool isRotatingR = state->mAnimator->isAnim("SpinGroundR")
                || player->mInput->isSpinInput();
            bool isSpinning = state->mAnimator->isAnim("SpinSeparate");

            isSpinActive = true;

            if (al::isFirstStep(state)) {
                state->mAnimator->endSubAnim();
                isPunchRight = !isPunchRight;

                if (!isSpinning) {

                    if (isRotatingL) {
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
                            //galaxySensorRemaining = 15;
                            //isPunching = false;  
                            //return;                                     
                    } else {
                        if (isPunchRight) {
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

                        isPunching = true; // Validate punch animations
                    }
                }
            }
            
            if (!isSpinning
                && !isRotatingL
                && !isRotatingR
                && !isCarrying
                && !isNearCollectible) {

                if (al::isStep(state, 3)) {
                    // Reduce Mario's existing momentum by 50%
                    sead::Vector3f currentVelocity = al::getVelocity(player);
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
                            || state->mAnimator->isAnim("RestartSpinJumpL")
                            || player->mInput->isSpinInput();
        bool isRotatingAirR  = state->mAnimator->isAnim("StartSpinJumpR")
                            || state->mAnimator->isAnim("RestartSpinJumpR")
                            || player->mInput->isSpinInput();
        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");

        isSpinActive = true;
        
        if(al::isFirstStep(state)) {

            if (!isSpinning) {

                if (isRotatingAirL) {
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

        if (!isSpinning && al::isGreaterStep(state, 41)) {
            al::invalidateHitSensor(state->mActor, "DoubleSpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }

        if (isSpinning && al::isGreaterStep(state, 21)) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
    }
};

PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;
PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;

static sead::Matrix34f hammerMtx;
static const sead::Vector3f rotOffset(0.0f, sead::Mathf::deg2rad(-90.0f), 0.0f);

class PlayerActorHakoniwaNrvHammer : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* model = player->mModelHolder->findModelActor("Mario");
        auto* anim = player->mAnimator;


        if (al::isFirstStep(player)) {
            
            anim->endSubAnim();
            anim->startAnim("HammerAttack");

            hammerAttack->makeActorAlive();

            hammerAttack->attach(
            al::getJointMtxPtr(model, "HandR"),
            sead::Vector3f (0.0f, 50.0f, -37.5f),
            rotOffset,
            "Wait");

            //al::startAction(hammerAttack, "Hammer");
        }
                
        if (anim->isAnimEnd()) {
            hammerAttack->makeActorDead();
            al::setNerve(player, getNerveAt(nrvHakoniwaFall));
        }
    }
};

PlayerActorHakoniwaNrvHammer HammerNrv;

struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear>{
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

struct PlayerStateSpinCapKill : public mallow::hook::Trampoline<PlayerStateSpinCapKill>{
    static void Callback(PlayerStateSpinCap* state) {
        Orig(state);
        canStandardSpin = true;
        canGalaxySpin = true;
        galaxyFakethrowRemainder = -1; 
        isPunching = false;
        isSpinActive = false;
        isNearCollectible = false;
    }
};

struct PlayerStateSpinCapFall : public mallow::hook::Trampoline<PlayerStateSpinCapFall>{
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

struct PlayerStateSpinCapIsEnableCancelHipDrop : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelHipDrop>{
    static bool Callback(PlayerStateSpinCap* state) {
        return Orig(state) || (al::isNerve(state, &GalaxySpinAir) && al::isGreaterStep(state, 10));
    }
};

struct PlayerStateSpinCapIsEnableCancelAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelAir>{
    static bool Callback(PlayerStateSpinCap* state) {
        return Orig(state) && !(!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
    }
};

struct PlayerStateSpinCapIsSpinAttackAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsSpinAttackAir>{
    static bool Callback(PlayerStateSpinCap* state) {
        return Orig(state) || (!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
    }
};

struct PlayerStateSpinCapIsEnableCancelGround : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelGround>{
    static bool Callback(PlayerStateSpinCap* state) {
        // Check if Mario is in the GalaxySpinGround nerve and performing the SpinSeparate move
        bool isSpinSeparate = state->mAnimator->isAnim("SpinSeparate");

        // Allow canceling only if Mario is in the SpinSeparate move
        return Orig(state) || (al::isNerve(state, &GalaxySpinGround) && isSpinSeparate && al::isGreaterStep(state, 10));
    }
};

struct PlayerConstGetSpinAirSpeedMax : public mallow::hook::Trampoline<PlayerConstGetSpinAirSpeedMax>{
    static float Callback(PlayerConst* playerConst) {
        if(isGalaxySpin && !isPunching)
            return playerConst->getNormalMaxSpeed();
        return Orig(playerConst);
    }
};

struct PlayerConstGetSpinBrakeFrame : public mallow::hook::Trampoline<PlayerConstGetSpinBrakeFrame>{
    static s32 Callback(PlayerConst* playerConst) {
        if(isGalaxySpin && !isPunching)
            return 0;
        return Orig(playerConst);
    }
};

// used in swimming, which also calls tryActionCapSpinAttack before, so just assume isGalaxySpin is properly set up
struct PlayerSpinCapAttackIsSeparateSingleSpin : public mallow::hook::Trampoline<PlayerSpinCapAttackIsSeparateSingleSpin>{
    static bool Callback(PlayerStateSwim* thisPtr) {
        if(triggerGalaxySpin) {
            return true;
        }
        return Orig(thisPtr);
    }
};

struct PlayerStateSwimExeSwimSpinCap : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCap>{
    static void Callback(PlayerStateSwim* thisPtr) {
        Orig(thisPtr);
        if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
            al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
            hitBufferCount = 0;
            isGalaxySpin = true;
            triggerGalaxySpin = false;
            isSpinActive = true;

            if (isNearCollectible) al::validateHitSensor(thisPtr->mActor, "Punch");
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

struct PlayerStateSwimExeSwimSpinCapSurface : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCapSurface>{
    static void Callback(PlayerStateSwim* thisPtr) {
        Orig(thisPtr);
        if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
            al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
            hitBufferCount = 0;
            isGalaxySpin = true;
            triggerGalaxySpin = false;
            isSpinActive = true;

            if (isNearCollectible) al::validateHitSensor(thisPtr->mActor, "Punch");
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

struct PlayerStateSwimExeSwimHipDropHeadSliding : public mallow::hook::Trampoline<PlayerStateSwimExeSwimHipDropHeadSliding>{
    static void Callback(PlayerStateSwim* thisPtr) {
        Orig(thisPtr);
        if(isPadTriggerGalaxySpin(-1))
            if(((PlayerActorHakoniwa*)thisPtr->mActor)->tryActionCapSpinAttackImpl(true))
                thisPtr->startCapThrow();
    }
};

struct PlayerStateSwimKill : public mallow::hook::Trampoline<PlayerStateSwimKill>{
    static void Callback(PlayerStateSwim* state) {
        Orig(state);
        isGalaxySpin = false;
        al::invalidateHitSensor(state->mActor, "GalaxySpin");
        isSpinActive = false;
    }
};

struct PlayerSpinCapAttackStartSpinSeparateSwimSurface : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwimSurface>{
    static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
        if(!isGalaxySpin && !triggerGalaxySpin) {
            Orig(thisPtr, animator);
            return;
        }

        if (isNearCollectible) animator->startAnim("RabbitGet");
        else animator->startAnim("SpinSeparateSwim");
    }
};

struct PlayerSpinCapAttackStartSpinSeparateSwim : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwim>{
    static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
        if(!isGalaxySpin && !triggerGalaxySpin) {
            Orig(thisPtr, animator);
            return;
        }

        if (isNearCollectible) animator->startAnim("RabbitGet");
        else animator->startAnim("SpinSeparateSwim");
    }
};

struct DisallowCancelOnUnderwaterSpinPatch : public mallow::hook::Inline<DisallowCancelOnUnderwaterSpinPatch>{
    static void Callback(exl::hook::InlineCtx* ctx) {
        if(isGalaxySpin)
            ctx->W[20] = true;
    }
};

struct DisallowCancelOnWaterSurfaceSpinPatch : public mallow::hook::Inline<DisallowCancelOnWaterSurfaceSpinPatch>{
    static void Callback(exl::hook::InlineCtx* ctx) {
        if(isGalaxySpin)
            ctx->W[21] = true;
    }
};

namespace rs {
    bool sendMsgHackAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapAttackCollide(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapReflect(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapReflectCollide(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgBlowObjAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapItemGet(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgEnemyAttackStrong(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgBreedaPush(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgWanwanEnemyAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgKillerMagnumAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgTsukkunThrust(al::HitSensor*, al::HitSensor*, sead::Vector3<float> const&, int, bool);
    bool sendMsgCapTouchWall(al::HitSensor*, al::HitSensor*, sead::Vector3<float> const&, sead::Vector3<float> const&);
    al::HitSensor* tryGetCollidedWallSensor(IUsePlayerCollision const* collider);
    al::HitSensor* tryGetCollidedGroundSensor(IUsePlayerCollision const* collider);
}

struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* target, al::HitSensor* source) {
        
        // Null check for thisPtr, target, and source
        if (!thisPtr || !target || !source) {
            return; // Exit early if any critical pointer is null
        }
        
        al::LiveActor* sourceHost = al::getSensorHost(source);
        al::LiveActor* targetHost = al::getSensorHost(target);
        
        // Null check for targetHost and sourceHost
        if (!targetHost || !sourceHost) {
            return; // Exit early if either targetHost or sourceHost is null
        }

        if((al::isSensorName(target, "HipDropKnockDown")) &&
            thisPtr->mAnimator && 
            (al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDrop") ||
            al::isEqualString(thisPtr->mAnimator->mCurAnim, "HipDropReaction") ||
            al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinJumpDownFallL") ||
            al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinJumpDownFallR"))) {
            bool isInHitBuffer = false;
            for(int i = 0; i < hitBufferCount; i++) {
                if(hitBuffer[i] == sourceHost) {
                    isInHitBuffer = true;
                    break;
                }
            }
            if (rs::tryGetCollidedGroundSensor(thisPtr->mCollider) &&
            !al::isEqualSubString(typeid(*sourceHost).name(),"FixMapParts") &&
            !al::isEqualSubString(typeid(*sourceHost).name(),"CitySignal")) {
                if(!isInHitBuffer) {
                    if (al::sendMsgExplosion(source, target, nullptr)) {
                        hitBuffer[hitBufferCount++] = al::getSensorHost(source);
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"ReactionObject") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"SphinxRide") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;
                    }
                }
            }
            return;
        }

        if((al::isSensorName(target, "GalaxySpin")
        || al::isSensorName(target, "Punch")
        || al::isSensorName(target, "DoubleSpin"))
        && thisPtr->mAnimator
        && (al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinSeparate")
            || al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchR")
            || al::isEqualString(thisPtr->mAnimator->mCurAnim, "KoopaCapPunchL")
            || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackLeft")
            || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackRight")
            || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackAirLeft")
            || al::isEqualString(thisPtr->mAnimator->mCurAnim, "SpinAttackAirRight")
            || al::isEqualString(thisPtr->mAnimator->mCurAnim, "RabbitGet")
            || isGalaxySpin)) {
            bool isInHitBuffer = false;
            for(int i = 0; i < hitBufferCount; i++) {
                if(hitBuffer[i] == sourceHost) {
                    isInHitBuffer = true;
                    break;
                }
            }

            // Null check for sourceHost's NerveKeeper
            if (!sourceHost->getNerveKeeper()) {
                return; // Exit early if NerveKeeper is null
            }

            if(sourceHost && sourceHost->getNerveKeeper()) {
                const al::Nerve* sourceNrv = sourceHost->getNerveKeeper()->getCurrentNerve();
                isInHitBuffer |= sourceNrv == getNerveAt(0x1D03268);  // GrowPlantSeedNrvHold
                isInHitBuffer |= sourceNrv == getNerveAt(0x1D00EC8);  // GrowFlowerSeedNrvHold
                isInHitBuffer |= sourceNrv == getNerveAt(0x1D22B78);  // RadishNrvHold
            
                // do not "disable" when trying to hit BlockQuestion/BlockBrick with TenCoin & Motorcycle
                if (al::isSensorName(target, "GalaxySpin")
                || al::isSensorName(target, "DoubleSpin")) {
                    isInHitBuffer &= sourceNrv != getNerveAt(0x1CD6758);
                    isInHitBuffer &= sourceNrv != getNerveAt(0x1CD4BB0);
                    isInHitBuffer &= sourceNrv != getNerveAt(0x1CD6FA0);
                    isInHitBuffer &= sourceNrv != getNerveAt(0x1D170D0);
                }

                if (al::isSensorName(target, "Punch") && !isPunching) {

                    if (al::isEqualSubString(typeid(*sourceHost).name(),"Stake") &&
                        sourceNrv == getNerveAt(0x1D36D20)) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        al::setNerve(sourceHost, getNerveAt(0x1D36D30));
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        al::tryEmitEffect(targetHost, "Hit", &effectPos);
                        return;
                    }
                    if (al::isEqualSubString(typeid(*sourceHost).name(),"Radish") &&
                        sourceNrv == getNerveAt(0x1D22B70)) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        al::setNerve(sourceHost, getNerveAt(0x1D22BD8));           
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        al::tryEmitEffect(targetHost, "Hit", &effectPos);
                        return;
                    }    
                    if (al::isEqualSubString(typeid(*sourceHost).name(),"BossRaidRivet") &&
                        sourceNrv == getNerveAt(0x1C5F330)) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        al::setNerve(sourceHost, getNerveAt(0x1C5F338));
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        al::tryEmitEffect(targetHost, "Hit", &effectPos);
                        return;
                    }
                }
            }
            if(!isInHitBuffer) {
                if (al::isEqualSubString(typeid(*sourceHost).name(),"CapSwitchTimer")
                && rs::sendMsgCapStartLockOn(source, target)) {
                    //al::setNerve(sourceHost, getNerveAt(0x1CE4338));
                    hitBuffer[hitBufferCount++] = sourceHost;
                    sead::Vector3 effectPos = al::getTrans(targetHost);
                    effectPos.y += 50.0f;
                    if (al::isSensorName(target, "Punch")) {
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 100.0f;
                    }
                    if (al::isSensorName(target, "GalaxySpin")
                    || al::isSensorName(target, "DoubleSpin")) {
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                    }
                    al::tryEmitEffect(targetHost, "Hit", &effectPos);
                    return;
                }
                if (!al::isEqualSubString(typeid(*sourceHost).name(),"CapSwitchTimer")
                && al::isEqualSubString(typeid(*sourceHost).name(),"CapSwitch")) {
                    al::setNerve(sourceHost, getNerveAt(0x1CE3E18));
                    hitBuffer[hitBufferCount++] = sourceHost;
                    sead::Vector3 effectPos = al::getTrans(targetHost);
                    effectPos.y += 50.0f;
                    if (al::isSensorName(target, "Punch")) {
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 100.0f;
                    }
                    if (al::isSensorName(target, "GalaxySpin")
                    || al::isSensorName(target, "DoubleSpin")) {
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                    }
                    al::tryEmitEffect(targetHost, "Hit", &effectPos);
                    return;
                }
                if (al::isEqualSubString(typeid(*sourceHost).name(),"GolemClimbWeakPoint")) {
                    al::setNerve(sourceHost, getNerveAt(0x1C69D08));
                    hitBuffer[hitBufferCount++] = sourceHost;
                    sead::Vector3 effectPos = al::getTrans(targetHost);
                    effectPos.y += 50.0f;
                    if (al::isSensorName(target, "Punch")) {
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 100.0f;
                    }
                    if (al::isSensorName(target, "GalaxySpin")
                    || al::isSensorName(target, "DoubleSpin")) {
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                    }
                    al::tryEmitEffect(targetHost, "Hit", &effectPos);
                    return;
                }
                if (al::isSensorNpc(source) &&
                !al::isEqualSubString(typeid(*sourceHost).name(),"YoshiFruit")) {
                    if (
                        ((al::isEqualSubString(typeid(*sourceHost).name(),"RadiconCar") ||
                        al::isEqualSubString(typeid(*sourceHost).name(),"CollectAnimal")) &&
                        rs::sendMsgCapAttack(source, target)) ||

                        rs::sendMsgCapReflect(source, target) ||
                        rs::sendMsgBlowObjAttack(source, target) ||
                        al::sendMsgEnemyAttack(source, target) ||
                        al::sendMsgPlayerTrampleReflect(source, target, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        if (al::isSensorName(target, "Punch")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 100.0f;
                        }
                        if (al::isSensorName(target, "GalaxySpin")
                        || al::isSensorName(target, "DoubleSpin")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 75.0f;
                        }
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"Frog")) {
                            al::tryDeleteEffect(targetHost, "HitSmall");}
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"CollectAnimal") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;
                    }
                }
                if (al::isSensorEnemyBody(source) &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"PartsModel")) {
                    sead::Vector3 fireDir = al::getTrans(sourceHost) - al::getTrans(targetHost);
                    fireDir.normalize();
                    if (
                        (al::isEqualSubString(typeid(*sourceHost).name(),"Breeda") &&
                        rs::sendMsgWanwanEnemyAttack(source, target)) ||
                        (al::isEqualSubString(typeid(*sourceHost).name(),"BreedaWanwan") &&
                        rs::sendMsgBreedaPush(source, target) &&
                        rs::sendMsgCapReflect(source, target)) ||
                        (al::isEqualSubString(typeid(*sourceHost).name(),"ReflectBomb") &&
                        rs::sendMsgTsukkunThrust(source, target, fireDir, 0, true)) ||
                        (al::isEqualSubString(typeid(*sourceHost).name(),"Killer") &&
                        rs::sendMsgKillerMagnumAttack(source, target)) ||
                        (al::isEqualSubString(typeid(*sourceHost).name(),"CapThrower") &&
                        rs::sendMsgCapReflect(source, target)) ||
                        (al::isEqualSubString(typeid(*sourceHost).name(),"Koopa") &&
                        al::sendMsgPlayerObjHipDropReflect(source, target, nullptr)) ||

                        al::sendMsgKickStoneAttackReflect(source, target) ||
                        rs::sendMsgHackAttack(source, target) ||
                        al::sendMsgExplosion(source, target, nullptr) ||
                        rs::sendMsgCapAttack(source, target) ||

                        (!al::isEqualSubString(typeid(*sourceHost).name(),"ReflectBomb") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"Gunetter") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"GunetterBody") &&
                        rs::sendMsgCapReflect(source, target))
                    ) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        if (al::isSensorName(target, "Punch")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 100.0f;
                        }
                        if (al::isSensorName(target, "GalaxySpin")
                        || al::isSensorName(target, "DoubleSpin")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 75.0f;
                        }
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"BombTail")) {
                            al::tryDeleteEffect(sourceHost, "CapReflect");}
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"FireBlower")) {
                            al::tryDeleteEffect(sourceHost, "HitSmall");
                            al::tryDeleteEffect(sourceHost, "HitMark");}
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"Shibaken") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"MofumofuScrap") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"GrowerWorm") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"FireBlowerCap") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"CapThrowerCap") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"Koopa") &&
                        al::tryEmitEffect(targetHost, "PunchHit", &effectPos));
                        return;
                    }
                }
                if (al::isSensorMapObj(source) &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"CitySignal")) {
                    if (
                        rs::sendMsgEnemyAttackStrong(source, target) ||
                        rs::sendMsgHackAttack(source, target) ||

                        (!al::isEqualSubString(typeid(*sourceHost).name(),"ReactionObject") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"SphinxRide") &&
                        al::sendMsgExplosion(source, target, nullptr)) ||

                        (al::isEqualSubString(typeid(*sourceHost).name(),"Radish") &&
                        rs::sendMsgCapReflect(source, target)) ||
                        (al::isEqualSubString(typeid(*sourceHost).name(),"SneakingMan") &&
                        rs::sendMsgCapAttack(source, target)) ||

                        al::sendMsgPlayerSpinAttack(source, target, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        if (al::isSensorName(target, "Punch")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 100.0f;
                        }
                        if (al::isSensorName(target, "GalaxySpin")
                        || al::isSensorName(target, "DoubleSpin")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 75.0f;
                        }
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"VolleyballBall")) {
                            if (!al::isEffectEmitting(sourceHost, "SmashHit")) {
                                al::tryEmitEffect(targetHost, "Hit", &effectPos);
                            }
                            return;
                        }
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"ReactionObject") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"AirBubble") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"ElectricWireTarget") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;
                    }
                        if (
                            (!al::isEqualSubString(typeid(*sourceHost).name(),"ShineTower") &&
                            rs::sendMsgCapReflect(source, target))
                        ) {
                            hitBuffer[hitBufferCount++] = sourceHost;
                            sead::Vector3 effectPos = al::getTrans(targetHost);
                            effectPos.y += 50.0f;
                            if (al::isSensorName(target, "Punch")) {
                                sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                                direction.normalize();
                                effectPos += direction * 100.0f;
                            }
                            if (al::isSensorName(target, "GalaxySpin")
                            || al::isSensorName(target, "DoubleSpin")) {
                                sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                                direction.normalize();
                                effectPos += direction * 75.0f;
                            }
                            al::tryEmitEffect(targetHost, "Hit", &effectPos);
                            return;                           
                            
                        }
                    if (
                        (al::isEqualSubString(typeid(*sourceHost).name(),"ShineTower") &&
                        al::sendMsgPlayerObjHipDropReflect(source, target, nullptr)) ||

                        (al::isEqualSubString(typeid(*sourceHost).name(),"HackFork") &&
                        al::sendMsgKickStoneAttackReflect(source, target)) ||

                        rs::sendMsgCapAttack(source, target) ||
                        rs::sendMsgCapItemGet(source, target)
                    ) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        if (al::isSensorName(target, "Punch")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 100.0f;
                        }
                        if (al::isSensorName(target, "GalaxySpin")
                        || al::isSensorName(target, "DoubleSpin")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 75.0f;
                        }
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"Souvenir")) {
                            al::tryDeleteEffect(sourceHost, "HitSmall");
                        }
                        (al::isEqualSubString(typeid(*sourceHost).name(),"Souvenir") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;
                    }
                }
                if (al::isSensorRide(source)) {
                    if (
                        rs::sendMsgCapReflect(source, target)
                    ) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        if (al::isSensorName(target, "Punch")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 100.0f;
                        }
                        if (al::isSensorName(target, "GalaxySpin")
                        || al::isSensorName(target, "DoubleSpin")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 75.0f;
                        }
                        al::tryEmitEffect(targetHost, "Hit", &effectPos);
                        return;
                    }
                }
                if (rs::tryGetCollidedWallSensor(thisPtr->mCollider) &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"FixMapParts") &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"CitySignal") &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"GolemClimb")
                ) {
                    if (
                        (al::isEqualSubString(typeid(*sourceHost).name(),"Doshi") &&
                        rs::sendMsgCapAttackCollide(source, target)) ||   
                        (!al::isSensorName(source, "Brake") &&
                        al::isEqualSubString(typeid(*sourceHost).name(),"Car") &&
                        rs::sendMsgCapReflectCollide(source, target)) ||
                        (al::isEqualSubString(typeid(*sourceHost).name(),"ChurchDoor") &&
                        rs::sendMsgCapTouchWall(source, target, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})) ||

                        rs::sendMsgHackAttack(source, target) ||
                        al::sendMsgExplosion(source, target, nullptr)
                    ) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        if (al::isSensorName(target, "Punch")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 100.0f;
                        }
                        if (al::isSensorName(target, "GalaxySpin")
                        || al::isSensorName(target, "DoubleSpin")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 75.0f;
                        }
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"ReactionObject") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"SphinxRide") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                    }
                    if (
                        rs::sendMsgCapReflect(source, target)
                    ) {
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        if (al::isSensorName(target, "Punch")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 100.0f;
                        }
                        if (al::isSensorName(target, "GalaxySpin")
                        || al::isSensorName(target, "DoubleSpin")) {
                            sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                            direction.normalize();
                            effectPos += direction * 75.0f;
                        }
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"MofumofuScrap") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;                    
                    }
                }
            }
        }
        Orig(thisPtr, target, source);
    }
};

struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling>{
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        
        if (isPadTriggerGalaxySpin(-1)
        && !thisPtr->mAnimator->isAnim("SpinSeparate")
        && canGalaxySpin
        && hammerAttack
        && al::isDead(hammerAttack)
        ) {
            al::setNerve(thisPtr, &HammerNrv);
            return;
        }

        /*if(isPadTriggerGalaxySpin(-1) && !thisPtr->mAnimator->isAnim("SpinSeparate") && canGalaxySpin) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }*/
        Orig(thisPtr);
    }
};

struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat>{
    static void Callback(PlayerActorHakoniwa* thisPtr) {

        if (isPadTriggerGalaxySpin(-1)
        && !thisPtr->mAnimator->isAnim("SpinSeparate")
        && canGalaxySpin
        && hammerAttack
        && al::isDead(hammerAttack)
        ) {
            al::setNerve(thisPtr, &HammerNrv);
            return;
        }

        /*if(isPadTriggerGalaxySpin(-1) && !thisPtr->mAnimator->isAnim("SpinSeparate") && canGalaxySpin) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }*/
        Orig(thisPtr);
    }
};

struct PlayerCarryKeeperIsCarryDuringSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSpin>{
    static void Callback(exl::hook::InlineCtx* ctx) {
        // if either currently in galaxyspin or already finished galaxyspin while still in-air
        if(ctx->X[0] && (isGalaxySpin || !canGalaxySpin))
            ctx->X[0] = false;
    }
};

struct PlayerCarryKeeperIsCarryDuringSwimSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSwimSpin>{
    static void Callback(exl::hook::InlineCtx* ctx) {
        // if either currently in galaxyspin
        if(ctx->X[0] && (isGalaxySpin || triggerGalaxySpin))
            ctx->X[0] = false;
    }
};

struct PlayerCarryKeeperStartThrowNoSpin : public mallow::hook::Trampoline<PlayerCarryKeeperStartThrowNoSpin>{
    static bool Callback(PlayerCarryKeeper* state) {
        if (isSpinActive || galaxySensorRemaining != -1 || galaxyFakethrowRemainder != -1) {
        return false;
        }
        return Orig(state); 
    }
};

struct PadTriggerYHook : public mallow::hook::Trampoline<PadTriggerYHook>{
    static bool Callback(int port) {
        if(port == 100)
            return Orig(-1);
        return false;
    };
};

struct TriggerCameraReset : public mallow::hook::Trampoline<TriggerCameraReset>{
    static bool Callback(al::LiveActor* actor, int port) {

        if (al::isPadTriggerR(-1)) return false;

        return Orig(actor, port);
    }
};

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        al::HitSensor* sensorSpin = al::getHitSensor(thisPtr, "GalaxySpin");
        al::HitSensor* sensorDoubleSpin = al::getHitSensor(thisPtr, "DoubleSpin");
        al::HitSensor* sensorPunch = al::getHitSensor(thisPtr, "Punch");
        al::HitSensor* sensorHipDrop = al::getHitSensor(thisPtr, "HipDropKnockDown");        

        if((sensorSpin && sensorSpin->mIsValid)
        || (sensorDoubleSpin && sensorDoubleSpin->mIsValid)
        || (sensorPunch && sensorPunch->mIsValid)
        || (sensorHipDrop && sensorHipDrop->mIsValid)) {
            if (sensorSpin && sensorSpin->mIsValid) {
                thisPtr->attackSensor(sensorSpin, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
            }
            if (sensorDoubleSpin && sensorDoubleSpin->mIsValid) {
                thisPtr->attackSensor(sensorDoubleSpin, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
            }
            if (sensorPunch && sensorPunch->mIsValid) {
                thisPtr->attackSensor(sensorPunch, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
            }
            if (sensorHipDrop && sensorHipDrop->mIsValid) {
                thisPtr->attackSensor(sensorHipDrop, rs::tryGetCollidedGroundSensor(thisPtr->mCollider));
            }
        }
        
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
        auto* cape = al::getSubActor(model, "ケープ");
        al::LiveActor* face = al::tryGetSubActor(model, "顔");
        al::IUseEffectKeeper* keeper = static_cast<al::IUseEffectKeeper*>(model);

        // Change face
        if ((isBrawl || isSuper)
        /*&& (al::isActionPlaying(model, "Move")
        || al::isActionPlaying(model, "MoveMoon")
        || al::isActionPlaying(model, "JumpBroad8")
        || al::isActionPlaying(model, "CapeGlide")
        || al::isActionPlaying(model, "CapeGlideFloatStart")
        || al::isActionPlaying(model, "CapeGlideFloat"))*/) {
            if (face && !al::isActionPlayingSubActor(model, "顔", "AngryWait"))
                al::startActionSubActor(model, "顔", "AngryWait");
        }

        // Reset proximity flag
        isNearCollectible = false;

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
                    if (al::isEqualSubString(typeName, "Radish") ||
                        al::isEqualSubString(typeName, "Stake") ||
                        al::isEqualSubString(typeName, "BossRaidRivet")) {
                        isNearCollectible = true;
                        break; // Exit early if found
                    }
                }
            }
        }

        // Handle cape logic for Brawl Suit
        if (isBrawl && cape) {

            if (al::isDead(cape)) isCapeActive = -1;
            else if (isCapeActive > 0 && --isCapeActive == 0) {

                cape->kill();

                if (!al::isEffectEmitting(keeper, "AppearBloom")) al::tryEmitEffect(keeper, "AppearBloom", nullptr);
                
                isCapeActive = -1;
            }
        }

        // Fireball throwing logic
        const char* jointName = nextThrowLeft ? "HandL" : "HandR";
        const char* fireAnim  = nextThrowLeft ? "FireL" : "FireR";

        FireBrosFireBall* fireBall = (FireBrosFireBall*)fireBalls->getDeadActor();
        bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool isWater = al::isInWater(thisPtr);

        static int fireStep = -1;

        if ((canFireball || anim->isAnim("CapeGlideFloat")) && al::isPadTriggerR(-1)) {
            if (fireStep < 0 && fireBall && al::isDead(fireBall)) {

                fireStep = 0;
                nextThrowLeft = !nextThrowLeft;
                canFireball = false;

                anim->startUpperBodyAnim(fireAnim);
                if (onGround && !isWater) anim->startAnim(fireAnim);

            } else {
                canFireball = false;
            }
        }
        if (fireStep >= 0) {
            if (fireStep == 5) {
                if (model && fireBall) {
                    fireBall->attach(al::getJointMtxPtr(model, jointName), sead::Vector3f(), sead::Vector3f(), "Wait");
                
                    sead::Vector3f startPos;
                    al::calcJointPos(&startPos, model, jointName);
                    sead::Quatf startQuat = al::getQuat(model);
                    sead::Vector3f dir;
                    al::calcQuatFront(&dir, model);
                    dir.normalize();
                    sead::Vector3f offset = dir * 10.0f;

                    fireBall->shoot(startPos, startQuat, offset, true, 0, false);
                }
            }
            if (onGround && !isWater
                && (anim->isAnimEnd() || anim->isUpperBodyAnimEnd())
            ) {
                al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                anim->clearUpperBodyAnim();
                fireStep = -1;
            }
            else if (anim->isUpperBodyAnimEnd()
            ) {
                anim->clearUpperBodyAnim();
                fireStep = -1;

                if (isSuper && anim->isAnim("CapeGlideFloat")) anim->startUpperBodyAnim("AngryWaitFall");

            } else {
                fireStep++;
            }
        }

        // Apply or remove invincibility
        PlayerDamageKeeper* damagekeep = thisPtr->mDamageKeeper;

        if (isSuper) {
            if (model) {
                if (!al::isEffectEmitting(keeper, "Bonfire")) al::tryEmitEffect(keeper, "Bonfire", nullptr);
            }
            if (!damagekeep->mIsPreventDamage) {
                damagekeep->activatePreventDamage();
                damagekeep->mRemainingInvincibility = INT_MAX;
            }
            exl::patch::CodePatcher moonMovPatcher(0x41B700);
            moonMovPatcher.WriteInst(0xAA0803E0);

        } else {
            damagekeep->mRemainingInvincibility = 0;
            exl::patch::CodePatcher normalMovPatcher(0x41B700);
            normalMovPatcher.WriteInst(0x9A961100);
        }
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

struct InputIsTriggerActionXexclusivelyHook : public mallow::hook::Trampoline<InputIsTriggerActionXexclusivelyHook>{
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

struct InputIsTriggerActionCameraResetHook : public mallow::hook::Trampoline<InputIsTriggerActionCameraResetHook>{
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

struct PlayerActorHakoniwaExeJump : public mallow::hook::Trampoline<PlayerActorHakoniwaExeJump> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {

        auto* anim   = thisPtr->mAnimator;
        auto* model = thisPtr->mModelHolder->findModelActor("Mario");
        auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

        if (!isBrawl) {
            Orig(thisPtr);
            return;
        }

        bool wasGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool wasWater = al::isInWater(thisPtr);

        Orig(thisPtr);

        bool isGround =  rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool isWater  =  al::isInWater(thisPtr);
        bool isAir     = !isGround || !isWater;

        if (wasWater || (wasGround && isAir)) isDoubleJump = false;

        if (isAir && !isDoubleJump && (al::isPadTriggerA(-1) || al::isPadTriggerB(-1))) {

            isDoubleJump = true;

            if (!al::isEffectEmitting(keeper, "DoubleJump")) al::tryEmitEffect(keeper, "DoubleJump", nullptr);
            
            al::setNerve(thisPtr, getNerveAt(nrvHakoniwaJump));
        }

        if (al::isFirstStep(thisPtr) && isDoubleJump) anim->startAnim("PoleHandStandJump");
    }
};

struct PlayerStateJumpTryCountUp : public mallow::hook::Trampoline<PlayerStateJumpTryCountUp>{
    static void Callback(PlayerStateJump* state, PlayerContinuousJump* cont) {

        if (isBrawl) return;

        Orig(state, cont);
    }
};

struct PlayerActorHakoniwaExeHeadSliding : public mallow::hook::Trampoline<PlayerActorHakoniwaExeHeadSliding> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);
                
        auto* anim   = thisPtr->mAnimator;
        auto* model = thisPtr->mModelHolder->findModelActor("Mario");
        auto* cape = al::getSubActor(model, "ケープ");
        auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

        if (!isBrawl && !isSuper && !isFeather) return;

        float vy = al::getVelocity(thisPtr).y;
        if (vy < -2.5f)
        al::setVelocityY(thisPtr, -2.5f);

        float speed = al::calcSpeed(thisPtr);

        if (al::isFirstStep(thisPtr)) {
            anim->endSubAnim();

            if (isBrawl && cape && al::isDead(cape)) {

                cape->appear();

                if (!al::isEffectEmitting(keeper, "AppearBloom")) al::tryEmitEffect(keeper, "AppearBloom", nullptr);
            }

            anim->startAnim("JumpBroad8");
        }
        else if (anim->isAnim("JumpBroad8") && anim->isAnimEnd()) {
            anim->startAnim("CapeGlide");
        }
        else if (speed < 10.f) {
            if (anim->isAnim("CapeGlide")) anim->startAnim("CapeGlideFloatStart");
            
            if (anim->isAnim("CapeGlideFloatStart") && anim->isAnimEnd()) {
                anim->startAnim("CapeGlideFloat");
                if (isSuper) anim->startUpperBodyAnim("AngryWaitFall");
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

struct PlayerHeadSlidingKill : public mallow::hook::Trampoline<PlayerHeadSlidingKill>{
    static void Callback(PlayerStateHeadSliding * state) {

        isCapeActive = 1200;
        if (state->mAnimator) state->mAnimator->clearUpperBodyAnim();
        Orig(state);
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    mallow::init::installHooks();
    // Mario's movement that checks ever frame
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");

    // Remove Cappy eyes while ide
    exl::patch::CodePatcher eyePatcher(0x41F7E4);
    eyePatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W0, 0));
    
    // Initialize player actor
    PlayerActorHakoniwaInitPlayer::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");
    PlayerActorHakoniwaInitAfterPlacement::InstallAtSymbol("_ZN19PlayerActorHakoniwa18initAfterPlacementEv");

    // Change Mario's idle
    PlayerStateWaitExeWait::InstallAtSymbol("_ZN15PlayerStateWait7exeWaitEv");
    
    // Handles Mario's double jump
    PlayerActorHakoniwaExeJump::InstallAtSymbol("_ZN19PlayerActorHakoniwa7exeJumpEv");
    PlayerStateJumpTryCountUp::InstallAtSymbol("_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump");
    
    // Handles Mario's glide
    PlayerActorHakoniwaExeHeadSliding::InstallAtSymbol("_ZN19PlayerActorHakoniwa14exeHeadSlidingEv");
    PlayerHeadSlidingKill::InstallAtSymbol("_ZN22PlayerStateHeadSliding4killEv");

    // Disable invincibility music patches
    exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
    invincibleStartPatcher.WriteInst(0x1F2003D5);  // NOP
    exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
    invinciblePatcher.WriteInst(0x1F2003D5);       // NOP

    // Disable R Reset Camera
    TriggerCameraReset::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");

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

    // Allow carrying an object during a GalaxySpin
    PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);
    PlayerCarryKeeperIsCarryDuringSwimSpin::InstallAtOffset(0x489EE8);
    PlayerCarryKeeperStartThrowNoSpin::InstallAtSymbol("_ZN17PlayerCarryKeeper10startThrowEb");

    // Allow triggering spin on roll and squat
    PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");

    // Allow triggering another spin while falling from a spin
    exl::patch::CodePatcher fakethrowPatcher(0x423B80);
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.Seek(0x423B9C);
    fakethrowPatcher.BranchInst(reinterpret_cast<void*>(&tryCapSpinAndRethrow));

    // Do not cancel momentum on spin
    //PlayerConstGetSpinAirSpeedMax::InstallAtSymbol("_ZNK11PlayerConst18getSpinAirSpeedMaxEv");
    //PlayerConstGetSpinBrakeFrame::InstallAtSymbol("_ZNK11PlayerConst17getSpinBrakeFrameEv");

    // Send out attack messages during spins
    PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");

    // Disable Y button for everything else
    // PadTriggerYHook::InstallAtSymbol("_ZN2al13isPadTriggerYEi");
    InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");
    // InputIsTriggerActionCameraResetHook::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");
    
    // Manually allow hacks and "special things" to use Y button
    exl::patch::CodePatcher yButtonPatcher(0x44C9FC);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerHackAction
    yButtonPatcher.Seek(0x44C718);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerAction
    yButtonPatcher.Seek(0x44C5F0);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerCarryStart
        
    DisallowCancelOnUnderwaterSpinPatch::InstallAtOffset(0x489F30);
    DisallowCancelOnWaterSurfaceSpinPatch::InstallAtOffset(0x48A3C8);
}