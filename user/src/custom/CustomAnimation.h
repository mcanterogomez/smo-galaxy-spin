#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"

namespace CustomAnimation {

    inline const char* remapAnim(const char* name, PlayerAnimator* anim = nullptr) {
        if (!isHakoniwa || (anim && anim != isHakoniwa->mAnimator)
            || rs::isPlayer2D(isHakoniwa)) return nullptr;

        if (isBlasterOn) {
            if (al::isEqualString(name, "Wait")) return "BattleWait";
        }
        if (isTanooki) {
            if (al::isEqualString(name, "Glide")) return "GlideAlt";
            if (al::isEqualString(name, "JumpBroad8")) return "JumpBroad8Alt";
        }
        if (isFly) {
            if (al::isEqualString(name, "GlideFloat")) return "GlideFloatSuper";
            if (al::isEqualString(name, "Wait")) return "WaitSuper";
        }
        if (isMetal) {
            if (al::isEqualString(name, "Wait")) return "BattleWait";
            if (al::isEqualString(name, "JumpDashFast")) return "Jump";
            if (al::isEqualString(name, "WearEnd")) return "WearEndSuper";
            for (const char* move : {"Move", "MoveMoon"})
                if (al::isEqualString(name, move)) return "MoveBrawl";
        }
        if (isBrawl) {
            if (al::isEqualString(name, "BattleWait")) return "WaitBrawlFight";
            if (al::isEqualString(name, "JumpDashFast")) return "Jump";
            if (al::isEqualString(name, "Wait")) return "WaitBrawl";
            if (al::isEqualString(name, "WearEnd")) return "WearEndBrawl";
            for (const char* move : {"Move", "MoveMoon"})
                if (al::isEqualString(name, move)) return "MoveBrawl";
        }
        if (isSuper) {
            if (al::isEqualString(name, "BattleWait")) return "WaitSuperFight";
            if (al::isEqualString(name, "GlideFloat")) return "GlideFloatSuper";
            if (al::isEqualString(name, "Wait")) return "WaitSuper";
            if (al::isEqualString(name, "WearEnd")) return "WearEndSuper";
            for (const char* move : {"Move", "MoveMoon"})
                if (al::isEqualString(name, move)) return "MoveSuper";
        }
        if (!isFeather && !isTanooki && !isFly && !isBrawl && !isSuper) {
            if (al::isEqualString(name, "JumpDashFast")) return "JumpDashFastClassic";
            for (const char* move : {"Move", "MoveMoon"})
                if (al::isEqualString(name, move)) return "MoveClassic";
        }

        bool isSuit = (isMario && isCapeOn) || isFeather || isFly || isBrawl || isSuper;

        if (isSuit) {
            if (al::isEqualString(name, "HipDrop")) return "HipDropPunch";
            if (al::isEqualString(name, "HipDropLand")) return "HipDropPunchLand";
            if (al::isEqualString(name, "HipDropReaction")) return "HipDropPunchReaction";
            if (al::isEqualString(name, "HipDropStart")) return "HipDropPunchStart";
            if (al::isEqualString(name, "SwimDive")) return "SwimHipDropPunch";
            if (al::isEqualString(name, "SwimHipDrop")) return "SwimHipDropPunch";
            if (al::isEqualString(name, "SwimHipDropLand")) return "SwimHipDropPunchLand";
            if (al::isEqualString(name, "SwimHipDropStart")) return "SwimHipDropPunchStart";
        }
        if (isSuit || isMetal) {
            if (al::isEqualString(name, "LandStiffen")) return "LandSuper";
            if (al::isEqualString(name, "MofumofuDemoOpening2")) return "MofumofuDemoOpening2Super";
        }
        if (al::isEqualString(name, "BattleWait")) return "WaitBrawl";
        return nullptr;
    }

    // Swaps animation names before they reach the player and sub-actors
    struct PlayerAnimatorStartAnimHook : public mallow::hook::Trampoline<PlayerAnimatorStartAnimHook> {
        static void Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
            if ((isMetal || isFly || isBrawl || isSuper || isBlasterOn)
                && al::isEqualString(animName.cstr(), "WaitRelaxStart")) return;

            const char* swapped = remapAnim(animName.cstr(), thisPtr);
            Orig(thisPtr, swapped ? swapped : animName.cstr());
        }
    };

    // Makes engine checks like isAnim("Move") return true when "MoveBrawl" is playing
    struct PlayerAnimatorIsAnimHook : public mallow::hook::Trampoline<PlayerAnimatorIsAnimHook> {
        static bool Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
            const char* swapped = remapAnim(animName.cstr(), thisPtr);
            return Orig(thisPtr, animName) || (swapped && Orig(thisPtr, swapped));
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
        #ifdef ALLOW_POWERUPS
            PlayerAnimatorStartAnimHook::InstallAtSymbol("_ZN14PlayerAnimator9startAnimERKN4sead14SafeStringBaseIcEE");
            PlayerAnimatorIsAnimHook::InstallAtSymbol("_ZNK14PlayerAnimator6isAnimERKN4sead14SafeStringBaseIcEE");
        #endif

        #ifdef ALLOW_KART
            FindAnimInfoHook::InstallAtSymbol("_ZNK2al13AnimInfoTable12findAnimInfoEPKc");
        #endif
    }
}