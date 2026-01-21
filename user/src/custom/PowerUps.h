#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"
#include "custom/_Nerves.h"
#include "custom/PlayerFreeze.h"

namespace PowerUps {

    struct FireBrosFireBallInitArchive : public mallow::hook::Inline<FireBrosFireBallInitArchive> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* actor = reinterpret_cast<al::LiveActor*>(ctx->X[0]);

            if (al::isEqualString(actor->getName(), "MarioIceBall")) ctx->X[8] = reinterpret_cast<u64>("PlayerIceBall");
        }
    };

    inline void executeInitPlayer(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        #ifdef ALLOW_POWERUPS
            auto* model = thisPtr->mModelHolder->findModelActor("Normal");

            isHammer = new HammerBrosHammer("HammerBrosHammer", model, "PlayerHammer", true);
            al::initCreateActorNoPlacementInfo(isHammer, *actorInfo);

            // Create and hide fireballs
            fireBalls = new al::LiveActorGroup("FireBrosFireBall", 4);
            while (!fireBalls->isFull()) {
                auto* fb = new FireBrosFireBall("MarioFireBall", model);
                al::initCreateActorNoPlacementInfo(fb, *actorInfo);
                fireBalls->registerActor(fb);
            }
            fireBalls->makeActorDeadAll();

            // Create and hide iceballs
            iceBalls = new al::LiveActorGroup("PlayerIceBall", 4);
            while (!iceBalls->isFull()) {
                auto* ib = new FireBrosFireBall("MarioIceBall", model);
                al::initCreateActorNoPlacementInfo(ib, *actorInfo);
                iceBalls->registerActor(ib);
            }
            iceBalls->makeActorDeadAll();
        #endif
    }

    struct PlayerActorHakoniwaInitAfterPlacement : public mallow::hook::Trampoline<PlayerActorHakoniwaInitAfterPlacement> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            Orig(thisPtr);
            
            if (isHammer) isHammer->makeActorDead();
            if (fireBalls) fireBalls->makeActorDeadAll();
            if (iceBalls) iceBalls->makeActorDeadAll();
        }
    };

    // shared pre-logic for TryActionCapSpinAttack hooks
    static inline int TryCapSpinPre(PlayerActorHakoniwa* player) {
        bool newIsCarry = player->mCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) { prevIsCarry = newIsCarry; return -1; }
        prevIsCarry = newIsCarry;

        if (isFireThrowing()) return -1;

        if (al::isPadTriggerR(-1)
            && !rs::is2D(player)
            && !player->mCarryKeeper->isCarry()
            && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) canFireball = true;

        return 0; // fallthrough to Orig
    }

    struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack> {
        static bool Callback(PlayerActorHakoniwa* player, bool a2) {
            switch (TryCapSpinPre(player)
            ) {
                case 1:  return true;
                case -1: return false;
            }
            if(Orig(player, a2)) { triggerGalaxySpin = false; return true; }
            return false;
        }
    };

    struct PlayerTryActionCapSpinAttackBindEnd : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttackBindEnd> {
        static bool Callback(PlayerActorHakoniwa* player, bool a2) {
            switch (TryCapSpinPre(player)
            ) {
                case 1:  return true;
                case -1: return false;
            }
            if(Orig(player, a2)) { triggerGalaxySpin = false; return true; }
            return false;
        }
    };

    struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            if (isFireThrowing()) return;

            Orig(thisPtr);
        }
    };

    inline void executeMovement(PlayerActorHakoniwa* thisPtr) {
        #ifdef ALLOW_POWERUPS
            auto* anim   = thisPtr->mAnimator;
            auto* holder = thisPtr->mModelHolder;
            auto* model  = holder->findModelActor("Normal");
            auto* cape = al::tryGetSubActor(model, "ケープ");
            auto* tail = al::tryGetSubActor(model, "尻尾");

            bool isMove = thisPtr->mInput->isMove();
            bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
            bool isWater = al::isInWater(thisPtr);
            bool isSurface = thisPtr->mWaterSurfaceFinder->isFoundSurface();
            bool isVisible = !al::isHideModel(model);
            bool isHack = thisPtr->mHackKeeper && thisPtr->mHackKeeper->mCurrentHackActor;

            f32 speedH = al::calcSpeedH(thisPtr);
            f32 dashBorder = thisPtr->mConst->getDashFastBorderSpeed();

            // Handle hammer attack
            if (isHammer
                && al::isAlive(isHammer)
                && !al::isNerve(thisPtr, &HammerNrv)
            ) {
                isHammer->makeActorDead();
                al::invalidateHitSensor(isHammer, "AttackHack");
            }

            // Handle fireball attack
            const char* jointName = nextThrowLeft ? "HandL" : "HandR";
            const char* fireAnim  = nextThrowLeft ? "FireL" : "FireR";

            al::LiveActorGroup* currentPool = isIce ? iceBalls : fireBalls;
            auto* projectile = (FireBrosFireBall*) currentPool->getDeadActor();

            bool isFullBody = (!isMove && onGround && (!isWater || isSurface));
            bool isFloating = al::isActionPlaying(model, "GlideFloat")
                || al::isActionPlaying(model, "GlideFloatSuper");

            if (isMario || isFire || isIce || isBrawl || isSuper
            ) {
                if (fireStep < 0
                    && (canFireball || isFloating)
                    && al::isPadTriggerR(-1)
                ) {
                    if (projectile && al::isDead(projectile)
                    ) {
                        fireStep = 0;
                        canFireball = false;

                        anim->startUpperBodyAnim(fireAnim);
                        if (isFullBody) anim->startAnim(fireAnim);
                    }
                }
                if (fireStep >= 0
                ) {
                    bool isShooting = anim->isUpperBodyAnim("FireL") || anim->isUpperBodyAnim("FireR")
                        || anim->isAnim("FireL") || anim->isAnim("FireR");

                    if (!isShooting) { fireStep = -1; return; }
                    if (fireStep == 2
                    ) {
                        hitBufferCount = 0;

                        sead::Vector3f startPos;
                        al::calcJointPos(&startPos, model, jointName);
                        sead::Vector3f offset(0.0f, 0.0f, 0.0f);
                        
                        if (isSuper) projectile->shoot(startPos, al::getQuat(model), offset, true, 0, true);
                        else projectile->shoot(startPos, al::getQuat(model), offset, true, 0, false);
                        al::tryStartSe(thisPtr, "FireBallShoot");

                        nextThrowLeft = !nextThrowLeft;
                    }
                    if (isFullBody ? anim->isAnimEnd() : anim->isUpperBodyAnimEnd()
                    ) {
                        if (isFullBody) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                        anim->clearUpperBodyAnim();
                        fireStep = -1;
                    }
                    else fireStep++;
                }
            }
            canFireball = false;

            // Handle cape logic for Mario/Brawl suit
            bool isGliding =
                al::isActionPlaying(model, "Glide")
                || al::isActionPlaying(model, "GlideAlt")
                || al::isActionPlaying(model, "GlideFloatStart")
                || al::isActionPlaying(model, "JumpBroad8")
                || al::isActionPlaying(model, "JumpBroad8Alt")
                || isFloating;
            
            if ((isMario || isBrawl)
                && cape
            ) {
                if (al::isDead(cape)) isCapeActive = -1;
                else if (!isGliding && isCapeActive > 0) {
                    if (--isCapeActive == 0) {
                        cape->kill();
                        al::tryEmitEffect(model, "AppearBloom", nullptr);
                        al::tryStartSe(thisPtr, "Bloom");
                        isCapeActive = -1;
                    }
                }
            }

            // Handle tail logic for Tanooki suit
            if (isTanooki
                && tail && al::isAlive(tail)
            ) {
                if (isGliding) {
                    if (!al::isActionPlaying(tail, "TailSpin")
                    ) {
                        al::tryStartAction(tail, "TailSpin");
                        al::tryEmitEffect(model, "TailSpin", nullptr);
                        al::tryStartSe(thisPtr, "SpinJumpDownFall");
                    }
                } else {
                    if (al::isActionPlaying(tail, "TailSpin")
                    ) {
                        al::tryStartAction(tail, "Wait");
                        al::tryDeleteEffect(model, "TailSpin");
                        al::tryStopSe(thisPtr, "SpinJumpDownFall", -1, nullptr);
                    }
                }
            }

            // Handle logic for Super suit
            if (isSuper) {
                applyMoonMarioConst(thisPtr->mConst); // force Moon physics

                // Apply attack sensor for DashFastSuper
                static bool wasMoveSuper = false;
                bool isMoveSuper = speedH >= dashBorder
                    || anim->isAnim("JumpBroad8") || anim->isAnim("Glide");

                if (isMoveSuper != wasMoveSuper
                ) {
                    if (isMoveSuper) { al::validateHitSensor(thisPtr, "GalaxySpin"); hitBufferCount = 0; }
                    else al::invalidateHitSensor(thisPtr, "GalaxySpin");
                }
                wasMoveSuper = isMoveSuper;
                
                // Apply effects for DashFastSuper
                bool isDash = al::isPadHoldR(-1) && !isFireThrowing() 
                        && al::isActionPlaying(model, "MoveSuper") && speedH >= dashBorder;
                bool isGlide = al::isActionPlaying(model, "Glide") && !isFireThrowing();

                if (isDash) al::tryEmitEffect(model, "DashSuper", nullptr);
                else if (isGlide) al::tryEmitEffect(model, "DashSuperGlide", nullptr);
                else {
                    al::tryDeleteEffect(model, "DashSuper");
                    al::tryDeleteEffect(model, "DashSuperGlide");
                }
                
                // Apply effects for Invincibility
                auto* damagekeep = thisPtr->mDamageKeeper;

                if (!isHack && isVisible
                ) {
                    if (damagekeep) {
                        if (!damagekeep->mIsPreventDamage) damagekeep->activatePreventDamage();
                        damagekeep->mRemainingInvincibility = INT_MAX;
                    }
                    al::tryEmitEffect(model, "Bonfire", nullptr);
                } else {
                    if (isHack && damagekeep) damagekeep->mRemainingInvincibility = 0;
                    al::tryDeleteEffect(model, "Bonfire");
                }
            }

            // Handle life recovery
            static int stillFrames = 0;
            static int healFrames = 0;

            bool isStill = onGround && isVisible && !isMove;
            bool canHeal = (isMario || isNoCap) && isStill && !GameDataFunction::isPlayerHitPointMax(thisPtr);

            if (canHeal) {
                if (stillFrames < 120) stillFrames++;

                int interval = (stillFrames >= 120) ? 60 : 600;
                if (++healFrames >= interval) { GameDataFunction::recoveryPlayer(thisPtr); healFrames = 0; }
            }
            else { stillFrames = 0; healFrames = 0; }

            #ifdef ALLOW_DASH // Handles dash animations and effects
                if (anim && anim->isAnim("JumpDashFast")
                ) {
                    if (isBrawl) anim->startAnim("Jump");
                    else {
                        bool isFlyingSuit = isFeather || isTanooki || isSuper || (isMario && cape && al::isAlive(cape));
                        if (!isFlyingSuit) anim->startAnim("JumpDashFastClassic");
                    }
                }

                bool isMoving = al::isActionPlaying(model, "Move")
                    || al::isActionPlaying(model, "MoveClassic")
                    || al::isActionPlaying(model, "MoveBrawl")
                    || al::isActionPlaying(model, "MoveSuper");

                static bool wasDash = false;
                bool isDashNow = al::isPadHoldR(-1)
                    && isMoving && !isFireThrowing() && speedH >= dashBorder;

                if (isDashNow && !wasDash
                ) {
                    const char* fx = isSuper ? "AccelSecond" : "Accel";
                    if (!al::isEffectEmitting(model, fx)) { al::tryStartSe(thisPtr, fx); al::tryEmitEffect(model, fx, nullptr); }
                }
                wasDash = isDashNow;
            #endif
        #endif
    }

    struct LiveActorMovementHook : public mallow::hook::Trampoline<LiveActorMovementHook> {
        static void Callback(al::LiveActor* actor) {
            // Check if this actor is frozen
            if (PlayerFreeze::updateFrozenActor(actor)) return; // Skip normal movement
            
            Orig(actor);

            static bool hammerEffect = false;

            if (actor != isHammer) return;

            if (!al::isAlive(isHammer)
            ) { 
                hammerEffect = false;
                return;
            }

            al::HitSensor* sensorHammer = al::getHitSensor(isHammer, "AttackHack");
            if (!sensorHammer || !sensorHammer->mIsValid) return;

            if (auto* sensorWall = al::tryGetCollidedWallSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorWall);
            if (auto* sensorCeiling = al::tryGetCollidedCeilingSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorCeiling);
            if (auto* sensorGround = al::tryGetCollidedGroundSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorGround);

            if (!hammerEffect
                && isHakoniwa->mAnimator->isAnim("HammerAttack")
                && al::isCollidedGround(isHammer)
            ) {
                al::tryEmitEffect(isHakoniwa, "HammerLandHit", nullptr);
                al::tryStartSe(isHakoniwa, "HammerLand");
                al::tryStartSe(isHakoniwa, "HammerHit");
                hammerEffect = true;
            }
        }
    };

    struct PlayerCarryKeeperStartCarry : public mallow::hook::Trampoline<PlayerCarryKeeperStartCarry> {
        static void Callback(PlayerCarryKeeper* thisPtr, al::HitSensor* sensor) {
            // if in hammer nerve block carry start
            if (isHakoniwa
                && isHakoniwa->getNerveKeeper()
                && isHakoniwa->getNerveKeeper()->getCurrentNerve() == &HammerNrv) return;
            
            Orig(thisPtr, sensor);
        }
    };

    struct PlayerActorHakoniwaExeJump : public mallow::hook::Trampoline<PlayerActorHakoniwaExeJump> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            auto* anim = thisPtr->mAnimator;
            auto* model = thisPtr->mModelHolder->findModelActor("Normal");
            auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

            bool wasGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
            bool wasWater = al::isInWater(thisPtr);

            Orig(thisPtr);

            if (!isBrawl) return;

            bool isGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
            bool isWater = al::isInWater(thisPtr);
            bool isAir = !isGround && !isWater;

            if (wasWater || (wasGround && isAir)
            ) { 
                isDoubleJump = false; 
                isDoubleJumpConsume = false;
            }
            if (isAir && !isDoubleJump
                && (al::isPadTriggerA(-1) || al::isPadTriggerB(-1))
            ) {
                isDoubleJump = true;
                isDoubleJumpConsume = true;

                if (isBrawl) al::tryEmitEffect(keeper, "DoubleJump", nullptr);
                al::setNerve(thisPtr, getNerveAt(nrvHakoniwaJump));
            }
            if (isDoubleJumpConsume
                && al::isFirstStep(thisPtr)
            ) {
                //if (isFeather || isTanooki) anim->startAnim("JumpDashFast");
                if (isBrawl) anim->startAnim("PoleHandStandJump");
                isDoubleJumpConsume = false;
            }
        }
    };

    struct PlayerStateJumpTryCountUp : public mallow::hook::Trampoline<PlayerStateJumpTryCountUp> {
        static void Callback(PlayerStateJump* state, PlayerContinuousJump* cont) {
            if (isBrawl) return;

            Orig(state, cont);
        }
    };

    struct PlayerActorHakoniwaExeHeadSliding : public mallow::hook::Trampoline<PlayerActorHakoniwaExeHeadSliding> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            Orig(thisPtr);
                    
            auto* anim   = thisPtr->mAnimator;
            auto* model = thisPtr->mModelHolder->findModelActor("Normal");
            auto* cape = al::tryGetSubActor(model, "ケープ");
            auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

            if (!isMario && !isFeather && !isTanooki && !isBrawl && !isSuper) return;

            float vy = al::getVelocity(thisPtr).y;
            if (vy < -2.5f) al::setVelocityY(thisPtr, -2.5f);

            float speed = al::calcSpeed(thisPtr);

            const char* jumpBroadAnim = isTanooki ? "JumpBroad8Alt" : "JumpBroad8";
            const char* glideAnim = isTanooki ? "GlideAlt" : "Glide";
            const char* glideFloatAnim = isSuper ? "GlideFloatSuper" : "GlideFloat";

            if (al::isFirstStep(thisPtr)
            ) {
                if ((isMario || isBrawl) 
                    && cape && al::isDead(cape)
                ) {
                    cape->appear();
                    al::tryEmitEffect(keeper, "AppearBloom", nullptr);
                    al::tryStartSe(thisPtr, "Bloom");
                }
                anim->startAnim(jumpBroadAnim);
            }
            else if (anim->isAnimEnd() && anim->isAnim(jumpBroadAnim)) anim->startAnim(glideAnim);
            else if (speed < 10.f) {
                if (anim->isAnim(glideAnim)) anim->startAnim("GlideFloatStart");
                if (anim->isAnimEnd() && anim->isAnim("GlideFloatStart")) anim->startAnim(glideFloatAnim);
            }
            if (al::isGreaterStep(thisPtr, 25)
            ) {
                if (al::isPadTriggerA(-1)
                    || al::isPadTriggerB(-1)
                ) {
                    if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaFall))) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                }

                if (al::isPadTriggerZL(-1)
                    || al::isPadTriggerZR(-1)
                ) {
                    if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop))) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop));
                }
                if (isPadTriggerGalaxySpin(-1)
                ) {
                    if (!al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))
                    ) {
                        canGalaxySpin = true;
                        canStandardSpin = true;
                        isGalaxyAfterStandardSpin = false;
                        isStandardAfterGalaxySpin = false;

                        triggerGalaxySpin = true;
                        al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                    }
                }
                else if (al::isPadTriggerX(-1) || al::isPadTriggerY(-1)
                ) {
                    if (!al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))
                    ) {
                        canGalaxySpin = true;
                        canStandardSpin = true;
                        isGalaxyAfterStandardSpin = false;
                        isStandardAfterGalaxySpin = false;

                        triggerGalaxySpin = false;
                        al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                    }
                }
            }
        }
    };

    struct PlayerHeadSlidingKill : public mallow::hook::Trampoline<PlayerHeadSlidingKill> {
        static void Callback(PlayerStateHeadSliding * state) {
            isCapeActive = 1200;
            if (state->mAnimator) state->mAnimator->clearUpperBodyAnim();
            Orig(state);
        }
    };

    struct PlayerConstGetHeadSlidingSpeed : public mallow::hook::Trampoline<PlayerConstGetHeadSlidingSpeed> {
        static float Callback(const PlayerConst* thisPtr) {
            float speed = Orig(thisPtr);

            if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return speed;
            if (isSuper) speed *= 1.5f;
            return speed;
        }
    };

    struct PlayerInputFunctionIsHoldAction : public mallow::hook::Trampoline<PlayerInputFunctionIsHoldAction> {
        static bool Callback(const al::LiveActor* actor, s32 port) {
            bool isFlying = isHakoniwa && isHakoniwa->mHackCap && isHakoniwa->mHackCap->isFlying();

            return Orig(actor, port) || (al::isPadHoldR(port) && !isFlying);
        }
    };

    struct PlayerActionGroundMoveControlUpdate : public mallow::hook::Trampoline<PlayerActionGroundMoveControlUpdate> {
        static float Callback(PlayerActionGroundMoveControl* thisPtr) {
            float update = Orig(thisPtr);

            if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return update;
            PlayerConst* playerConst = const_cast<PlayerConst*>(thisPtr->mConst);

            bool isDash = al::isPadHoldR(-1) && !isFireThrowing();

            if (isSuper && isDash
            ) {
                playerConst->mNormalMaxSpeed = 28.0f;
                thisPtr->mMaxSpeed = 28.0f;
            }
            else if (isDash
            ) {
                playerConst->mNormalMaxSpeed = 21.0f;
                thisPtr->mMaxSpeed = 21.0f;
            }
            else {
                playerConst->mNormalMaxSpeed = 14.0f;
                thisPtr->mMaxSpeed = 14.0f;
            }
            return update;
        }
    };

    struct PlayerAnimControlRunUpdate : public mallow::hook::Inline<PlayerAnimControlRunUpdate> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return;
            if (isSuper) *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("MoveSuper"); //mMoveAnimName in PlayerAnimControlRun
            else if (isBrawl) *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("MoveBrawl");
            else if (isFeather || isTanooki) *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("Move");
            else *reinterpret_cast<u64*>(ctx->X[0] + 0x38) = reinterpret_cast<u64>("MoveClassic");
        }
    };

    struct PlayerSeCtrlUpdateMove : public mallow::hook::Inline<PlayerSeCtrlUpdateMove> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mCurrentHackActor) return;
            if (isSuper) ctx->X[8] = reinterpret_cast<u64>("MoveSuper");
            else if (isBrawl) ctx->X[8] = reinterpret_cast<u64>("MoveBrawl");
            else if (isFeather || isTanooki) ctx->X[8] = reinterpret_cast<u64>("Move");
            else ctx->X[8] = reinterpret_cast<u64>("MoveClassic");
        }
    };

    struct PlayerSeCtrlUpdateWearEnd : public mallow::hook::Inline<PlayerSeCtrlUpdateWearEnd> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (isBrawl) ctx->X[20] = reinterpret_cast<u64>("WearEndBrawl");
            if (isSuper) ctx->X[20] = reinterpret_cast<u64>("WearEndSuper");
        }
    };

    struct StartWaterSurfaceRunJudge : public mallow::hook::Trampoline<StartWaterSurfaceRunJudge> {
        static bool Callback(const PlayerJudgeStartWaterSurfaceRun* thisPtr) {
            if (isSuper) {
                return thisPtr->mWaterSurfaceFinder->isFoundSurface()
                    && al::isNearZeroOrGreater(thisPtr->mWaterSurfaceFinder->getDistance())
                    && al::getGravity(thisPtr->mPlayer).dot(al::getVelocity(thisPtr->mPlayer)) >= 0.0f
                    && al::calcSpeedH(thisPtr->mPlayer) >= MIN_SPEED_RUN_ON_WATER;
            }
            else {
                return Orig(thisPtr);
            }
        }
    };

    struct WaterSurfaceRunJudge : public mallow::hook::Trampoline<WaterSurfaceRunJudge> {
        static bool Callback(const PlayerJudgeWaterSurfaceRun* thisPtr) {
            isSuperRunningOnSurface = false;

            if (isSuper) {
                bool result = thisPtr->mWaterSurfaceFinder->isFoundSurface()
                    && al::isNearZeroOrGreater(thisPtr->mWaterSurfaceFinder->getDistance())
                    && al::calcSpeedH(thisPtr->mPlayer) >= MIN_SPEED_RUN_ON_WATER;
                isSuperRunningOnSurface = result;
                return result;
            }
            else {
                return Orig(thisPtr);
            }
        }
    };

    struct RunWaterSurfaceDisableSink : public mallow::hook::Inline<RunWaterSurfaceDisableSink> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            // move value > 0 into W8 to cause skipping the "sink" part
            if (isSuperRunningOnSurface) ctx->W[8] = 1;
        }
    };

    struct WaterSurfaceRunDisableSlowdown : public mallow::hook::Inline<WaterSurfaceRunDisableSlowdown> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            // used to redirect `turnVecToVecRate` into this location instead of the proper one, so the call is basically ignored
            static sead::Vector3f garbageVec;
            if (isSuperRunningOnSurface) ctx->X[0] = reinterpret_cast<u64>(&garbageVec);
        }
    };

    struct RsIsTouchDeadCode : public mallow::hook::Trampoline<RsIsTouchDeadCode> {
        static bool Callback(const al::LiveActor* actor, const IUsePlayerCollision* coll, const IPlayerModelChanger* changer, const IUseDimension* dim, float f) {
            if (isSuper) return false;
            return Orig(actor, coll, changer, dim, f);
        }
    };

    struct RsIsTouchDamageFireCode : public mallow::hook::Trampoline<RsIsTouchDamageFireCode> {
        static bool Callback(const al::LiveActor* actor, const IUsePlayerCollision* coll, const IPlayerModelChanger* changer) {
            if (isSuper) return false;
            return Orig(actor, coll, changer);
        }
    };

    inline void Install() {
        #ifdef ALLOW_POWERUPS
            FireBrosFireBallInitArchive::InstallAtOffset(0x10082C);
            PlayerActorHakoniwaInitAfterPlacement::InstallAtSymbol("_ZN19PlayerActorHakoniwa18initAfterPlacementEv");
            
            #ifdef ALLOW_CAPPY_ONLY // Handles Fireball logic
                PlayerTryActionCapSpinAttack::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
                PlayerTryActionCapSpinAttackBindEnd::InstallAtSymbol("_ZN19PlayerActorHakoniwa29tryActionCapSpinAttackBindEndEv");
                PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");
            #endif

            // Handles control/movement
            LiveActorMovementHook::InstallAtSymbol("_ZN2al9LiveActor8movementEv");

            // Handles Hammer while Carrying
            PlayerCarryKeeperStartCarry::InstallAtSymbol("_ZN17PlayerCarryKeeper10startCarryEPN2al9HitSensorE");

            // Handles Double Jump
            PlayerActorHakoniwaExeJump::InstallAtSymbol("_ZN19PlayerActorHakoniwa7exeJumpEv");
            PlayerStateJumpTryCountUp::InstallAtSymbol("_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump");
            
            // Handles Glide
            PlayerActorHakoniwaExeHeadSliding::InstallAtSymbol("_ZN19PlayerActorHakoniwa14exeHeadSlidingEv");
            PlayerHeadSlidingKill::InstallAtSymbol("_ZN22PlayerStateHeadSliding4killEv");
            PlayerConstGetHeadSlidingSpeed::InstallAtSymbol("_ZNK11PlayerConst19getHeadSlidingSpeedEv");

            #ifdef ALLOW_DASH // Handles Dash
                PlayerInputFunctionIsHoldAction::InstallAtSymbol("_ZN19PlayerInputFunction12isHoldActionEPKN2al9LiveActorEi");
                PlayerActionGroundMoveControlUpdate::InstallAtSymbol("_ZN29PlayerActionGroundMoveControl6updateEv");
                PlayerAnimControlRunUpdate::InstallAtOffset(0x42C6BC);
                PlayerSeCtrlUpdateMove::InstallAtOffset(0x463038);

                // Handles running on water
                StartWaterSurfaceRunJudge::InstallAtSymbol("_ZNK31PlayerJudgeStartWaterSurfaceRun5judgeEv");
                WaterSurfaceRunJudge::InstallAtSymbol("_ZNK26PlayerJudgeWaterSurfaceRun5judgeEv");
                RunWaterSurfaceDisableSink::InstallAtOffset(0x48023C);
                WaterSurfaceRunDisableSlowdown::InstallAtOffset(0x4184C0);
                RsIsTouchDeadCode::InstallAtSymbol("_ZN2rs15isTouchDeadCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChangerPK13IUseDimensionf");
                RsIsTouchDamageFireCode::InstallAtSymbol("_ZN2rs21isTouchDamageFireCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChanger");
            #endif

            // Handles WearEnd
            PlayerSeCtrlUpdateWearEnd::InstallAtOffset(0x463DE0);

            // Disable invincibility music patches
            exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
            invincibleStartPatcher.WriteInst(0x1F2003D5); // NOP
            exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
            invinciblePatcher.WriteInst(0x1F2003D5); // NOP
        #endif
    }
}