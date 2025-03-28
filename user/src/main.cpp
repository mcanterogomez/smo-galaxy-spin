#include <exl/hook/base.hpp>
#include <mallow/config.hpp>
#include <mallow/init/initLogging.hpp>
#include <mallow/logging/logger.hpp>
#include <mallow/mallow.hpp>

#include "ModOptions.h"
#include "Player/PlayerActionGroundMoveControl.h"
#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerSpinCapAttack.h"
#include "Player/PlayerColliderHakoniwa.h"
#include "Player/PlayerStateSpinCap.h"
#include "Player/PlayerStateSwim.h"
#include "Player/PlayerTrigger.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerFunction.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/Controller/InputFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/ActorSensorFunction.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/Math/MathUtil.h"
#include "Library/Base/StringUtil.h"
#include "Project/HitSensor/HitSensor.h"
#include "Player/PlayerAnimator.h"
#include "Util/PlayerCollisionUtil.h"
#include "Library/Base/StringUtil.h"
#include "Library/Nerve/NerveUtil.h"
#include "Player/PlayerModelHolder.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/Nerve/NerveSetupUtil.h"

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

//Mod code

const al::Nerve* getNerveAt(uintptr_t offset)
{
    return (const al::Nerve*)((((u64)malloc) - 0x00724b94) + offset);
}

bool isPadTriggerGalaxySpin(int port){
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

int galaxySensorRemaining = -1;

struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack>{
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        // do not allow Y to trigger both pickup and spin on seeds (for picking up rocks, this function is not called)
        bool newIsCarry = player->mPlayerCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) {
            prevIsCarry = newIsCarry;
            return false;
        }
        prevIsCarry = newIsCarry;
        if (isPadTriggerGalaxySpin(-1) && !rs::is2D(player) && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mPlayerEquipmentUser)) {
            if(player->mPlayerAnimator->isAnim("SpinSeparate"))
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
        bool newIsCarry = player->mPlayerCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) {
            prevIsCarry = newIsCarry;
            return false;
        }
        prevIsCarry = newIsCarry;
        if (isPadTriggerGalaxySpin(-1) && !rs::is2D(player) && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mPlayerEquipmentUser)) {
            if(player->mPlayerAnimator->isAnim("SpinSeparate"))
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

        if(al::isFirstStep(state)) {
            state->mAnimator->endSubAnim();

            state->mAnimator->startSubAnim("SpinSeparate");
            state->mAnimator->startAnim("SpinSeparate");

            al::validateHitSensor(state->mActor, "GalaxySpin");
            galaxySensorRemaining = 21;
        }

        state->updateSpinGroundNerve();

        if(al::isGreaterStep(state, 21)) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
        }

        if(state->mAnimator->isAnimEnd()) {
            state->kill();
        }
    }
};

class PlayerStateSpinCapNrvGalaxySpinAir : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();

        if(al::isFirstStep(state)) {
            state->mAnimator->startSubAnim("SpinSeparate");
            state->mAnimator->startAnim("SpinSeparate");
            al::validateHitSensor(state->mActor, "GalaxySpin");
            galaxySensorRemaining = 21;
        }

        state->updateSpinAirNerve();

        if(al::isGreaterStep(state, 21)) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
        }
    }
};

PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;
PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;

struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear> {
    static void Callback(PlayerStateSpinCap* state) {
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

        // Decide between ground or air Galaxy spin
        const bool isGrounded =
            rs::isOnGround(state->mActor, state->mCollider) &&
            !state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val2);
        const bool forcedGroundSpin =
            state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val33);

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
    static void Callback(PlayerStateSpinCap* state){
        Orig(state);
        canStandardSpin = true;
        canGalaxySpin = true;
        galaxyFakethrowRemainder = -1;
        // do not invalidate hitsensor/clear `isGalaxySpin`,
        // because Mario might go into a jump, which should continue the spin
    }
};

struct PlayerStateSpinCapFall : public mallow::hook::Trampoline<PlayerStateSpinCapFall>{
    static void Callback(PlayerStateSpinCap* state){
        Orig(state);

        if(galaxyFakethrowRemainder == -2) {
            galaxyFakethrowRemainder = 21;
            al::validateHitSensor(state->mActor, "GalaxySpin");
            state->mAnimator->startSubAnim("SpinSeparate");
            state->mAnimator->startAnim("SpinSeparate");
            galaxySensorRemaining = 21;
        }
        else if(galaxyFakethrowRemainder > 0) {
            galaxyFakethrowRemainder--;
        }
        else if(galaxyFakethrowRemainder == 0) {
            galaxyFakethrowRemainder = -1;
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
        }
    }
};

struct PlayerStateSpinCapIsEnableCancelHipDrop : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelHipDrop>{
    static bool Callback(PlayerStateSpinCap* state){
        return Orig(state) || (al::isNerve(state, &GalaxySpinAir) && al::isGreaterStep(state, 10));
    }
};

struct PlayerStateSpinCapIsEnableCancelAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelAir>{
    static bool Callback(PlayerStateSpinCap* state){
        return Orig(state) && !(!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
    }
};

struct PlayerStateSpinCapIsSpinAttackAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsSpinAttackAir>{
    static bool Callback(PlayerStateSpinCap* state){
        return Orig(state) || (!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
    }
};

struct PlayerStateSpinCapIsEnableCancelGround : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelGround>{
    static bool Callback(PlayerStateSpinCap* state){
        return Orig(state) || (al::isNerve(state, &GalaxySpinGround) && al::isGreaterStep(state, 10));
    }
};

struct PlayerConstGetSpinAirSpeedMax : public mallow::hook::Trampoline<PlayerConstGetSpinAirSpeedMax> {
    static float Callback(PlayerConst* playerConst) {
        if(isGalaxySpin)
            return playerConst->getNormalMaxSpeed();
        return Orig(playerConst);
    }
};

struct PlayerConstGetSpinBrakeFrame : public mallow::hook::Trampoline<PlayerConstGetSpinBrakeFrame> {
    static s32 Callback(PlayerConst* playerConst) {
        if(isGalaxySpin)
            return 0;
        return Orig(playerConst);
    }
};

// used in swimming, which also calls tryActionCapSpinAttack before, so just assume isGalaxySpin is properly set up
struct PlayerSpinCapAttackIsSeparateSingleSpin : public mallow::hook::Trampoline<PlayerSpinCapAttackIsSeparateSingleSpin>{
    static bool Callback(PlayerStateSwim* thisPtr){
        if(triggerGalaxySpin) {
            return true;
        }
        return Orig(thisPtr);
    }
};

struct PlayerStateSwimExeSwimSpinCap : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCap>{
    static void Callback(PlayerStateSwim* thisPtr){
        Orig(thisPtr);
        if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
            al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
            hitBufferCount = 0;
            isGalaxySpin = true;
            triggerGalaxySpin = false;
        }
        if(isGalaxySpin && (al::isGreaterStep(thisPtr, 62) || al::isStep(thisPtr, -1))) {
            al::invalidateHitSensor(thisPtr->mActor, "GalaxySpin");
            isGalaxySpin = false;
        }
    }
};

struct PlayerStateSwimExeSwimSpinCapSurface : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCapSurface>{
    static void Callback(PlayerStateSwim* thisPtr){
        Orig(thisPtr);
        if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
            al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
            hitBufferCount = 0;
            isGalaxySpin = true;
            triggerGalaxySpin = false;
        }
        if(isGalaxySpin && (al::isGreaterStep(thisPtr, 62) || al::isStep(thisPtr, -1))) {
            al::invalidateHitSensor(thisPtr->mActor, "GalaxySpin");
            isGalaxySpin = false;
        }
    }
};

struct PlayerStateSwimExeSwimHipDropHeadSliding : public mallow::hook::Trampoline<PlayerStateSwimExeSwimHipDropHeadSliding>{
    static void Callback(PlayerStateSwim* thisPtr){
        Orig(thisPtr);
        if(isPadTriggerGalaxySpin(-1))
            if(((PlayerActorHakoniwa*)thisPtr->mActor)->tryActionCapSpinAttackImpl(true))
                thisPtr->startCapThrow();
    }
};

struct PlayerStateSwimKill : public mallow::hook::Trampoline<PlayerStateSwimKill>{
    static void Callback(PlayerStateSwim* state){
        Orig(state);
        isGalaxySpin = false;
        al::invalidateHitSensor(state->mActor, "GalaxySpin");
    }
};

struct PlayerSpinCapAttackStartSpinSeparateSwimSurface : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwimSurface>{
    static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator){
        if(!isGalaxySpin && !triggerGalaxySpin) {
            Orig(thisPtr, animator);
            return;
        }

        animator->startSubAnim("SpinSeparateSwim");
        animator->startAnim("SpinSeparateSwim");
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
}

struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* target, al::HitSensor* source){

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

        if(al::isSensorName(target, "GalaxySpin") && thisPtr->mPlayerAnimator && (al::isEqualString(thisPtr->mPlayerAnimator->mCurrentAnim, "SpinSeparate") || isGalaxySpin)){
            bool isInHitBuffer = false;
            for(int i = 0; i < hitBufferCount; i++){
                if(hitBuffer[i] == sourceHost){
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
                isInHitBuffer &= sourceNrv != getNerveAt(0x1CD6758);
                isInHitBuffer &= sourceNrv != getNerveAt(0x1CD4BB0);
                isInHitBuffer &= sourceNrv != getNerveAt(0x1CD6FA0);
                isInHitBuffer &= sourceNrv != getNerveAt(0x1D170D0);

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
            if(!isInHitBuffer){
                    if (al::isEqualSubString(typeid(*sourceHost).name(),"CapSwitchTimer")) {
                        al::setNerve(sourceHost, getNerveAt(0x1CE4338));
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        al::tryEmitEffect(targetHost, "Hit", &effectPos);
                        return;
                    }
                    if (al::isEqualSubString(typeid(*sourceHost).name(),"CapSwitch")) {
                        al::setNerve(sourceHost, getNerveAt(0x1CE3E18));
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        al::tryEmitEffect(targetHost, "Hit", &effectPos);
                        return;
                    }
                    if (al::isSensorNpc(source) &&
                !al::isEqualSubString(typeid(*sourceHost).name(),"YoshiFruit")){
                    if (
                        ((al::isEqualSubString(typeid(*sourceHost).name(),"RadiconCar") ||
                        al::isEqualSubString(typeid(*sourceHost).name(),"CollectAnimal")) &&
                        rs::sendMsgCapAttack(source, target)) ||

                        rs::sendMsgCapReflect(source, target) ||
                        rs::sendMsgBlowObjAttack(source, target) ||
                        al::sendMsgEnemyAttack(source, target) ||
                        al::sendMsgPlayerTrampleReflect(source, target, nullptr)
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"Frog")){
                            al::tryDeleteEffect(targetHost, "HitSmall");}
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"CollectAnimal") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;
                    }
                }
                if (al::isSensorEnemyBody(source) &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"PartsModel")){
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

                        al::sendMsgKickStoneAttackReflect(source, target) ||
                        rs::sendMsgHackAttack(source, target) ||
                        al::sendMsgExplosion(source, target, nullptr) ||
                        rs::sendMsgCapAttack(source, target) ||

                        (!al::isEqualSubString(typeid(*sourceHost).name(),"ReflectBomb") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"Gunetter") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"GunetterBody") &&
                        rs::sendMsgCapReflect(source, target))
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"BombTail")){
                            al::tryDeleteEffect(sourceHost, "CapReflect");}
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"FireBlower")){
                            al::tryDeleteEffect(sourceHost, "HitSmall");
                            al::tryDeleteEffect(sourceHost, "HitMark");}
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"Shibaken") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"MofumofuScrap") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"GrowerWorm") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"FireBlowerCap") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"CapThrowerCap") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;
                    }
                }
                if (al::isSensorMapObj(source) &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"CitySignal")){
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
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
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
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
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
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        if (al::isEqualSubString(typeid(*sourceHost).name(),"Souvenir")){
                            al::tryDeleteEffect(sourceHost, "HitSmall");}
                        (al::isEqualSubString(typeid(*sourceHost).name(),"Souvenir") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                        return;
                    }
                }   
                if (al::isSensorRide(source)){
                    if (
                        rs::sendMsgCapReflect(source, target)
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        al::tryEmitEffect(targetHost, "Hit", &effectPos);
                        return;
                    }
                }
                if (rs::tryGetCollidedWallSensor(thisPtr->mPlayerColliderHakoniwa) &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"FixMapParts") &&
                    !al::isEqualSubString(typeid(*sourceHost).name(),"CitySignal")){
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
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
                        (!al::isEqualSubString(typeid(*sourceHost).name(),"ReactionObject") &&
                        !al::isEqualSubString(typeid(*sourceHost).name(),"SphinxRide") &&
                        al::tryEmitEffect(targetHost, "Hit", &effectPos));
                    }
                    if (
                        rs::sendMsgCapReflect(source, target)
                        ){
                        hitBuffer[hitBufferCount++] = sourceHost;
                        sead::Vector3 effectPos = al::getTrans(targetHost);
                        effectPos.y += 50.0f;
                        sead::Vector3 direction = (al::getTrans(sourceHost) - al::getTrans(targetHost));
                        direction.normalize();
                        effectPos += direction * 75.0f;
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

// these are not supposed to be able to switch to capthrow mode, so check Y and current state manually
struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        if(isPadTriggerGalaxySpin(-1) && !thisPtr->mPlayerAnimator->isAnim("SpinSeparate") && canGalaxySpin) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};
struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        if(isPadTriggerGalaxySpin(-1) && !thisPtr->mPlayerAnimator->isAnim("SpinSeparate") && canGalaxySpin) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
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

struct PadTriggerYHook : public mallow::hook::Trampoline<PadTriggerYHook>{
    static bool Callback(int port){
        if(port == 100)
            return Orig(-1);
        return false;
    };
};

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        Orig(thisPtr);
        al::HitSensor* sensor = al::getHitSensor(thisPtr, "GalaxySpin");
        if(sensor && sensor->mIsValid) {
            if(rs::tryGetCollidedWallSensor(thisPtr->mPlayerColliderHakoniwa))
                thisPtr->attackSensor(sensor, rs::tryGetCollidedWallSensor(thisPtr->mPlayerColliderHakoniwa));
        }
        
        if(galaxySensorRemaining > 0) {
            galaxySensorRemaining--;
            if(galaxySensorRemaining == 0) {
                al::invalidateHitSensor(thisPtr, "GalaxySpin");
                isGalaxySpin = false;
                galaxySensorRemaining = -1;
            }
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
            if(galaxyFakethrowRemainder != -1 || player->mPlayerAnimator->isAnim("SpinSeparate"))
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
        if(isPadTriggerGalaxySpin(-1) && galaxyFakethrowRemainder == -1 && !player->mPlayerAnimator->isAnim("SpinSeparate")) {
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
            if(galaxyFakethrowRemainder != -1 || player->mPlayerAnimator->isAnim("SpinSeparate"))
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
    static bool Callback(const al::LiveActor* actor, int port){
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
    static bool Callback(const al::LiveActor* actor, int port){
        switch (mallow::config::getConfg<ModOptions>()->spinButton) {
            case 'L':
                return al::isPadTriggerR(port);
            case 'R':
                return al::isPadTriggerL(port);
        }
        return Orig(actor, port);
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
    mallow::init::installHooks();
    // trigger spin instead of cap throw
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
    // UPDATE: do not interrupt underwater dive with GalaxySpin
    //PlayerStateSwimExeSwimHipDropHeadSliding::InstallAtSymbol("_ZN15PlayerStateSwim25exeSwimHipDropHeadSlidingEv");
    PlayerStateSwimKill::InstallAtSymbol("_ZN15PlayerStateSwim4killEv");
    PlayerSpinCapAttackStartSpinSeparateSwimSurface::InstallAtSymbol("_ZN19PlayerSpinCapAttack28startSpinSeparateSwimSurfaceEP14PlayerAnimator");

    // allow carrying an object during a GalaxySpin
    PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);
    PlayerCarryKeeperIsCarryDuringSwimSpin::InstallAtOffset(0x489EE8);

    // allow triggering spin on roll and squat
    PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");

    // allow triggering another spin while falling from a spin
    exl::patch::CodePatcher fakethrowPatcher(0x423B80);
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.Seek(0x423B9C);
    fakethrowPatcher.BranchInst(reinterpret_cast<void*>(&tryCapSpinAndRethrow));

    // do not cancel momentum on spin
    PlayerConstGetSpinAirSpeedMax::InstallAtSymbol("_ZNK11PlayerConst18getSpinAirSpeedMaxEv");
    PlayerConstGetSpinBrakeFrame::InstallAtSymbol("_ZNK11PlayerConst17getSpinBrakeFrameEv");

    // send out attack messages during spins
    PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");

    // disable Y button for everything else
    //PadTriggerYHook::InstallAtSymbol("_ZN2al13isPadTriggerYEi");
    InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");
    InputIsTriggerActionCameraResetHook::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");
    // manually allow hacks and "special things" to use Y button
    exl::patch::CodePatcher yButtonPatcher(0x44C9FC);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerHackAction
    yButtonPatcher.Seek(0x44C718);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerAction
    yButtonPatcher.Seek(0x44C5F0);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerCarryStart
    
    // Remove Cappy eyes while ide
    //exl::patch::CodePatcher eyePatcher(0x41F7E4);
    //eyePatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W0, 0));
    
    DisallowCancelOnUnderwaterSpinPatch::InstallAtOffset(0x489F30);
    DisallowCancelOnWaterSurfaceSpinPatch::InstallAtOffset(0x48A3C8);
}