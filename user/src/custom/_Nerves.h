#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"

// Custom Nerves
class PlayerStateSpinCapNrvGalaxySpinAir; 
extern PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir; 

class PlayerStateSpinCapNrvGalaxySpinGround : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
        PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        bool isCape = (isMario && cape && al::isAlive(cape)) || isFeather;

        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");
        bool isRotatingL = state->mAnimator->isAnim("SpinGroundL");
        bool isRotatingR = state->mAnimator->isAnim("SpinGroundR");
        bool isCarrying = player->mCarryKeeper->isCarry();
        bool isFinish = state->mAnimator->isAnim("KoopaCapPunchFinishL")
            || state->mAnimator->isAnim("KoopaCapPunchFinishR");
        bool didSpin = player->mInput->isSpinInput();
        int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;

        isSpinActive = true;

        if (al::isFirstStep(state)
        ) {
            state->mAnimator->endSubAnim();
            isPunchRight = !isPunchRight;

            if (!isSpinning) {
                if (didSpin) {
                    if (spinDir > 0) {
                        state->mAnimator->startSubAnim("SpinAttackLeft");
                        state->mAnimator->startAnim ("SpinAttackLeft");
                    }
                    else {
                        state->mAnimator->startSubAnim("SpinAttackRight");
                        state->mAnimator->startAnim ("SpinAttackRight");
                    }
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingL) {
                    state->mAnimator->startSubAnim("SpinAttackLeft");
                    state->mAnimator->startAnim("SpinAttackLeft");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingR) {
                    state->mAnimator->startSubAnim("SpinAttackRight");
                    state->mAnimator->startAnim("SpinAttackRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isCarrying) {
                    state->mAnimator->startSubAnim("SpinSeparate");
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else if (isNearCollectible) {
                    state->mAnimator->startAnim("RabbitGet");
                    al::validateHitSensor(state->mActor, "Punch");
                } else if (isNearTreasure || isNearSwoonedEnemy) {
                    state->mAnimator->startAnim("Kick");
                    al::validateHitSensor(state->mActor, "Punch");
                } else {
                    if (isCape) {
                        al::setNerve(state, reinterpret_cast<al::Nerve*>(&GalaxySpinAir));
                        return;
                    } else if (isTanooki) {
                        state->mAnimator->startSubAnim("TailAttack");
                        state->mAnimator->startAnim("TailAttack");
                        al::validateHitSensor(state->mActor, "GalaxySpin");
                        galaxySensorRemaining = 21;
                    } else {
                    #ifdef ALLOW_SPIN_ATTACK // Only spin attack
                        state->mAnimator->startSubAnim("SpinSeparate");
                        state->mAnimator->startAnim("SpinSeparate");
                        al::validateHitSensor(state->mActor, "GalaxySpin");
                        galaxySensorRemaining = 21;
                    #else
                        if (isFinalPunch) {
                            if (isPunchRight) {
                                state->mAnimator->startSubAnim("KoopaCapPunchFinishRStart");
                                state->mAnimator->startAnim("KoopaCapPunchFinishR");
                            } else {
                                state->mAnimator->startSubAnim("KoopaCapPunchFinishLStart");
                                state->mAnimator->startAnim("KoopaCapPunchFinishL");
                            }
                            isFinalPunch = false;
                        } else {
                            if (isPunchRight) {
                                state->mAnimator->startSubAnim("KoopaCapPunchRStart");
                                state->mAnimator->startAnim("KoopaCapPunchR");
                            } else {
                                state->mAnimator->startSubAnim("KoopaCapPunchLStart");
                                state->mAnimator->startAnim("KoopaCapPunchL");
                            }
                        }
                        // Make winding up invincible
                        al::invalidateHitSensor(state->mActor, "Foot");
                        al::invalidateHitSensor(state->mActor, "Body");
                        al::invalidateHitSensor(state->mActor, "Head");

                        isPunching = true; // Validate punch animations*/
                    #endif
                    }
                }
            }
        }
        
        if (!isSpinning && !isCarrying
            && !isNearCollectible && !isNearTreasure && !isNearSwoonedEnemy
            && !isRotatingL && !isRotatingR
            && !isTanooki
        ) {
            if (al::isStep(state, 3)) {
                // Reduce Mario's existing momentum by 50%
                sead::Vector3 currentVelocity = al::getVelocity(player);
                currentVelocity *= 0.5f;
                al::setVelocity(player, currentVelocity);
    
                // Apply a small forward movement during the punch
                sead::Vector3f forward;
                al::calcQuatFront(&forward, player);
                forward.normalize();
                forward *= 5.0f;
                al::addVelocity(player, forward);
            }
            if (al::isStep(state, 6)) {
                // Make Mario vulnerable again
                al::validateHitSensor(state->mActor, "Foot");
                al::validateHitSensor(state->mActor, "Body");
                al::validateHitSensor(state->mActor, "Head");
                al::validateHitSensor(state->mActor, "Punch");
                //galaxySensorRemaining = 15;
            }
        }
        
        if (isFinish) al::setVelocity(player, sead::Vector3f::zero);
        else state->updateSpinGroundNerve();

        if (al::isGreaterStep(state, 41)) al::invalidateHitSensor(state->mActor, "DoubleSpin");
        if (al::isGreaterStep(state, 21)) al::invalidateHitSensor(state->mActor, "GalaxySpin");
        if (al::isGreaterStep(state, 15)) al::invalidateHitSensor(state->mActor, "Punch");

        if (state->mAnimator->isAnimEnd()) {
            state->kill();
            isSpinActive = false;
        }
    }
};

class PlayerStateSpinCapNrvGalaxySpinAir : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
        PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        bool isCape = (isMario && cape && al::isAlive(cape)) || isFeather;

        bool isRotatingAirL  = state->mAnimator->isAnim("StartSpinJumpL")
            || state->mAnimator->isAnim("RestartSpinJumpL");
        bool isRotatingAirR  = state->mAnimator->isAnim("StartSpinJumpR")
            || state->mAnimator->isAnim("RestartSpinJumpR");
        bool isCarrying = player->mCarryKeeper->isCarry();
        bool didSpin = player->mInput->isSpinInput();
        int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;
        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");

        isSpinActive = true;

        if (state->mAnimator->isAnim("CapeAttack")
            && cape && al::isDead(cape)
        ) {
            state->mAnimator->startAnim("SpinSeparate");
            al::validateHitSensor(state->mActor, "GalaxySpin"); 
            galaxySensorRemaining = 21; 
        }
        
        if(al::isFirstStep(state)
        ) {
            const char* cur = state->mAnimator->mCurAnim.cstr();
            if (!al::isEqualSubString(cur, "SpinCap")) state->mAnimator->endSubAnim();
            
            if (!isSpinning) {
                if (didSpin) {
                    if (spinDir > 0) state->mAnimator->startAnim("SpinAttackAirLeft");
                    else state->mAnimator->startAnim("SpinAttackAirRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingAirL) {
                    state->mAnimator->startAnim("SpinAttackAirLeft");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isRotatingAirR) {
                    state->mAnimator->startAnim("SpinAttackAirRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    galaxySensorRemaining = 41;
                } else if (isCarrying) {
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else if (isCape) {
                    state->mAnimator->startAnim("CapeAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else if (isTanooki) {
                    state->mAnimator->startAnim("TailAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                } else {
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    galaxySensorRemaining = 21;
                }
            }
        }
        
        state->updateSpinAirNerve();

        if ((isCape || isTanooki)
            && state->mAnimator->isAnimEnd()
        ) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
        if (!isSpinning
            && al::isGreaterStep(state, 41)
        ) {
            al::invalidateHitSensor(state->mActor, "DoubleSpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
        if (isSpinning
            && al::isGreaterStep(state, 21)
        ) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
    }
};

class PlayerActorHakoniwaNrvTauntLeft : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* anim = player->mAnimator;

        al::setVelocity(player, sead::Vector3f::zero);

        if (al::isFirstStep(player)
        ) {
            anim->endSubAnim();
            
            if (isFire || isBrawl) anim->startAnim("WearEndBrawl");
            else if (isSuper) anim->startAnim("WearEndSuper");
            else anim->startAnim("WearEnd");
        }

        if (anim->isAnimEnd()
        ) {
            al::setNerve(player, getNerveAt(nrvHakoniwaWait));
            return;
        }
    }
};

class PlayerActorHakoniwaNrvTauntRight : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* anim = player->mAnimator;
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        auto* effect = static_cast<al::IUseEffectKeeper*>(model);

        al::setVelocity(player, sead::Vector3f::zero);

        if (al::isFirstStep(player)
        ) {
            anim->endSubAnim();

            if (tauntRightAlt) {
                if (isBrawl) {
                    if (cape && al::isDead(cape)) anim->startAnim("LandJump3");
                    else anim->startAnim("TauntFeather");
                }
                else if (isFire || isIce || isSuper) anim->startAnim("TauntSuper");
                else if (isFeather || isTanooki) anim->startAnim("AreaWaitSayCheese");
                else anim->startAnim("AreaWait64");
            }
            else if (isIce) {
                anim->startAnim("TauntIce");
                al::tryStartSe(player, "IceOn");
            }
            else if (isFire || isBrawl || isSuper) anim->startAnim("TauntFire");
            else if (isFeather || isTanooki) anim->startAnim("TauntFeather");
            else anim->startAnim("TauntMario");
        }

        if (anim->isAnim("LandJump3")
        ) {
            if (al::isStep(player, 25)
            ) {
                if (cape) cape->appear();
                isCapeActive = 1200;
                al::tryEmitEffect(effect, "AppearBloom", nullptr);
                al::tryStartSe(player, "Bloom");
            }
        }
        else if (anim->isAnim("TauntFire")
        ) {
            if (al::isStep(player, 65)) al::tryStartSe(player, "FireOn");
            if (al::isStep(player, 160)
            ) {
                al::tryStopSe(player, "FireOn", -1, nullptr);
                al::tryStartSe(player, "FireOff");
            }
        }
        else if (anim->isAnim("TauntIce")
        ) {
            if (al::isStep(player, 160)
            ) {
                al::tryStopSe(player, "IceOn", -1, nullptr);
                al::tryStartSe(player, "IceOff");
            }
        }
        else if (anim->isAnim("TauntSuper")
        ) {
            if (isFire) player->mStainControl->recordDamageFire();
            if (isIce) player->mStainControl->recordIceWater();

            if (al::isStep(player, 14)
            ) {
                if (isIce) {
                    al::tryEmitEffect(effect, "IceEffect", nullptr);
                    al::tryStartSe(player, "FireOn");
                }
                if (isFire || isSuper) {
                    al::tryEmitEffect(effect, "BonfireSuper", nullptr);
                    al::tryStartSe(player, "FireOn");
                }
                if (isSuper) {
                    al::tryEmitEffect(player, "InvincibleStart", nullptr);
                    al::tryEmitEffect(effect, "ChargeSuper", nullptr);
                    al::tryStartSe(player, "StartInvincible");
                }
            }
        }

        if (anim->isAnimEnd()
        ) {
            tauntRightAlt = false;
            al::tryDeleteEffect(effect, "BonfireSuper");
            al::tryDeleteEffect(effect, "IceEffect");
            al::tryStopSe(player, "FireOn", -1, nullptr);
            al::tryStopSe(player, "IceOn", -1, nullptr);
            al::setNerve(player, getNerveAt(nrvHakoniwaWait));
            return;
        }
    }
};

class PlayerActorHakoniwaNrvHammer : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* hammer = al::tryGetSubActor(model, "Hammer");

        bool isGround = rs::isOnGround(player, player->mCollider);
        bool isWater = al::isInWater(player);
        bool isSurface = player->mWaterSurfaceFinder->isFoundSurface();

        const sead::Matrix34f* mL = al::getJointMtxPtr(model, "ArmL2");
        const sead::Matrix34f* mR = al::getJointMtxPtr(model, "ArmR2");

        sead::Vector3 posL = mL->getTranslation();
        sead::Vector3 posR = mR->getTranslation();
        sead::Vector3 mid  = (posL + posR) * 0.5f;

        sead::Quatf qMid;
        sead::Quatf qL; mL->toQuat(qL);
        sead::Quatf qR; mR->toQuat(qR);
        al::slerpQuat(&qMid, qL, qR, 0.5f);

        static sead::Matrix34f hammerMtx;
        hammerMtx.makeQT(qMid, mid);
               
        if (al::isFirstStep(player)
        ) {
            player->mAnimator->endSubAnim();
            hitBufferCount = 0;

            if (hammer) al::hideModelIfShow(hammer);

            isHammer->makeActorAlive();
            isHammer->attach(
            &hammerMtx,
            sead::Vector3(0.0f, -12.5f, -37.5f),
            sead::Vector3(0.0f, sead::Mathf::deg2rad(-90.0f), 0.0f),
            nullptr);

            al::onCollide(isHammer);
            al::invalidateClipping(isHammer);
            al::showShadow(isHammer);

            if (!isGround) {
                player->mAnimator->startAnim("RollingStart");
                al::validateHitSensor(isHammer, "AttackHack");
            } else {
                player->mAnimator->startAnim("HammerAttack");
            }
        }
        if (player->mAnimator->isAnimEnd()
            && player->mAnimator->isAnim("RollingStart")
        ) {
            player->mAnimator->startAnim("Rolling");
            al::tryStartAction(isHammer, "Spin");
        }
        if (isGround
            && (player->mAnimator->isAnim("RollingStart") || player->mAnimator->isAnim("Rolling"))
        ) {
            player->mAnimator->endSubAnim();
            al::tryStartAction(isHammer, "Hammer");
            player->mAnimator->startAnim("HammerAttack");
        }
        if (al::isStep(player, 3)
        ) {
            sead::Vector3 currentVelocity = al::getVelocity(player);
            if (isGround) {
                currentVelocity *= 0.5f;
            } else {
                if (currentVelocity.y > 0.0f) currentVelocity.y = 0.0f;
                //currentVelocity += al::getGravity(player);
            }
            al::setVelocity(player, currentVelocity);
        }

        if (!isGround) al::addVelocity(player, (al::getGravity(player) * 0.5f));

        if (al::isStep(player, 6)) al::validateHitSensor(isHammer, "AttackHack");
        
        if (player->mAnimator->isAnimEnd()
        ) {
            isHammer->makeActorDead();
            if (hammer) al::showModelIfHide(hammer);
            al::offCollide(isHammer);
            al::invalidateHitSensor(isHammer, "AttackHack");
            al::setNerve(player, getNerveAt(nrvHakoniwaFall));
            return;
        }
        else if (isWater && !isSurface
        ) {
            isHammer->makeActorDead();
            if (hammer) al::showModelIfHide(hammer);
            al::offCollide(isHammer);
            al::invalidateHitSensor(isHammer, "AttackHack");
            al::setNerve(player, getNerveAt(nrvHakoniwaFall));
            al::tryEmitEffect(isHammer, "Break", nullptr);
            return;
        }
        if (hammer && al::isDead(isHammer)) al::showModelIfHide(hammer);
    }

    void executeOnEnd(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* model  = player->mModelHolder->findModelActor("Normal");
        auto* hammer = al::tryGetSubActor(model, "Hammer");

        if (hammer) al::showModelIfHide(hammer);

        if (isHammer) {
            al::offCollide(isHammer);
            al::invalidateHitSensor(isHammer, "AttackHack");
            isHammer->makeActorDead();
        }
    }
};

PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;
PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;
PlayerActorHakoniwaNrvTauntLeft TauntLeftNrv;
PlayerActorHakoniwaNrvTauntRight TauntRightNrv;
PlayerActorHakoniwaNrvHammer HammerNrv;