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

        inline void unfreezeActor(al::LiveActor* actor) {
            for (int i = 0; i < frozenCount; i++) {
                if (frozenList[i].actor == actor) {
                    al::setActionFrameRate(actor, 1.0f);
                    al::setVelocity(actor, sead::Vector3f::zero);
                    
                    if (frozenList[i].prevAction) al::tryStartAction(actor, frozenList[i].prevAction);
                    
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
                        unfreezeActor(actor);
                        return false;
                    }
                    return true;
                }
            }
            return false;
        }

    struct PlayerActorHakoniwaReceiveMsg : public mallow::hook::Trampoline<PlayerActorHakoniwaReceiveMsg> {
        static bool Callback(PlayerActorHakoniwa* thisPtr, const al::SensorMsg* message, al::HitSensor* other, al::HitSensor* self) {
            // Check if the attacker is frozen
            al::LiveActor* attacker = al::getSensorHost(other);

            if (attacker && PlayerFreeze::isFrozen(attacker)) 
                if (al::isMsgEnemyAttack(message)) return false;
            
            return Orig(thisPtr, message, other, self);
        }
    };

    inline void Install() {
        #ifdef ALLOW_POWERUPS
            PlayerActorHakoniwaReceiveMsg::InstallAtSymbol("_ZN19PlayerActorHakoniwa10receiveMsgEPKN2al9SensorMsgEPNS0_9HitSensorES5_");
        #endif
    }
}