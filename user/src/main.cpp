#include <mallow/config.hpp>
#include <mallow/mallow.hpp>

#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerFunction.h"
#include "Library/LiveActor/ActorSensorMsgFunction.h"
#include "Library/Controller/InputFunction.h"
#include "Library/LiveActor/ActorSensorFunction.h"
#include "Library/LiveActor/ActorPoseKeeper.h"
#include "Library/Base/StringUtil.h"
#include "Player/PlayerAnimator.h"
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

const uintptr_t disallowedPlayerNrvOffsets[] = {
    // Fall, Wait, Squat, Run
    0x01d78930, // Slope
    // Rolling, SpinCap, Jump, CapCatch, WallAir
    0x01d78960, // WallCatch
    0x01d78968, // GrabCeil
    0x01d78970, // PoleClimb
    0x01d78978, // HipDrop
    //0x01d78980, // HeadSliding
    // LongJump, SandSink, SandGeyser, Rise, Swim
    0x01d789b0, // Damage
    0x01d789b8, // DamageSwim
    0x01d789c0, // DamageFire
    0x01d789c8, // Press
    0x01d789d0, // Hack
    0x01d789d8, // EndHack
    0x01d789e0, // Bind
    0x01d789e8, // Camera
    0x01d789f0, // Abyss
    0x01d789f8, // Demo
    0x01d78a00, // Dead
};


int framesAfterLastSpin = 0;

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook>{
    static void Callback(PlayerActorHakoniwa* player){
        PlayerAnimator* animator = player->mPlayerAnimator;
        if(!animator)
            return Orig(player);

        if(framesAfterLastSpin > 20)
            al::invalidateHitSensor(player, "GalaxySpin");

        const char* curMainAnim = player->mPlayerAnimator->mCurrentAnim;

        bool isInForbiddenState = false;
        for(int i = 0; i < sizeof(disallowedPlayerNrvOffsets) / sizeof(uintptr_t); i++){
            if (al::isNerve(player, getNerveAt(disallowedPlayerNrvOffsets[i]))) {
                isInForbiddenState = true;
                break;
            }
        }

        if(al::isPadTriggerY(100) && framesAfterLastSpin > 30 && !isInForbiddenState){
            animator->startSubAnim("SpinSeparate");
            al::validateHitSensor(player, "GalaxySpin");
            framesAfterLastSpin = 0;
        }
        framesAfterLastSpin++;
        Orig(player);
    }
};

namespace rs {
    bool sendMsgHackAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapTrampolineAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgHammerBrosHammerEnemyAttack(al::HitSensor* receiver, al::HitSensor* sender);
}

struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* target, al::HitSensor* source){
        if(al::isSensorName(target, "GalaxySpin") && thisPtr->mPlayerAnimator && al::isEqualString(thisPtr->mPlayerAnimator->mCurrentSubAnim, "SpinSeparate")){
            if(rs::sendMsgCapTrampolineAttack(source, target) || al::sendMsgEnemyAttackFire(source, target, nullptr) || al::sendMsgExplosion(source, target, nullptr) || rs::sendMsgHackAttack(source, target) || rs::sendMsgHammerBrosHammerEnemyAttack(source, target)){
                //al::invalidateHitSensor(thisPtr, "GalaxySpin");
                return;
            }
        }
        Orig(thisPtr, target, source);
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
    //nnMainHook::InstallAtSymbol("nnMain");
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
    PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");
    PadTriggerYHook::InstallAtSymbol("_ZN2al13isPadTriggerYEi");
}
