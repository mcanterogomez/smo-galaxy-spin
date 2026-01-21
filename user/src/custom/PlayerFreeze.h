#pragma once

#include "ModConfig.h"
#include "custom/_Globals.h"

namespace PlayerFreeze {

    // Frozen actor tracking
    struct FrozenState { al::LiveActor* actor; int timer; const char* prevAction; };
    inline FrozenState frozenList[32];
    inline int frozenCount = 0;

    inline void freezeActor(al::LiveActor* actor, int duration) {
        // Check if already frozen
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor == actor) return;
        }
        
        // Add to list
        if (frozenCount < 32) {
            const char* curAction = al::getActionName(actor);
            bool isBlowDown = al::tryStartAction(actor, "BlowDown");

            frozenList[frozenCount] = { actor, duration, isBlowDown ? curAction : nullptr };
            frozenCount++;

            al::setActionFrameRate(actor, 0.0f);
            al::setVelocity(actor, sead::Vector3f::zero);
        }
    }

    inline bool isFrozen(al::LiveActor* actor) {
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor == actor) return true;
        }
        return false;
    }

    inline void unfreezeActor(al::LiveActor* actor, bool isTimer = false) {
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor == actor) {
                al::setActionFrameRate(actor, 1.0f);
                al::setVelocity(actor, sead::Vector3f::zero);

                if (isTimer) {                        
                    if (frozenList[i].prevAction) al::tryStartAction(actor, frozenList[i].prevAction);
                }
                
                // Remove from list
                frozenList[i] = frozenList[frozenCount - 1];
                frozenCount--;
                return;
            }
        }
    }

    inline bool updateFrozenActor(al::LiveActor* actor) {
        for (int i = 0; i < frozenCount; i++) {
            if (frozenList[i].actor == actor) {
                al::setActionFrameRate(actor, 0.0f);
                al::setVelocity(actor, sead::Vector3f::zero);

                if (actor->getHitSensorKeeper()) {
                    actor->getHitSensorKeeper()->update();
                    actor->getHitSensorKeeper()->attackSensor();
                }
                
                if (--frozenList[i].timer <= 0) {
                    unfreezeActor(actor, true);
                    return false;
                }
                return true;
            }
        }
        return false;
    }

    // Handle freezed enemies attacking Mario
    inline bool handleReceiveMsg(const al::SensorMsg* msg, al::HitSensor* source) {
        #ifdef ALLOW_POWERUPS
            if (!msg || !source) return false;

            al::LiveActor* attacker = al::getSensorHost(source);
            if (attacker && PlayerFreeze::isFrozen(attacker)
            ) {
                if (al::isMsgEnemyAttack(msg)) return true;
            }
        #endif
        return false;
    }

    // Handle Mario attacking freezed enemies
    struct SendMsgSensorToSensorUnfreeze : public mallow::hook::Trampoline<SendMsgSensorToSensorUnfreeze> {
        static bool Callback(const al::SensorMsg& message, al::HitSensor* sender, al::HitSensor* receiver) {

            al::LiveActor* target = receiver ? receiver->mParentActor : nullptr;

            if (target && PlayerFreeze::isFrozen(target)
            ) {
                const char* type = typeid(message).name();

                if (type && (al::isEqualSubString(type, "Trample")
                    || al::isEqualSubString(type, "Reflect")
                    || al::isEqualSubString(type, "HipDrop")
                    || al::isEqualSubString(type, "Kick")
                    || al::isEqualSubString(type, "Spin")
                    || al::isEqualSubString(type, "Punch")
                    || al::isEqualSubString(type, "Cap")
                    || al::isEqualSubString(type, "Hack"))) PlayerFreeze::unfreezeActor(target);
            }

            return Orig(message, sender, receiver);
        }
    };

    inline void Install() {
        #ifdef ALLOW_POWERUPS
            SendMsgSensorToSensorUnfreeze::InstallAtSymbol("_ZN21alActorSensorFunction21sendMsgSensorToSensorERKN2al9SensorMsgEPNS0_9HitSensorES5_");
        #endif
    }
}