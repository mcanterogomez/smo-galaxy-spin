#include "custom/_Globals.h"
#include "custom/CustomAnimation.h"
#include "custom/KoopaBattle.h"
#include "custom/PlayerKart.h"
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
        auto* model = isHakoniwa->mModelHolder->findModelActor("Normal");
        if (isMario && al::tryGetSubActor(model, "Blaster")) return false;

        return Orig(holder);
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    mallow::init::installHooks();

    PlayerCore::Install();
    PlayerSpinAttack::Install();
    AttackSensor::Install();
    CustomAnimation::Install();
    KoopaBattle::Install();
    PlayerKart::Install();

    #ifdef ALLOW_POWERUPS
        PowerUps::Install();
        TriggerCameraReset::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");
        TriggerAmiibo::InstallAtSymbol("_ZN2rs19isTriggerAmiiboModeEPKN2al18IUseSceneObjHolderE");
    #endif
}