#include <exl/hook/base.hpp>
#include <mallow/config.hpp>
#include <mallow/init/initLogging.hpp>
#include <mallow/logging/logger.hpp>
#include <mallow/mallow.hpp>

#include "ModOptions.h"
#include "Player/PlayerActorHakoniwa.h"
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

#include "System/GameDataFunction.h"
#include "Player/PlayerDamageKeeper.h"
#include <limits.h>

// Hook to grant infinite invincibility when Mario is in the Super suit
struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        // Run original movement logic first
        Orig(thisPtr);

        // Check for Super suit costume and cap
        const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
        const char* cap     = GameDataFunction::getCurrentCapTypeName(thisPtr);
        bool isSuper = (costume && al::isEqualString(costume, "MarioColorSuper"))
                    && (cap     && al::isEqualString(cap,     "MarioColorSuper"));

        // Apply or remove invincibility
        PlayerDamageKeeper* damagekeep = thisPtr->mPlayerDamageKeeper;

        if (isSuper) {
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

extern "C" void userMain() {
    // Initialize hooks system
    exl::hook::Initialize();

    // Install our movement hook
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");

    // Disable invincibility music patches
    exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
    invincibleStartPatcher.WriteInst(0x1F2003D5);  // NOP
    exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
    invinciblePatcher.WriteInst(0x1F2003D5);       // NOP
}