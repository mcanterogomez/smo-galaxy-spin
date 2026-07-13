#pragma once
#include "custom/_Globals.h"

inline bool isKartSubmerged(Motorcycle* kart) {
    if (!al::isInWater(kart)) return false;
    sead::Vector3f surfacePos, surfaceNormal;
    return !al::calcFindWaterSurface(&surfacePos, &surfaceNormal, kart, al::getTrans(kart), sead::Vector3f::ey, 75.0f);
}

namespace PlayerKart {

    // Swap motorcycle anim to kart
    struct FindAnimInfoHook : public mallow::hook::Trampoline<FindAnimInfoHook> {
        static void* Callback(void* table, const char* name) {
            if (!al::isEqualSubString(name, "Motorcycle") || !isKart || !isHakoniwa) return Orig(table, name);

            bool isRidingKart = al::getSensorHost(isHakoniwa->mBindKeeper->mBindSensor) == (al::LiveActor*)isKart;
            if (!isRidingKart) return Orig(table, name);

            sead::FixedSafeString<64> kart;
            kart.format("Kart%s", name + strlen("Motorcycle"));
            if (void* result = Orig(table, kart.cstr())) return result;

            return Orig(table, name);
        }
    };

    struct InitActorSuffixHook : public mallow::hook::Trampoline<InitActorSuffixHook> {
        static void Callback(al::LiveActor* actor, const al::ActorInitInfo& info, const char* suffix) {
            if (actor == (al::LiveActor*)isKart) {
                al::initActorWithArchiveName(actor, info, "PlayerKart", suffix);
                return;
            }
            Orig(actor, info, suffix);
        }
    };

    inline void executeInitPlayer(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        // Create custom kart
        if (al::isExistArchive("ObjectData/PlayerKart")) {
            isKart = new Motorcycle("Kart");
            al::initCreateActorNoPlacementInfo(isKart, *actorInfo);
            isKart->makeActorDead();

            wheelFlipL = 0.0f; wheelFlipR = 0.0f;
            al::initJointLocalZRotator(isKart, &wheelFlipL, "FrontTireL"); al::initJointLocalZRotator(isKart, &wheelFlipR, "FrontTireR");
            al::initJointLocalZRotator(isKart, &wheelFlipL, "BackTireL"); al::initJointLocalZRotator(isKart, &wheelFlipR, "BackTireR");
            al::initJointLocalZRotator(isKart, &propellerSpin, "Propeller"); al::initJointLocalScaleController(isKart, &propellerScale, "Propeller");
        }
    }

    inline void executeInitAfterPlacement() { if (isKart) isKart->makeActorDead(); }

    inline void executeMovement(PlayerActorHakoniwa* thisPtr) {
        auto* damage = thisPtr->mDamageKeeper;
        bool isFlicker = damage && damage->mDamageInvalidCount > 0;
        bool isHack = thisPtr->mHackKeeper && thisPtr->mHackKeeper->mHackActor;
        bool isActive = !isFlicker && !isHack && !rs::isActiveDemo(thisPtr);

        // Handle kart spawning
        static int holdLeftFrames = 0;
        if (al::isPadHoldLeft(-1)) holdLeftFrames++;
        else holdLeftFrames = 0;

        if (!isKart || !isActive || holdLeftFrames != 30 || thisPtr->mInput->isMove()) return;

        if (al::isAlive(isKart)) {
            if (rs::isPlayerBinding(thisPtr)) return;

            al::tryEmitEffect(isKart, "Disappear", nullptr);
            al::tryStartSe(isKart, "CommonVanishS");
            isKart->kill();
            return;
        }

        sead::Vector3f front;
        al::calcFrontDir(&front, thisPtr);
        sead::Vector3f gravity = al::getGravity(thisPtr);
        sead::Vector3f marioPos = al::getTrans(thisPtr);
        sead::Vector3f target = marioPos + front * 500.0f;

        sead::Vector3f groundPos;
        bool hasGround = alCollisionUtil::getHitPosOnArrow(thisPtr, &groundPos, target - gravity * 1000.0f, gravity * 2000.0f, nullptr, nullptr);

        if (!hasGround) { al::tryStartSe(thisPtr, "InvalidCapAction"); return; }
        target = groundPos - gravity;

        al::setTrans(isKart, target);
        isKart->appear();
        al::tryEmitEffect(isKart, "Appear", nullptr);
        al::tryStartSe(isKart, "Appear");
    }

    struct MotorcycleMovementHook : public mallow::hook::Trampoline<MotorcycleMovementHook> {
        static void Callback(al::LiveActor* actor) {
            if (actor != (al::LiveActor*)isKart || !al::isAlive(actor)) { Orig(actor); return; }

            auto* kart = static_cast<Motorcycle*>(actor);
            auto* collider = static_cast<IUsePlayerCollision*>(kart);
            const char* actionName = al::getActionName(kart);
            bool wasAntiGravity = isAntiGravity;

            // Hover state: hazard ground forces it on, safe ground releases it
            bool isHazard = isKartSubmerged(kart) || rs::isCollisionCodeDamageFireGround(collider) || rs::isCollisionCodePoisonTouch(collider);
            bool isSafeGround = rs::isOnGround(kart, kart) && !isHazard;
            if (isHazard) isAntiGravity = true;
            else if (isSafeGround) isAntiGravity = false;

            if (isAntiGravity && !wasAntiGravity) al::tryStartSe(kart, "HoverStart");
            else if (!isAntiGravity && wasAntiGravity) al::tryStartSe(kart, "HoverFinish");

            // Wheels: tilt up while hovering, back down otherwise
            float targetL = isAntiGravity ? -90.0f : 0.0f;
            float targetR = isAntiGravity ? 90.0f : 0.0f;
            wheelFlipL = al::lerpValue(wheelFlipL, targetL, 0.08f);
            wheelFlipR = al::lerpValue(wheelFlipR, targetR, 0.08f);

            // Propeller: grows/shrinks with hover state
            const float propellerScaleRate = 0.20f;
            float s = sead::Mathf::clamp(propellerScale.x + (isAntiGravity ? propellerScaleRate : -propellerScaleRate), 0.0f, 1.0f);
            propellerScale = {s, s, s};

            // Propeller: spins during run/land, eases in/out, effect follows the spin
            bool isActive = al::isEqualSubString(actionName, "Run") || al::isEqualSubString(actionName, "Land");
            bool wasSpinning = wasAntiGravity && propellerSpeed > 5.0f;
            float targetSpeed = isActive ? 20.0f : 0.0f;
            propellerSpeed = al::lerpValue(propellerSpeed, targetSpeed, 0.1f);
            propellerSpin += propellerSpeed;

            bool isSpinning = isAntiGravity && propellerSpeed > 5.0f;
            if (isSpinning && !wasSpinning) al::tryEmitEffect(kart, "PropellerSpin", nullptr);
            else if (!isSpinning && wasSpinning) al::tryDeleteEffect(kart, "PropellerSpin");

            // While hovering, land/run should look like swimming instead
            if (isAntiGravity && !al::isEqualSubString(actionName, "Swim")) {
                if (al::isEqualSubString(actionName, "Land")) al::tryStartAction(kart, "SwimLand");
                else if (al::isEqualSubString(actionName, "Run")) al::tryStartAction(kart, "SwimRun");
            }

            Orig(actor);
        }
    };

    struct CalcAnimHook : public mallow::hook::Trampoline<CalcAnimHook> {
        static void Callback(al::LiveActor* actor) {
            if (actor != (al::LiveActor*)isKart || !al::isAlive(actor)) { Orig(actor); return; }

            auto* kart = static_cast<Motorcycle*>(actor);

            // Dampen lean while tilted
            float savedLean = kart->mLean;
            kart->mLean *= wheelFlipR / 90.0f;

            Orig(actor);

            kart->mLean = savedLean;
        }
    };

    template <uintptr_t Offset>
    struct ForceOutOfWaterInline : public mallow::hook::Inline<ForceOutOfWaterInline<Offset>> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (reinterpret_cast<void*>(ctx->X[19]) != isKart) return;
            ctx->X[0] = 0; // force al::isInWater's return to false, kart only
        }
    };

    inline void Install() {
        InitActorSuffixHook::InstallAtSymbol("_ZN2al15initActorSuffixEPNS_9LiveActorERKNS_13ActorInitInfoEPKc");
        MotorcycleMovementHook::InstallAtSymbol("_ZN10Motorcycle8movementEv");
        CalcAnimHook::InstallAtSymbol("_ZN2al9LiveActor8calcAnimEv");
        FindAnimInfoHook::InstallAtSymbol("_ZNK2al13AnimInfoTable12findAnimInfoEPKc");
        ForceOutOfWaterInline<0x2C92D0>::InstallAtOffset(0x2C92D0); // Motorcycle::movement, while riding
        ForceOutOfWaterInline<0x2CA110>::InstallAtOffset(0x2CA110); // sub_71002CA0E0, on dismount/fall
        // Motorcycle's joint keeper defaults to capacity 6; bump to 12 so our 4 tire rotators fit
        exl::patch::CodePatcher motorcycleJointCapPatcher(0x2C72A4);
        motorcycleJointCapPatcher.WriteInst(0x52800181); // MOV W1, #12
    }
}