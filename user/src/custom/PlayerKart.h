#pragma once
#include "custom/_Globals.h"

inline bool isKartSubmerged(Motorcycle* kart) {
    if (!al::isInWater(kart)) return false;
    sead::Vector3f surfacePos, surfaceNormal;
    return !al::calcFindWaterSurface(&surfacePos, &surfaceNormal, kart, al::getTrans(kart), sead::Vector3f::ey, 75.0f);
}

namespace PlayerKart {

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
            static bool isHovering = false;

            // Are we currently over something that forces hover mode?
            bool isHazard = isKartSubmerged(kart) || rs::isCollisionCodeDamageFireGround(collider) || rs::isCollisionCodePoisonTouch(collider);
            bool isSafeGround = rs::isOnGround(kart, kart) && !isHazard;

            if (isHazard) isAntiGravity = true;
            else if (isSafeGround) isAntiGravity = false;

            // Play hover SE once on each state change, not every frame
            if (isAntiGravity && !isHovering) { al::tryStartSe(kart, "HoverStart"); isHovering = true; }
            else if (!isAntiGravity && isHovering) { al::tryStartSe(kart, "HoverFinish"); isHovering = false; }

            // Tilt wheels up while hovering, back down otherwise
            float targetL = isAntiGravity ? -90.0f : 0.0f;
            float targetR = isAntiGravity ? 90.0f : 0.0f;
            wheelFlipL = al::lerpValue(wheelFlipL, targetL, 0.08f);
            wheelFlipR = al::lerpValue(wheelFlipR, targetR, 0.08f);

            // While hovering, land/run should look like swimming instead
            const char* actionName = al::getActionName(kart);
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

    // Prevent crash when Motorcycle enters water (null OceanWave in fluid system)
    struct CalcFindWaterSurfaceFlatFix : public mallow::hook::Trampoline<CalcFindWaterSurfaceFlatFix> {
        static bool Callback(sead::Vector3f* outPos, sead::Vector3f* outNormal, const al::LiveActor* actor,
            const sead::Vector3f& pos, const sead::Vector3f& up, float range) {
            if (actor == isKart) return true;
            return Orig(outPos, outNormal, actor, pos, up, range);
        }
    };

    struct CalcFindWaterSurfaceFix : public mallow::hook::Trampoline<CalcFindWaterSurfaceFix> {
        static bool Callback(sead::Vector3f* outPos, sead::Vector3f* outNormal, const al::LiveActor* actor,
            const sead::Vector3f& pos, const sead::Vector3f& up, float range) {
            if (actor == isKart) return true;
            return Orig(outPos, outNormal, actor, pos, up, range);
        }
    };

    // Kart animation swap at the bfres level (motorcycle bypasses PlayerAnimator)
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

    inline void Install() {
        InitActorSuffixHook::InstallAtSymbol("_ZN2al15initActorSuffixEPNS_9LiveActorERKNS_13ActorInitInfoEPKc");
        MotorcycleMovementHook::InstallAtSymbol("_ZN10Motorcycle8movementEv");
        CalcAnimHook::InstallAtSymbol("_ZN2al9LiveActor8calcAnimEv");
        CalcFindWaterSurfaceFlatFix::InstallAtSymbol("_ZN2al24calcFindWaterSurfaceFlatEPN4sead7Vector3IfEES3_PKNS_9LiveActorERKS2_S8_f");
        //CalcFindWaterSurfaceFix::InstallAtSymbol("_ZN2al20calcFindWaterSurfaceEPN4sead7Vector3IfEES3_PKNS_9LiveActorERKS2_S8_f");
        FindAnimInfoHook::InstallAtSymbol("_ZNK2al13AnimInfoTable12findAnimInfoEPKc");
        // Motorcycle's joint keeper defaults to capacity 6; bump to 12 so our 4 tire rotators fit
        exl::patch::CodePatcher motorcycleJointCapPatcher(0x2C72A4);
        motorcycleJointCapPatcher.WriteInst(0x52800181); // MOV W1, #12
    }
}