#pragma once

#include "custom/_Globals.h"

namespace KoopaBattle {

    inline bool isKnockBack = false;

    // Reads Bowser's internal kill-ready flag
    // KoopaLv1+264 → KoopaCap+272 → KoopaCapPlayer+292
    inline bool isKillReady(al::LiveActor* koopa) {
        if (!koopa || !al::isAlive(koopa)) return false;
        auto* cap = *reinterpret_cast<void**>((char*)koopa + 264);
        if (!cap) return false;
        auto* player = *reinterpret_cast<void**>((char*)cap + 272);
        if (!player) return false;
        return *reinterpret_cast<int*>((char*)player + 292) == 1;
    }

    // Detects when Bowser accepts knockback (tail attack starts)
    struct AttackTailHook : public mallow::hook::Trampoline<AttackTailHook> {
        static bool Callback(void* counter, void* cap) {
            bool isStart = Orig(counter, cap);
            if (isStart && isKoopa && isHakoniwa) isKnockBack = true;
            return isStart;
        }
    };

    inline void attack(PlayerActorHakoniwa* mario, al::HitSensor* source, al::HitSensor* target) {
        al::LiveActor* bowser = al::getSensorHost(target);
        isKoopa = bowser;
        isKnockBack = false;

        bool isFinish = false;
        bool isHandled = false;

        if (isKillReady(bowser)) {
            isFinish = rs::sendMsgKoopaCapPunchFinishL(target, source);
            isHandled = isFinish;
        } else {
            rs::sendMsgKoopaCapPunchKnockBackL(target, source);
            isHandled = isKnockBack;
        }

        bool isInvincible = false;
        if (!isHandled) {
            isInvincible = rs::sendMsgKoopaCapPunchInvincibleL(target, source);
            isHandled = isInvincible || rs::sendMsgKoopaCapPunchL(target, source);
        }

        if (!isHandled) return;

        sead::Vector3f hitPos = getHitSpawnPos(source, target);

        // Pushback Mario when Bowser accepts knockback
        if (isKnockBack) {
            al::tryStartSe(mario, "DamageHit");
            auto* spinCap = *reinterpret_cast<PlayerStateSpinCap**>(reinterpret_cast<uintptr_t>(mario) + 0x300);
            al::setNerve(spinCap, getNerveAt(nrvSpinCapFall));

            sead::Vector3f away = al::getTrans(mario) - al::getTrans(bowser);
            al::tryNormalizeOrZero(&away);
            al::setVelocity(mario, away * (isHakoniwa->mInput->isMove() ? 25.0f : 15.0f) - al::getGravity(mario) * 20.0f);
        }

        if (isInvincible) al::tryEmitEffect(bowser, "Guard", &hitPos);
        else if (!al::isEffectEmitting(bowser, "Guard")) al::tryEmitEffect(mario, "KoopaHit", &hitPos);

        hitBuffer[hitBufferCount++] = bowser;
    }

    inline void Install() {
        AttackTailHook::InstallAtOffset(0x97250);
    }
}