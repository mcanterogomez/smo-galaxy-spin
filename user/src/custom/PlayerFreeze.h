#pragma once

#include "ModConfig.h"
#include "custom/_Globals.h"
#include "headers/PlayerIceCube.h"

namespace PlayerFreeze {

    struct FrozenEntry {
        al::LiveActor* actor = nullptr;
        PlayerIceCube* cube = nullptr;
        const char* prevAction = nullptr;
        s32 timer = 0;
    };

    inline constexpr s32 kMaxFrozen = 32;
    inline FrozenEntry sFrozenList[kMaxFrozen];
    inline s32 sFrozenCount = 0;

    inline void clearAllFrozen() {
        sFrozenCount = 0;
    }

    inline FrozenEntry* findEntry(al::LiveActor* actor) {
        for (s32 i = 0; i < sFrozenCount; i++) {
            if (sFrozenList[i].actor == actor)
                return &sFrozenList[i];
        }
        return nullptr;
    }

    inline bool isFrozen(al::LiveActor* actor) {
        return actor && findEntry(actor) != nullptr;
    }

    inline void freezeActor(al::LiveActor* actor, s32 duration) {
        if (!actor || isFrozen(actor) || sFrozenCount >= kMaxFrozen)
            return;

        const char* prevAction = al::getActionName(actor);
        bool usedBlowDown = al::tryStartAction(actor, "BlowDown");

        PlayerIceCube* cube = nullptr;
        if (iceCubes) {
            cube = static_cast<PlayerIceCube*>(iceCubes->getDeadActor());
            if (cube)
                cube->freeze(actor);
        }

        sFrozenList[sFrozenCount++] = {
            actor,
            cube,
            usedBlowDown ? prevAction : nullptr,
            duration
        };

        al::setActionFrameRate(actor, 0.0f);
        al::invalidateHitSensors(actor);
    }

    inline void unfreezeActor(al::LiveActor* actor, bool restoreAction = false) {
        FrozenEntry* entry = findEntry(actor);
        if (!entry) return;

        if (entry->cube && al::isAlive(entry->cube))
            entry->cube->unfreeze();

        if (actor && al::isAlive(actor)) {
            al::setActionFrameRate(actor, 1.0f);
            al::validateHitSensors(actor);

            if (restoreAction && entry->prevAction)
                al::tryStartAction(actor, entry->prevAction);
        }

        *entry = sFrozenList[--sFrozenCount];
    }

    inline bool sendAttackToEnemy(al::LiveActor* enemy, al::HitSensor* attacker) {
        if (!attacker || !enemy || !al::isAlive(enemy)) return false;

        al::HitSensor* target = al::getHitSensor(enemy, "Body");
        if (!target && enemy->getHitSensorKeeper())
            target = enemy->getHitSensorKeeper()->getSensor(0);
        if (!target) return false;

        sead::Vector3f effectPos = (al::getSensorPos(attacker) + al::getTrans(enemy)) * 0.5f;
        effectPos.y += 20.0f;

        al::LiveActor* attackerActor = al::getSensorHost(attacker);

        // Projectile attacks
        if (attackerActor && al::isEqualSubString(typeid(*attackerActor).name(), "FireBrosFireBall")) {
            if (al::sendMsgPlayerFireBallAttack(target, attacker) ||
                rs::sendMsgFireBrosFireBallCollide(target, attacker))
                return true;

            if (rs::sendMsgHackAttack(target, attacker) ||
                al::sendMsgExplosion(target, attacker, nullptr)) {
                if (!al::isEffectEmitting(attackerActor, "Hit"))
                    al::tryEmitEffect(isHakoniwa, "Hit", &effectPos);
                return true;
            }
        }
        // Standard attacks
        else {
            return rs::sendMsgHackAttack(target, attacker) ||
                   rs::sendMsgCapReflect(target, attacker) ||
                   rs::sendMsgCapAttack(target, attacker) ||
                   al::sendMsgPlayerObjHipDropReflect(target, attacker, nullptr);
        }

        return false;
    }

    inline bool updateFrozenActor(al::LiveActor* actor) {
        FrozenEntry* entry = findEntry(actor);
        if (!entry) return false;

        if (!actor || !al::isAlive(actor)) {
            if (entry->cube && al::isAlive(entry->cube))
                entry->cube->makeActorDead();
            *entry = sFrozenList[--sFrozenCount];
            return false;
        }

        // Guard: cube gone (died or scene change) -> unfreeze actor
        if (entry->cube && !al::isAlive(entry->cube)) {
            entry->cube = nullptr;
            unfreezeActor(actor, true);
            return false;
        }

        // Cube hit -> damage enemy
        if (entry->cube && entry->cube->wasHit()) {
            al::HitSensor* attacker = entry->cube->getAttacker();
            unfreezeActor(actor);
            sendAttackToEnemy(actor, attacker);
            return false;
        }

        al::setActionFrameRate(actor, 0.0f);

        if (--entry->timer <= 0) {
            unfreezeActor(actor, true);
            return false;
        }

        return true;
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

}  // namespace PlayerFreeze