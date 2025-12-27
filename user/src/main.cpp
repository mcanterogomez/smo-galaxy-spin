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
#include "Player/PlayerCounterForceRun.h"
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
#include "actors/custom/FireBall.h"
#include "actors/custom/HammerBrosHammer.h"
#include "actors/custom/PlayerAnimator.h"
#include "actors/custom/PlayerDamageKeeper.h"
#include "actors/custom/PlayerJudgeWallHitDown.h"
#include "actors/custom/PlayerStateJump.h"
#include "actors/custom/PlayerStateWait.h"
#include "ModOptions.h"
#include "math/seadVectorFwd.h"

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

const uintptr_t nrvHakoniwaFall = 0x01d78910;

static PlayerActorHakoniwa* isHakoniwa = nullptr; // Global pointer for Hakoniwa

// Global flags to track states
bool isMario = false;
bool isFeather = false;
bool isFire = false;
bool isTanooki = false;
bool isBrawl = false;
bool isSuper = false;

al::LiveActorGroup* fireBalls = nullptr; // Global pointer for fireballs
bool nextThrowLeft = true; // Global flag to track next throw direction
bool canFireball = false; // Global flag to track fireball trigger
int fireStep = -1;
static inline bool isFireThrowing() { return fireStep >= 0; }

bool prevIsCarry = false;

struct PlayerActorHakoniwaInitPlayer : public mallow::hook::Trampoline<PlayerActorHakoniwaInitPlayer> {
    static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        Orig(thisPtr, actorInfo, playerInfo);

        // Set Hakoniwa pointer
        isHakoniwa = thisPtr;
        auto* model = thisPtr->mModelHolder->findModelActor("Normal");

        // Create and hide fireballs
        fireBalls = new al::LiveActorGroup("FireBrosFireBall", 4);
        while (!fireBalls->isFull()) {
            auto* fb = new FireBrosFireBall("FireBall", model);
            al::initCreateActorNoPlacementInfo(fb, *actorInfo);
            fireBalls->registerActor(fb);
        }
        fireBalls->makeActorDeadAll();

        // Check for Super suit costume and cap
        const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
        const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);

        isMario = (costume && al::isEqualString(costume, "Mario"))
            && (cap && al::isEqualString(cap, "Mario"));
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

struct PlayerActorHakoniwaInitAfterPlacement : public mallow::hook::Trampoline<PlayerActorHakoniwaInitAfterPlacement> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        if (fireBalls) fireBalls->makeActorDeadAll();
    }
};

struct TriggerCameraReset : public mallow::hook::Trampoline<TriggerCameraReset> {
    static bool Callback(al::LiveActor* actor, int port) {
        if (isFire && al::isPadTriggerR(-1)) return false;
        return Orig(actor, port);
    }
};

// shared pre-logic for TryActionCapSpinAttack hooks
static inline int TryCapSpinPre(PlayerActorHakoniwa* player) {
    if (isFireThrowing()) return -1;

    bool newIsCarry = player->mCarryKeeper->isCarry();
    if (newIsCarry && !prevIsCarry) { prevIsCarry = newIsCarry; return -1; }
    prevIsCarry = newIsCarry;

    if (al::isPadTriggerR(-1)
        && !rs::is2D(player)
        && !player->mCarryKeeper->isCarry()
        && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) canFireball = true;

    return 0; // fallthrough to Orig
}

struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack> {
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        int pre = TryCapSpinPre(player);
        if (pre != 0) return pre > 0;
        return Orig(player, a2);
    }
};

struct PlayerTryActionCapSpinAttackBindEnd : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttackBindEnd> {
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        int pre = TryCapSpinPre(player);
        if (pre != 0) return pre > 0;
        return Orig(player, a2);
    }
};

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);

        auto* anim   = thisPtr->mAnimator;
        auto* holder = thisPtr->mModelHolder;
        auto* model  = holder->findModelActor("Normal");

        // Handle fireball attack
        const char* jointName = nextThrowLeft ? "HandL" : "HandR";
        const char* fireAnim  = nextThrowLeft ? "FireL" : "FireR";

        FireBrosFireBall* fireBall = (FireBrosFireBall*) fireBalls->getDeadActor();
        bool isMove = thisPtr->mInput->isMove();
        bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool isWater = al::isInWater(thisPtr);
        bool isSurface = thisPtr->mWaterSurfaceFinder->isFoundSurface();

        bool isFullBody = (!isMove && onGround && (!isWater || isSurface));
        bool isFloating = al::isActionPlaying(model, "GlideFloat")
            || al::isActionPlaying(model, "GlideFloatSuper");

        if (isFire
        ) {
            if (fireStep < 0
                && (canFireball || isFloating)
                && al::isPadTriggerR(-1)
            ) {
                if (fireBall && al::isDead(fireBall)
                ) {
                    fireStep = 0;
                    canFireball = false;

                    anim->startUpperBodyAnim(fireAnim);
                    if (isFullBody) anim->startAnim(fireAnim);
                }
            }
            if (fireStep >= 0
            ) {
                bool isShooting = anim->isUpperBodyAnim("FireL") || anim->isUpperBodyAnim("FireR")
                    || anim->isAnim("FireL") || anim->isAnim("FireR");

                if (fireStep >= 0 && !isShooting) { fireStep = -1; return; }

                if (fireStep == 2
                ) {
                    sead::Vector3f startPos;
                    al::calcJointPos(&startPos, model, jointName);
                    sead::Vector3f offset(0.0f, 0.0f, 0.0f);

                    fireBall->shoot(startPos, al::getQuat(model), offset, true, 0, false);
                    al::tryStartSe(thisPtr, "FireBallShoot");

                    nextThrowLeft = !nextThrowLeft;
                }
                if (isFullBody ? anim->isAnimEnd() : anim->isUpperBodyAnimEnd()
                ) {
                    if (isFullBody) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                    anim->clearUpperBodyAnim();
                    fireStep = -1;
                }
                else fireStep++;
            }
        }
        canFireball = false;
    }
};

struct FireballAttackSensorHook : public mallow::hook::Trampoline<FireballAttackSensorHook> {
    static void Callback(FireBrosFireBall* thisPtr, al::HitSensor* source, al::HitSensor* target) {
        if (!thisPtr || !source || !target) return;

        al::LiveActor* sourceHost = al::getSensorHost(source);
        al::LiveActor* targetHost = al::getSensorHost(target);
        
        if (!sourceHost || !targetHost) return;
        if (targetHost == isHakoniwa) return;

        Orig(thisPtr, source, target);
    }
};

struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        if (isFireThrowing()) return;

        Orig(thisPtr);
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    mallow::init::installHooks();

    // Disable R Reset Camera
    TriggerCameraReset::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");

    // Initialize player actor
    PlayerActorHakoniwaInitPlayer::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");
    PlayerActorHakoniwaInitAfterPlacement::InstallAtSymbol("_ZN19PlayerActorHakoniwa18initAfterPlacementEv");

    // Trigger spin instead of cap throw
    PlayerTryActionCapSpinAttack::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
    PlayerTryActionCapSpinAttackBindEnd::InstallAtSymbol("_ZN19PlayerActorHakoniwa29tryActionCapSpinAttackBindEndEv");

    // Mario specific hooks
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");
}