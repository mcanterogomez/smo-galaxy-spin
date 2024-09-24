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

const uintptr_t nrvPlayerActorHakoniwaDemo = 0x01d789f8; // Locks player in place

int framesAfterLastSpin = 0;

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook>{
    static void Callback(PlayerActorHakoniwa* player){
        PlayerAnimator* animator = player->mPlayerAnimator;
        if(!animator)
            return Orig(player);

        if(framesAfterLastSpin > 20)
            al::invalidateHitSensor(player, "GalaxySpin");

        const char* curMainAnim = player->mPlayerAnimator->mCurrentAnim;

        bool isActionCanStartSpin = !al::isEqualSubString(curMainAnim, "Motorcycle") && !al::isEqualSubString(curMainAnim, "SphinxRide"); 

        if(!player->mPlayerHackKeeper->mCurrentHackActor && al::isPadTriggerY(100) && framesAfterLastSpin > 30 && !al::isNerve(player, getNerveAt(nrvPlayerActorHakoniwaDemo)) && isActionCanStartSpin && !PlayerFunction::isPlayerDeadStatus(player)){
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
