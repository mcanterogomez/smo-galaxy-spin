#include "custom/_Globals.h"
#include "custom/KoopaBattle.h"
#include "custom/_Nerves.h"
#include "custom/AttackSensor.h"
#include "custom/PlayerCore.h"
#include "custom/PlayerSpinAttack.h"
#include "custom/PowerUps.h"

struct TriggerCameraReset : public mallow::hook::Trampoline<TriggerCameraReset> {
    static bool Callback(al::LiveActor* actor, int port) {
        if ((isMario || isFire || isIce || isBrawl || isSuper)
            && al::isPadTriggerR(-1)) return false;

        return Orig(actor, port);
    }
};

struct TriggerAmiibo : public mallow::hook::Trampoline<TriggerAmiibo> {
    static bool Callback(const al::IUseSceneObjHolder* holder) {
        if (isBlaster) return false;
        return Orig(holder);
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    mallow::init::installHooks();

    KoopaBattle::Install();

    PlayerCore::Install();
    PlayerSpinAttack::Install();
    AttackSensor::Install();
    PowerUps::Install();

    TriggerCameraReset::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");
    TriggerAmiibo::InstallAtSymbol("_ZN2rs19isTriggerAmiiboModeEPKN2al18IUseSceneObjHolderE");

    #ifdef REMOVE_CAPPY_EYES // Remove Cappy eyes while ide
        exl::patch::CodePatcher eyePatcher(0x41F7E4);
        eyePatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W0, 0));
    #endif
}