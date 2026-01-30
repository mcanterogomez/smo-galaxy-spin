#pragma once

#include "ModConfig.h"
#include "custom/_Globals.h"
#include "headers/PlayerIceCube.h"

namespace PlayerFreeze {

    // Frozen actor state tracking
    struct FrozenState {
        al::LiveActor* actor;
        int timer;
        const char* prevAction;
        PlayerIceCube* cube;
    };

    inline FrozenState frozenList[32];
    inline int frozenCount = 0;

    // Freeze any actor for specified duration, spawn ice cube visual
    inline void freezeActor(al::LiveActor* actor, int duration) {
        if (!actor) return;
        
        // Skip if already frozen
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor == actor) return;
        }
        
        if (frozenCount >= 32) return;

        // Store current action if transitioning to BlowDown
        const char* curAction = al::getActionName(actor);
        bool usedBlowDown = al::tryStartAction(actor, "BlowDown");
        
        // Allocate cube from pool
        PlayerIceCube* cube = nullptr;
        if (iceCubes) {
            cube = (PlayerIceCube*)iceCubes->getDeadActor();
            if (cube) cube->freeze(actor);
        }

        // Register frozen state
        frozenList[frozenCount++] = { actor, duration, usedBlowDown ? curAction : nullptr, cube };

        // Lock animation
        al::setActionFrameRate(actor, 0.0f);
        
        // Invalidate all sensors so actor can't be hit or attack
        al::invalidateHitSensors(actor);
    }

    inline bool isFrozen(al::LiveActor* actor) {
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor == actor) return true;
        }
        return false;
    }

    // Remove freeze, restore action frame rate
    // isTimer = true restores previous action (timeout unfreeze)
    inline void unfreezeActor(al::LiveActor* actor, bool isTimer = false) {
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor != actor) continue;

            // Destroy visual cube
            if (frozenList[i].cube) frozenList[i].cube->unfreeze();

            // Restore animation speed
            al::setActionFrameRate(actor, 1.0f);
            
            // Re-enable sensors
            al::validateHitSensors(actor);

            // Restore previous action on timeout
            if (isTimer && frozenList[i].prevAction) al::tryStartAction(actor, frozenList[i].prevAction);

            // Remove from registry (swap with last, decrement)
            frozenList[i] = frozenList[--frozenCount];
            return;
        }
    }

    // Try sending attack message to enemy's primary sensor
    inline bool sendAttackToEnemy(al::LiveActor* enemy, al::HitSensor* attacker) {
        if (!attacker || !enemy) return false;

        // Find target sensor (prefer Body)
        al::HitSensor* target = al::getHitSensor(enemy, "Body");
        if (!target && enemy->getHitSensorKeeper()) target = enemy->getHitSensorKeeper()->getSensor(0);
        if (!target) return false;

        sead::Vector3f targetPos = al::getTrans(enemy);
        sead::Vector3f sourcePos = al::getSensorPos(attacker);
        sead::Vector3f spawnPos = (sourcePos + targetPos) * 0.5f;
        spawnPos.y += 20.0f;

        al::LiveActor* attackerHost = al::getSensorHost(attacker);

        // Fireball/Iceball specific messages
        if (al::isEqualSubString(typeid(*attackerHost).name(), "FireBrosFireBall")
        ) {
            if (al::sendMsgPlayerFireBallAttack(target, attacker)
                || rs::sendMsgFireBrosFireBallCollide(target, attacker)) return true;
            
            else if (rs::sendMsgHackAttack(target, attacker)
                || al::sendMsgExplosion(target, attacker, nullptr)
            ) {
                if (attackerHost && !al::isEffectEmitting(attackerHost, "Hit")) al::tryEmitEffect(isHakoniwa, "Hit", &spawnPos);
                return true;
            }
        }
        // Standard attack messages (Mario/Cappy attacks)
        else {
            if (rs::sendMsgHackAttack(target, attacker)
                || rs::sendMsgCapReflect(target, attacker)
                || rs::sendMsgCapAttack(target, attacker)
                || al::sendMsgPlayerObjHipDropReflect(target, attacker, nullptr)) return true;
        }
        
        return false;
    }

    // Update frozen actor each frame
    // Returns true if actor remains frozen
    inline bool updateFrozenActor(al::LiveActor* actor) {
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor != actor) continue;

            // Handle cube hit -> kill enemy
            if (frozenList[i].cube && frozenList[i].cube->wasHit()) {
                al::HitSensor* attacker = frozenList[i].cube->getAttackerSensor();
                unfreezeActor(actor);
                sendAttackToEnemy(actor, attacker);
                return false;
            }

            // Maintain frozen state
            al::setActionFrameRate(actor, 0.0f);

            // Check timer expiration
            if (--frozenList[i].timer <= 0) {
                unfreezeActor(actor, true);
                return false;
            }

            return true;
        }
        return false;
    }

    // Clear all frozen actors (call on stage change/warp)
    inline void clearAllFrozen() {
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor && al::isAlive(frozenList[i].actor)) {
                // Destroy cube
                if (frozenList[i].cube) frozenList[i].cube->unfreeze();
                
                // Restore animation and sensors
                al::setActionFrameRate(frozenList[i].actor, 1.0f);
                al::validateHitSensors(frozenList[i].actor);
                
                // Restore previous action
                if (frozenList[i].prevAction) {
                    al::tryStartAction(frozenList[i].actor, frozenList[i].prevAction);
                }
            }
        }
        frozenCount = 0;
    }

    // Prevent frozen enemies from attacking Mario
    inline bool handleReceiveMsg(const al::SensorMsg* msg, al::HitSensor* source) {
        #ifdef ALLOW_POWERUPS
            if (!msg || !source) return false;

            al::LiveActor* attacker = al::getSensorHost(source);
            if (attacker && isFrozen(attacker) && al::isMsgEnemyAttack(msg)) return true;
        #endif
        return false;
    }
}