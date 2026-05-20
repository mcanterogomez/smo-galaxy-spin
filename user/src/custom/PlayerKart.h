#pragma once
#include "custom/_Globals.h"

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
        if (al::isExistArchive("ObjectData/PlayerKart")
        ) {
            isKart = new Motorcycle("Kart");
            al::initCreateActorNoPlacementInfo(isKart, *actorInfo);
            isKart->makeActorDead();
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

        if (isKart && isActive
            && holdLeftFrames == 30
            && !thisPtr->mInput->isMove()
        ) {
            if (al::isAlive(isKart)
            ) {
                if (rs::isPlayerBinding(thisPtr)) return;

                al::tryEmitEffect(isKart, "Disappear", nullptr);
                al::tryStartSe(isKart, "CommonVanishS");
                isKart->kill();
                return;
            } else {
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
        }
    }

    struct CalcAnimHook : public mallow::hook::Trampoline<CalcAnimHook> {
        static void Callback(al::LiveActor* actor) {
            bool isKartAnim = typeid(*actor) == typeid(Motorcycle) && al::isAlive(actor);
            float savedLean = 0.0f;

            if (isKartAnim) {
                float* lean = reinterpret_cast<float*>((char*)actor + 312);
                savedLean = *lean;
                *lean = 0.0f;
            }

            Orig(actor);

            if (isKartAnim) *reinterpret_cast<float*>((char*)actor + 312) = savedLean;
        }
    };

    // Prevent crash when Motorcycle enters water (null OceanWave in fluid system)
    struct CalcFindWaterSurfaceFlatFix : public mallow::hook::Trampoline<CalcFindWaterSurfaceFlatFix> {
        static bool Callback(sead::Vector3f* outPos, sead::Vector3f* outNormal, const al::LiveActor* actor,
            const sead::Vector3f& pos, const sead::Vector3f& up, float range) {
            if (actor == isKart) return false;
            return Orig(outPos, outNormal, actor, pos, up, range);
        }
    };

    // Kart animation swap at the bfres level (motorcycle bypasses PlayerAnimator)
    struct FindAnimInfoHook : public mallow::hook::Trampoline<FindAnimInfoHook> {
        static void* Callback(void* table, const char* name) {
            if (al::isEqualSubString(name, "Motorcycle") && isKart
                && isHakoniwa && al::getSensorHost(isHakoniwa->mBindKeeper->mBindSensor) == (al::LiveActor*)isKart
            ) {
                sead::FixedSafeString<64> kart;
                kart.format("Kart%s", name + strlen("Motorcycle"));
                void* result = Orig(table, kart.cstr());
                if (result) return result;
            }
            return Orig(table, name);
        }
    };

    inline void Install() {
        InitActorSuffixHook::InstallAtSymbol("_ZN2al15initActorSuffixEPNS_9LiveActorERKNS_13ActorInitInfoEPKc");
        CalcAnimHook::InstallAtSymbol("_ZN2al9LiveActor8calcAnimEv");
        CalcFindWaterSurfaceFlatFix::InstallAtSymbol("_ZN2al24calcFindWaterSurfaceFlatEPN4sead7Vector3IfEES3_PKNS_9LiveActorERKS2_S8_f");
        FindAnimInfoHook::InstallAtSymbol("_ZNK2al13AnimInfoTable12findAnimInfoEPKc");
    }
}
