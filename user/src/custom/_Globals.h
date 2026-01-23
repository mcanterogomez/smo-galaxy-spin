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

// Engine / “Library” headers
#include "Library/Base/StringUtil.h"
#include "Library/Controller/InputFunction.h"
#include "Library/Controller/SpinInputAnalyzer.h"
#include "Library/Effect/EffectKeeper.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/HitSensor/HitSensorKeeper.h"
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
#include "Player/HackCap.h"

// Mod‑specific & custom actors
#include "headers/CustomGauge.h"
#include "headers/CustomPlayerConst.h"
#include "headers/FireBall.h"
#include "headers/HammerBrosHammer.h"
#include "headers/PlayerAnimator.h"
#include "headers/PlayerDamageKeeper.h"
#include "headers/PlayerJudgeWallHitDown.h"
#include "headers/PlayerStateJump.h"
#include "headers/PlayerStateWait.h"
#include "headers/PlayerStainControl.h"
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
const uintptr_t spinCapNrvOffset = 0x1d78940;
const uintptr_t nrvSpinCapFall = 0x1d7ff70;
const uintptr_t nrvHakoniwaWait = 0x01D78918;
const uintptr_t nrvHakoniwaSquat = 0x01D78920;
const uintptr_t nrvHakoniwaFall = 0x01d78910;
const uintptr_t nrvHakoniwaHipDrop = 0x1D78978;
const uintptr_t nrvHakoniwaJump = 0x1D78948;

// Spin Flags
bool isGalaxySpin = false;
bool canGalaxySpin = true;
bool canStandardSpin = true;
bool isGalaxyAfterStandardSpin = false;
bool isStandardAfterGalaxySpin = false;
int galaxyFakethrowRemainder = -1;
bool triggerGalaxySpin = false;
bool prevIsCarry = false;
bool isSpinRethrow = false;
int galaxySensorRemaining = -1;
bool isSpinActive = false;

// Action Flags
bool isPunching = false;
bool isPunchRight = false;
bool isFinalPunch = false;
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
bool isBrawl = false;
bool isSuper = false;

// Actor Pointers
inline PlayerActorHakoniwa* isHakoniwa = nullptr;
inline HammerBrosHammer* isHammer = nullptr;
inline al::LiveActor* isKoopa = nullptr;
inline al::LiveActorGroup* fireBalls = nullptr;
inline al::LiveActorGroup* iceBalls = nullptr;
inline CustomGauge* isGauge = nullptr;

// Powerup Specifics
bool nextThrowLeft = true;
bool canFireball = false;
int fireStep = -1;
static inline bool isFireThrowing() { return fireStep >= 0; }
bool tauntRightAlt = false;
bool isDoubleJump = false;
bool isDoubleJumpConsume = false;
int isCapeActive = -1;
bool isSuperRunningOnSurface = false;
const f32 MIN_SPEED_RUN_ON_WATER = 15.0f;