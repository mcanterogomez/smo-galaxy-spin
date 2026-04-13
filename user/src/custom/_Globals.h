#pragma once
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

// Library headers
#include "Library/Base/StringUtil.h"
#include "Library/Camera/CameraUtil.h"
#include "Library/Controller/InputFunction.h"
#include "Library/Controller/SpinInputAnalyzer.h"
#include "Library/Effect/EffectKeeper.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/File/FileUtil.h"
#include "Library/HitSensor/HitSensorKeeper.h"
#include "Library/Joint/JointControllerKeeper.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/LiveActor/ActorCollisionFunction.h"
#include "Library/LiveActor/ActorClippingFunction.h"
#include "Library/LiveActor/ActorFlagFunction.h"
#include "Library/LiveActor/ActorInitInfo.h"
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
#include "Util/DemoUtil.h"
#include "Util/PlayerCollisionUtil.h"
#include "Util/PlayerUtil.h"
#include "Util/SensorMsgFunction.h"

// Player actor & state headers
#include "Player/IUsePlayerCollision.h"
#include "Player/PlayerActionGroundMoveControl.h"
#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerBindKeeper.h"
#include "Player/PlayerColliderHakoniwa.h"
#include "Player/PlayerCounterForceRun.h"
#include "Player/PlayerEquipmentUser.h"
#include "Player/PlayerFunction.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerInfo.h"
#include "Player/PlayerInput.h"
#include "Player/PlayerJointControlKeeper.h"
#include "Player/PlayerJudgeStartWaterSurfaceRun.h"
#include "Player/PlayerJudgeWaterSurfaceRun.h"
#include "Player/PlayerModelHolder.h"
#include "Player/PlayerStateHeadSliding.h"
#include "Player/PlayerStateSpinCap.h"
#include "Player/PlayerStateSwim.h"
#include "Player/PlayerTrigger.h"
#include "Player/HackCap.h"

// Mod‑specific & custom actors
#include "custom/CustomGauge.h"
#include "custom/CustomPlayerConst.h"
#include "headers/FireBall.h"
#include "headers/HammerBrosHammer.h"
#include "headers/Motorcycle.h"
#include "headers/PlayerAnimator.h"
#include "headers/PlayerDamageKeeper.h"
#include "headers/PlayerIceCube.h"
#include "headers/PlayerJudgeWallHitDown.h"
#include "headers/PlayerStateJump.h"
#include "headers/PlayerStateWait.h"
#include "headers/PlayerStainControl.h"
#include "headers/TankBullet.h"
#include "ModOptions.h"
#include "math/seadVectorFwd.h"

// Namespaces
namespace rs {
    bool is2D(const IUseDimension*);
    bool isEnableSendTrampleMsg(const al::LiveActor* player, al::HitSensor* source, al::HitSensor* target);
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

// Helper function
const al::Nerve* getNerveAt(uintptr_t offset) {
    return (const al::Nerve*)((((u64)malloc) - 0x00724b94) + offset);
}

// Configuration
bool isPadTriggerGalaxySpin(int port) {
    switch (mallow::config::getConfg<ModOptions>()->spinButton) {
        case 'L': return al::isPadTriggerL(port);
        case 'X': return al::isPadTriggerX(port);
        case 'Y': default: return al::isPadTriggerY(port);
    }
}

// Global Buffers
al::LiveActor* hitBuffer[0x40];
int hitBufferCount = 0;

// Offsets
const uintptr_t spinCapNrvOffset = 0x1D78940;
const uintptr_t nrvSpinCapFall = 0x1D7ff70;
const uintptr_t nrvHakoniwaWait = 0x01D78918;
const uintptr_t nrvHakoniwaSquat = 0x01D78920;
const uintptr_t nrvHakoniwaFall = 0x01D78910;
const uintptr_t nrvHakoniwaHipDrop = 0x1D78978;
const uintptr_t nrvHakoniwaJump = 0x1D78948;

// Action Flags
int galaxySensorRemaining = -1;
bool prevIsCarry = false;
bool isSpinActive = false;
bool isSpinRethrow = false;
bool isPunchActive = false;
bool isPunchRight = false;
bool isNearCollectible = false;
bool isNearTreasure = false;
bool isNearSwoonedEnemy = false;

// Suit Flags
bool isMario = false;
bool isNoCap = false;
bool isFeather = false;
bool isFire = false;
bool isIce = false;
bool isTanooki = false;
bool isDrill = false;
bool isMetal = false;
bool isFly = false;
bool isBrawl = false;
bool isSuper = false;

bool isCapeOn = false;
bool isBlasterOn = false;

// Actor Pointers
inline PlayerActorHakoniwa* isHakoniwa = nullptr;
inline HammerBrosHammer* isHammer = nullptr;
inline CustomGauge* isGauge = nullptr;
inline Motorcycle* isKart = nullptr;
inline al::LiveActor* isKoopa = nullptr;
inline al::LiveActorGroup* fireBalls = nullptr;
inline al::LiveActorGroup* iceBalls = nullptr;
inline al::LiveActorGroup* iceCubes = nullptr;
inline al::LiveActorGroup* tankBullets = nullptr;

// Powerup Specifics
bool nextThrowLeft = true;
bool canFireball = false;
int fireStep = -1;
bool isFireThrowing() { return fireStep >= 0; }
bool tauntRightAlt = false;
bool isDoubleJump = false;
bool isDoubleJumpConsume = false;
float glideLean = 0.0f;
float glidePitch = 0.0f;
int isCapeActive = -1;
bool isSuperRunningOnSurface = false;
const f32 MIN_SPEED_RUN_ON_WATER = 15.0f;
const sead::Color4u8 paintClear(0, 0, 0, 0);

inline sead::Vector3f getHitSpawnPos(al::HitSensor* a, al::HitSensor* b) {
    sead::Vector3f pos = (al::getSensorPos(a) + al::getSensorPos(b)) * 0.5f;
    pos.y += 20.0f;
    return pos;
}

inline sead::Vector3f getFireDir(al::LiveActor* from, al::LiveActor* to) {
    sead::Vector3f dir = al::getTrans(to) - al::getTrans(from);
    dir.normalize();
    return dir;
}

inline bool isInHitBuffer(al::LiveActor* actor) {
    for (int i = 0; i < hitBufferCount; i++) {
        if (hitBuffer[i] == actor) return true;
    }
    return false;
}

// Edge guard logic
inline void applyEdgeGuard(al::LiveActor* player) {
    sead::Vector3f front;
    al::calcFrontDir(&front, player);
    sead::Vector3f grav = al::getGravity(player);
    sead::Vector3f hitPos;
    if (!alCollisionUtil::getHitPosOnArrow(player, &hitPos, al::getTrans(player) + front * 25.0f - grav * 50.0f, grav * 75.0f, nullptr, nullptr)) {
        sead::Vector3f* vel = al::getVelocityPtr(player);
        *vel = grav * vel->dot(grav);
    }
}

// Home in on nearest target
inline al::LiveActor* findNearestTarget(al::LiveActor* player, f32 maxDist) {
    al::HitSensor* eye = al::getHitSensor(player, "Eye");
    if (!eye) return nullptr;

    al::LiveActor* nearest = nullptr;
    f32 best = maxDist;

    for (int i = 0; i < eye->mSensorCount; i++) {
        al::LiveActor* actor = al::getSensorHost(eye->mSensors[i]);
        if (!actor || actor == player || !al::isAlive(actor)) continue;
        if (!al::isSensorNpc(eye->mSensors[i])
            && !(al::isSensorEnemyBody(eye->mSensors[i]) && al::isEqualSubString(eye->mSensors[i]->mName, "Body"))) continue;
        if (isInHitBuffer(actor)) continue; // Skip actors already hit this attack

        f32 d = al::calcDistance(player, actor);
        if (d < best) { best = d; nearest = actor; }
    }
    return nearest;
}

inline al::LiveActor* isNearTarget = nullptr;

// Spin logic
struct SpinState {
    bool isGalaxy = false;
    bool canGalaxy = true;
    bool canStandard = true;
    bool galaxyAfterStandard = false;
    bool standardAfterGalaxy = false;
    bool trigger = false;
    int fakethrowRemainder = -1;

    void reset() {
        isGalaxy = false;
        canGalaxy = true;
        canStandard = true;
        galaxyAfterStandard = false;
        standardAfterGalaxy = false;
        trigger = false;
        fakethrowRemainder = -1;
    }

    void resetForNewSpin() {
        canGalaxy = true;
        canStandard = true;
        galaxyAfterStandard = false;
        standardAfterGalaxy = false;
    }
};

inline SpinState spin;
enum class SpinPre { Fallthrough, Accept, Reject };

// Attack animations list
inline bool isBaseSpinAnim(PlayerAnimator* anim) {
    return al::isEqualString(anim->mCurAnim, "SpinSeparate")
        || al::isEqualString(anim->mCurAnim, "SpinSeparateSwim")
        || al::isEqualString(anim->mCurAnim, "CapeAttack")
        || al::isEqualString(anim->mCurAnim, "TailAttack")
        || al::isEqualString(anim->mCurAnim, "BlastAttack");
}

inline bool isDoubleSpinAnim(PlayerAnimator* anim) {
    return al::isEqualString(anim->mCurAnim, "SpinAttackLeft")
        || al::isEqualString(anim->mCurAnim, "SpinAttackRight")
        || al::isEqualString(anim->mCurAnim, "SpinAttackAirLeft")
        || al::isEqualString(anim->mCurAnim, "SpinAttackAirRight");
}

inline bool isSpinAnim(PlayerAnimator* anim) {
    if (!anim) return false;
    return isBaseSpinAnim(anim) || isDoubleSpinAnim(anim);
}

inline bool isPunchAnim(PlayerAnimator* anim) {
    if (!anim) return false;
    return al::isEqualString(anim->mCurAnim, "KoopaCapPunchL")
        || al::isEqualString(anim->mCurAnim, "KoopaCapPunchR")
        || al::isEqualString(anim->mCurAnim, "KoopaCapPunchFinishL")
        || al::isEqualString(anim->mCurAnim, "KoopaCapPunchFinishR")
        || al::isEqualString(anim->mCurAnim, "RabbitGet")
        || al::isEqualString(anim->mCurAnim, "Kick");
}

inline bool isHipDropAnim(PlayerAnimator* anim) {
    if (!anim) return false;
    return al::isEqualString(anim->mCurAnim, "HipDrop")
        || al::isEqualString(anim->mCurAnim, "HipDropPunch")
        || al::isEqualString(anim->mCurAnim, "HipDropReaction")
        || al::isEqualString(anim->mCurAnim, "HipDropPunchReaction")
        || al::isEqualString(anim->mCurAnim, "SpinJumpDownFallL")
        || al::isEqualString(anim->mCurAnim, "SpinJumpDownFallR")
        || al::isEqualString(anim->mCurAnim, "SwimHipDrop")
        || al::isEqualString(anim->mCurAnim, "SwimHipDropPunch")
        || al::isEqualString(anim->mCurAnim, "SwimDive");
}