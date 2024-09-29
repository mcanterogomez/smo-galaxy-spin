#include <mallow/config.hpp>
#include <mallow/logging/logger.hpp>
#include <mallow/mallow.hpp>

#include "Player/PlayerActionGroundMoveControl.h"
#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerStateSpinCap.h"
#include "Player/PlayerStateSwim.h"
#include "Player/PlayerTrigger.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerFunction.h"
#include "Library/LiveActor/ActorSensorMsgFunction.h"
#include "Library/Controller/InputFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/LiveActor/ActorSensorFunction.h"
#include "Library/LiveActor/ActorPoseKeeper.h"
#include "Library/Math/MathAngleUtil.h"
#include "Library/Base/StringUtil.h"
#include "Library/Nerve/NerveSetupUtil.h"
#include "Player/PlayerAnimator.h"
#include "Util/PlayerCollisionUtil.h"
#include "Library/Base/StringUtil.h"
#include "Library/Nerve/NerveUtil.h"

static void setupLogging() {
    using namespace mallow::log::sink;
    // This sink writes to a file on the SD card.
    static FileSink fileSink = FileSink("sd:/mallow.log");
    addLogSink(&fileSink);

    // This sink writes to a network socket on a host computer. Raw logs are sent with no
    auto config = mallow::config::getConfig();
    if (config["logger"]["ip"].is<const char*>()) {
        static NetworkSink networkSink = NetworkSink(
            config["logger"]["ip"],
            config["logger"]["port"] | 3080
        );
        if (networkSink.isSuccessfullyConnected())
            addLogSink(&networkSink);
        else
            mallow::log::logLine("Failed to connect to the network sink");
    } else {
        mallow::log::logLine("The network logger is unconfigured.");
        if (config["logger"].isNull()) {
            mallow::log::logLine("Please configure the logger in config.json");
        } else if (!config["logger"]["ip"].is<const char*>()) {
            mallow::log::logLine("The IP address is missing or invalid.");
        }
    }
}

using mallow::log::logLine;

//Mod code

const al::Nerve* getNerveAt(uintptr_t offset)
{
    return (const al::Nerve*)((((u64)malloc) - 0x00724b94) + offset);
}

al::HitSensor* hitBuffer[0x40];
int hitBufferCount = 0;

const uintptr_t spinCapNrvOffset = 0x1d78940;


bool triggerGalaxySpin = false;

struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack>{
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        triggerGalaxySpin = false;
        if (al::isPadTriggerY(100)) {
            if (!player->mPlayerAnimator->isAnim("SpinSeparate")) {
                triggerGalaxySpin = true;
                return true;
            }
            return false;
        }

        return Orig(player, a2);
    }
};

class PlayerStateSpinCapNrvGalaxySpinGround : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();

        if(al::isFirstStep(state)) {
            state->mAnimator->startAnim("SpinSeparate");
            al::validateHitSensor(state->mActor, "GalaxySpin");
        }

        state->updateSpinGroundNerve();

        if(al::isStep(state, 20)) {
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
            state->mAnimator->startAnim("SpinSeparate");
            al::validateHitSensor(state->mActor, "GalaxySpin");
        }

        state->updateSpinAirNerve();

        if(al::isStep(state, 20)) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
        }

        if(state->mAnimator->isAnimEnd()) {
            state->kill();
        }
    }
};

PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;
PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;

struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear>{
    static void Callback(PlayerStateSpinCap* state){
        mallow::log::logLine("PlayerSpinCapAttackAppear: shouldTriggerGalaxySpin: %d", triggerGalaxySpin);
        if(!triggerGalaxySpin){
            Orig(state);
            return;
        }
        hitBufferCount = 0;

        // ----------------
        // MODIFIED FROM PlayerStateSpinCap::appear
        bool v2 = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val33);
        state->mIsDead = false;
        state->mIsInWater = false;
        state->_99 = 0;
        state->_80 = 0;
        state->_9C = {0.0f, 0.0f, 0.0f};
        state->_A8 = 0;
        // TODO set something on JudgeWaterSurfaceRun
        state->_A9 = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val0);
        bool v10 =
            rs::isOnGround(state->mActor, state->mCollider) && !state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val2);

        if (v2 || v10) {
            /*if (rs::isOnGroundSkateCode(mActor, mCollider))
                mSpinCapAttack->clearAttackInfo();*/

            if (state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val1)) {
                al::alongVectorNormalH(al::getVelocityPtr(state->mActor), al::getVelocity(state->mActor),
                                        -al::getGravity(state->mActor), rs::getCollidedGroundNormal(state->mCollider));
            }

            state->mActionGroundMoveControl->appear();
            /*mSpinCapAttack->setupAttackInfo();

            if (mSpinCapAttack->isSeparateSingleSpin())
                al::setNerve(this, &SpinGroundSeparate);
            else
                al::setNerve(this, &SpinGround);
            return;*/
            al::setNerve(state, &GalaxySpinGround); // <- new
        } else {
            state->_78 = 1;
            /*mSpinCapAttack->setupAttackInfo();
            if (mSpinCapAttack->isSeparateSingleSpin())
                al::setNerve(this, &SpinAirSeparate);
            else
                al::setNerve(this, &SpinAir);*/
            al::setNerve(state, &GalaxySpinAir);  // <- new
            return;
        }
        // ----------------------
        // END MODIFIED CODE
    }
};

struct PlayerConstGetSpinAirSpeedMax : public mallow::hook::Trampoline<PlayerConstGetSpinAirSpeedMax> {
    static float Callback(PlayerConst* playerConst) {
        return playerConst->getNormalMaxSpeed();
    }
};

struct PlayerStateSwimStartCapThrow : public mallow::hook::Trampoline<PlayerStateSwimStartCapThrow>{
    static void Callback(PlayerStateSwim* state) {
        if(triggerGalaxySpin){
            al::setNerve(state->mActor, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(state);
    }
};

struct PlayerStateSwimStartCapThrowSurface : public mallow::hook::Trampoline<PlayerStateSwimStartCapThrowSurface>{
    static void Callback(PlayerStateSwim* state) {
        if(triggerGalaxySpin){
            al::setNerve(state->mActor, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(state);
    }
};

namespace rs {
    bool sendMsgHackAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapTrampolineAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgHammerBrosHammerEnemyAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapReflect(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapAttack(al::HitSensor* receiver, al::HitSensor* sender);
}

struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* target, al::HitSensor* source){
        if(al::isSensorName(target, "GalaxySpin") && thisPtr->mPlayerAnimator && al::isEqualString(thisPtr->mPlayerAnimator->mCurrentAnim, "SpinSeparate")){
            bool isInHitBuffer = false;
            for(int i = 0; i < hitBufferCount; i++){
                if(hitBuffer[i] == source){
                    isInHitBuffer = true;
                    break;
                }
            }
            if(!isInHitBuffer){
                hitBuffer[hitBufferCount++] = source;
                if(rs::sendMsgCapTrampolineAttack(source, target) || al::sendMsgEnemyAttackFire(source, target, nullptr) || al::sendMsgExplosion(source, target, nullptr) || rs::sendMsgHackAttack(source, target) || rs::sendMsgHammerBrosHammerEnemyAttack(source, target) || rs::sendMsgCapReflect(source, target) || rs::sendMsgCapAttack(source, target))
                    return;
            }
        }
        Orig(thisPtr, target, source);
    }
};

struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        if(al::isPadTriggerY(100) && !thisPtr->mPlayerAnimator->isAnim("SpinSeparate")) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};

struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        if(al::isPadTriggerY(100) && !thisPtr->mPlayerAnimator->isAnim("SpinSeparate")) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};


struct PadTriggerYHook : public mallow::hook::Trampoline<PadTriggerYHook>{
    static bool Callback(int port){
        if(port == 100)
            return Orig(-1);
        return false;
    };
};

struct nnMainHook : public mallow::hook::Trampoline<nnMainHook>{
    static void Callback(){
        nn::fs::MountSdCardForDebug("sd");
        mallow::config::loadConfig(true);

        setupLogging();
        //logLine("Hello from smo!");
        Orig();
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    nnMainHook::InstallAtSymbol("nnMain");
    //PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");

    // trigger spin instead of cap throw
    PlayerTryActionCapSpinAttack::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
    PlayerSpinCapAttackAppear::InstallAtSymbol("_ZN18PlayerStateSpinCap6appearEv");
    PlayerStateSwimStartCapThrow::InstallAtSymbol("_ZN15PlayerStateSwim13startCapThrowEv");
    PlayerStateSwimStartCapThrowSurface::InstallAtSymbol("_ZN15PlayerStateSwim20startCapThrowSurfaceEv");

    // allow triggering spin on roll and squat
    PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");

    // do not cancel momentum on spin
    PlayerConstGetSpinAirSpeedMax::InstallAtSymbol("_ZNK11PlayerConst18getSpinAirSpeedMaxEv");

    // send out attack messages during spins
    PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");

    // disable Y button for everything else
    PadTriggerYHook::InstallAtSymbol("_ZN2al13isPadTriggerYEi");
}
