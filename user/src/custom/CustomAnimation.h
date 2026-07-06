#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"

namespace CustomAnimation {

    inline const char* remapAnim(const char* name, PlayerAnimator* anim = nullptr) {
        if (!isHakoniwa || (anim && anim != isHakoniwa->mAnimator)
            || rs::isPlayer2D(isHakoniwa)) return nullptr;

        if (isWeaponOn) {
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
        }
        if (isBrawl) {
            if (al::isEqualString(name, "BattleWait")) return "WaitBrawlFight";
            if (al::isEqualString(name, "JumpDashFast")) return "Jump";
            if (al::isEqualString(name, "Wait")) return "WaitBrawl";
            if (al::isEqualString(name, "WearEnd")) return "WearEndBrawl";
        }
        if (isSuper) {
            if (al::isEqualString(name, "BattleWait")) return "WaitSuperFight";
            if (al::isEqualString(name, "GlideFloat")) return "GlideFloatSuper";
            if (al::isEqualString(name, "Wait")) return "WaitSuper";
            if (al::isEqualString(name, "WearEnd")) return "WearEndSuper";
        }
        if (!isFeather && !isTanooki && !isFly && !isBrawl && !isSuper) {
            if (al::isEqualString(name, "JumpDashFast")) return "JumpDashFastClassic";
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

        #ifdef ALLOW_DASH
            if (isMetal || isBrawl)
                for (const char* move : {"Move", "MoveMoon"})
                    if (al::isEqualString(name, move)) return "MoveBrawl";
            if (isSuper)
                for (const char* move : {"Move", "MoveMoon"})
                    if (al::isEqualString(name, move)) return "MoveSuper";
            if (!isFeather && !isTanooki && !isFly && !isBrawl && !isSuper)
                for (const char* move : {"Move", "MoveMoon"})
                    if (al::isEqualString(name, move)) return "MoveClassic";
        #endif

        return nullptr;
    }

    // Swaps animation names before they reach the player and sub-actors
    struct PlayerAnimatorStartAnimHook : public mallow::hook::Trampoline<PlayerAnimatorStartAnimHook> {
        static void Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
            if (al::isEqualString(animName.cstr(), "WaitRelaxStart")
                && remapAnim("Wait", thisPtr)) return;

            const char* swapped = remapAnim(animName.cstr(), thisPtr);
            Orig(thisPtr, swapped ? swapped : animName.cstr());
        }
    };

    // Makes engine checks like isAnim("Move") return true when "MoveBrawl" is playing
    struct PlayerAnimatorIsAnimHook : public mallow::hook::Trampoline<PlayerAnimatorIsAnimHook> {
        static bool Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
            const char* swapped = remapAnim(animName.cstr(), thisPtr);

            if (swapped) {
                if (al::isEqualString(animName.cstr(), "Wait")) return false;
                return Orig(thisPtr, swapped);
            }
            return Orig(thisPtr, animName);
        }
    };

    //Swap for new 2d animation archive
    struct PlayerAnimation2DArchiveHook : public mallow::hook::Inline<PlayerAnimation2DArchiveHook> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            const char* model = (const char*)ctx->X[20];
            static bool isNew2D = al::isExistFile("ObjectData/MarioNew2D.txt");

            if ((al::isEqualString(model, "Mario2D") && isNew2D)
                #ifdef ALLOW_POWERUPS
                    || al::isEqualString(model, "MarioFeather2D")
                    || al::isEqualString(model, "MarioColorFire2D")
                    || al::isEqualString(model, "MarioColorIce2D")
                    || al::isEqualString(model, "MarioTanooki2D")
                    || al::isEqualString(model, "MarioDrill2D")
                    || al::isEqualString(model, "MarioColorMetal2D")
                    || al::isEqualString(model, "MarioColorFly2D")
                    || al::isEqualString(model, "MarioColorBrawl2D")
                    || al::isEqualString(model, "MarioColorSuper2D")
                #endif
            ) ctx->X[25] = (u64)"PlayerAnimationNew2D";
        }
    };

    inline void Install() {
        #ifdef ALLOW_POWERUPS
            PlayerAnimatorStartAnimHook::InstallAtSymbol("_ZN14PlayerAnimator9startAnimERKN4sead14SafeStringBaseIcEE");
            PlayerAnimatorIsAnimHook::InstallAtSymbol("_ZNK14PlayerAnimator6isAnimERKN4sead14SafeStringBaseIcEE");
        #endif
        PlayerAnimation2DArchiveHook::InstallAtOffset(0x445664);
    }
}