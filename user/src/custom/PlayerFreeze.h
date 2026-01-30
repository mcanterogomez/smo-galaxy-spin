#pragma once

#include "ModConfig.h"
#include "custom/_Globals.h"
#include "headers/PlayerIceCube.h"

namespace PlayerFreeze {

    struct FrozenState {
        al::LiveActor* actor;
        int timer;
        const char* prevAction;
        PlayerIceCube* cube;
    };

    inline FrozenState frozenList[32];
    inline int frozenCount = 0;

    // Find frozen state index, returns -1 if not found
    inline int findFrozenIndex(al::LiveActor* actor) {
        for (int i = 0; i < frozenCount; i++)
            if (frozenList[i].actor == actor) return i;
        return -1;
    }

    inline bool isFrozen(al::LiveActor* actor) {
        return findFrozenIndex(actor) >= 0;
    }

    inline void freezeActor(al::LiveActor* actor, int duration) {
        if (!actor || findFrozenIndex(actor) >= 0 || frozenCount >= 32) return;

        const char* curAction = al::getActionName(actor);
        bool usedBlowDown = al::tryStartAction(actor, "BlowDown");

        PlayerIceCube* cube = iceCubes ? (PlayerIceCube*)iceCubes->getDeadActor() : nullptr;
        if (cube) cube->freeze(actor);

        frozenList[frozenCount++] = {
            actor, duration, usedBlowDown ? curAction : nullptr, cube
        };

        al::setActionFrameRate(actor, 0.0f);
        al::invalidateHitSensors(actor);
    }

    inline void unfreezeActor(al::LiveActor* actor, bool restoreAction = false) {
        int i = findFrozenIndex(actor);
        if (i < 0) return;

        FrozenState& state = frozenList[i];

        if (state.cube) state.cube->unfreeze();
        al::setActionFrameRate(actor, 1.0f);
        al::validateHitSensors(actor);
        if (restoreAction && state.prevAction) al::tryStartAction(actor, state.prevAction);

        frozenList[i] = frozenList[--frozenCount];
    }

    inline bool sendAttackToEnemy(al::LiveActor* enemy, al::HitSensor* attacker) {
        if (!attacker || !enemy) return false;

        al::HitSensor* target = al::getHitSensor(enemy, "Body");
        if (!target && enemy->getHitSensorKeeper())
            target = enemy->getHitSensorKeeper()->getSensor(0);
        if (!target) return false;

        al::LiveActor* attackerHost = al::getSensorHost(attacker);
        bool isFireball = al::isEqualSubString(typeid(*attackerHost).name(), "FireBrosFireBall");

        if (isFireball) {
            if (al::sendMsgPlayerFireBallAttack(target, attacker) ||
                rs::sendMsgFireBrosFireBallCollide(target, attacker)) return true;

            if (rs::sendMsgHackAttack(target, attacker) ||
                al::sendMsgExplosion(target, attacker, nullptr)) {
                sead::Vector3f spawnPos = (al::getSensorPos(attacker) + al::getTrans(enemy)) * 0.5f;
                spawnPos.y += 20.0f;
                if (!al::isEffectEmitting(attackerHost, "Hit"))
                    al::tryEmitEffect(isHakoniwa, "Hit", &spawnPos);
                return true;
            }
        } else {
            if (rs::sendMsgHackAttack(target, attacker) ||
                rs::sendMsgCapReflect(target, attacker) ||
                rs::sendMsgCapAttack(target, attacker) ||
                al::sendMsgPlayerObjHipDropReflect(target, attacker, nullptr)) return true;
        }

        return false;
    }

    inline bool updateFrozenActor(al::LiveActor* actor) {
        int i = findFrozenIndex(actor);
        if (i < 0) return false;

        FrozenState& state = frozenList[i];

        // Cube hit -> kill enemy
        if (state.cube && state.cube->wasHit()) {
            al::HitSensor* attacker = state.cube->getAttackerSensor();
            unfreezeActor(actor);
            sendAttackToEnemy(actor, attacker);
            return false;
        }

        al::setActionFrameRate(actor, 0.0f);

        if (--state.timer <= 0) {
            unfreezeActor(actor, true);
            return false;
        }

        return true;
    }

    inline void clearAllFrozen() {
        for (int i = 0; i < frozenCount; i++) {
            FrozenState& state = frozenList[i];
            if (!state.actor || !al::isAlive(state.actor)) continue;

            if (state.cube) state.cube->unfreeze();
            al::setActionFrameRate(state.actor, 1.0f);
            al::validateHitSensors(state.actor);
            if (state.prevAction) al::tryStartAction(state.actor, state.prevAction);
        }
        frozenCount = 0;
    }

    inline bool handleReceiveMsg(const al::SensorMsg* msg, al::HitSensor* source) {
        #ifdef ALLOW_POWERUPS
        if (!msg || !source) return false;
        al::LiveActor* attacker = al::getSensorHost(source);
        return attacker && isFrozen(attacker) && al::isMsgEnemyAttack(msg);
        #else
        return false;
        #endif
    }

}