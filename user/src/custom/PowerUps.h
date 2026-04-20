#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"
#include "custom/_Nerves.h"
#include "custom/PlayerFreeze.h"
#include "headers/PlayerIceCube.h"
#include "custom/WallStick.h"

namespace PowerUps {

    struct FireBrosFireBallInitArchive : public mallow::hook::Inline<FireBrosFireBallInitArchive> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* actor = reinterpret_cast<al::LiveActor*>(ctx->X[0]);

            if (isIce && al::isEqualString(actor->getName(), "MarioIceBall")) ctx->X[8] = reinterpret_cast<u64>("PlayerIceBall");
        }
    };

    struct InitActorSuffixHook : public mallow::hook::Trampoline<InitActorSuffixHook> {
        static void Callback(al::LiveActor* actor, const al::ActorInitInfo& info, const char* suffix) {
            if (actor == (al::LiveActor*)isKart) {
                al::initActorWithArchiveName(actor, info, "PlayerKart", suffix);
                return;
            }
            Orig(actor, info, suffix);
        }
    };

    struct InitActorArchiveHook : public mallow::hook::Trampoline<InitActorArchiveHook> {
        static void Callback(al::LiveActor* actor, const al::ActorInitInfo& info, const sead::SafeString& archive, const char* suffix) {
            if (al::isEqualString(actor->getName(), "MarioTankBullet")) {
                sead::SafeString custom("PlayerBullet");
                Orig(actor, info, custom, suffix);
                return;
            }
            Orig(actor, info, archive, suffix);
        }
    };

    inline void executeInitPlayer(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        #ifdef ALLOW_POWERUPS
            auto* model = thisPtr->mModelHolder->findModelActor("Normal");
            glideLean = 0.0f;
            glidePitch = 0.0f;
            
            al::initJointLocalYRotator(model, &glideLean, "JointRoot");
            al::initJointLocalZRotator(model, &glidePitch, "Spine1");

            if (al::isExistArchive("ObjectData/PlayerHammer")
            ) {
                isHammer = new HammerBrosHammer("HammerBrosHammer", model, "PlayerHammer", true);
                al::initCreateActorNoPlacementInfo(isHammer, *actorInfo);
            }

            // Create and hide fireballs
            fireBalls = new al::LiveActorGroup("FireBrosFireBall", 4);
            while (!fireBalls->isFull()) {
                auto* fb = new FireBrosFireBall("MarioFireBall", model);
                al::initCreateActorNoPlacementInfo(fb, *actorInfo);
                fireBalls->registerActor(fb);
            }
            fireBalls->makeActorDeadAll();

            // Create and hide iceballs
            if (al::isExistArchive("ObjectData/PlayerIceBall")
            ) {
                iceBalls = new al::LiveActorGroup("PlayerIceBall", 4);
                while (!iceBalls->isFull()) {
                    auto* ib = new FireBrosFireBall("MarioIceBall", model);
                    al::initCreateActorNoPlacementInfo(ib, *actorInfo);
                    iceBalls->registerActor(ib);
                }
                iceBalls->makeActorDeadAll();
            }

            // Create ice cube
            if (al::isExistArchive("ObjectData/PlayerIceCube")
            ) {
                iceCubes = new al::LiveActorGroup("IceCubes", 32);
                while (!iceCubes->isFull()) {
                    auto* cube = new PlayerIceCube("IceCube");
                    al::initCreateActorNoPlacementInfo(cube, *actorInfo);
                    iceCubes->registerActor(cube);
                }
            }

            // Create and hide tank bullets
            if (al::isExistArchive("ObjectData/PlayerBullet")
            ) {
                tankBullets = new al::LiveActorGroup("TankBullet", 4);
                while (!tankBullets->isFull()) {
                    auto* tb = new TankBullet("MarioTankBullet");
                    al::initCreateActorNoPlacementInfo(tb, *actorInfo);
                    tankBullets->registerActor(tb);
                }
                tankBullets->makeActorDeadAll();
            }

            // Create custom gauge
            isGauge = new CustomGauge(*actorInfo->layoutInitInfo);

            #ifdef ALLOW_KART
                // Create custom kart
                if (al::isExistArchive("ObjectData/PlayerKart")
                ) {
                    isKart = new Motorcycle("Kart");
                    al::initCreateActorNoPlacementInfo(isKart, *actorInfo);
                    isKart->makeActorDead();
                }
            #endif
        #endif
    }

    struct PlayerActorHakoniwaInitAfterPlacement : public mallow::hook::Trampoline<PlayerActorHakoniwaInitAfterPlacement> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            Orig(thisPtr);

            PlayerFreeze::clearAllFrozen();

            if (isHammer) isHammer->makeActorDead();
            if (fireBalls) fireBalls->makeActorDeadAll();
            if (iceBalls) iceBalls->makeActorDeadAll();
            if (tankBullets) tankBullets->makeActorDeadAll();
            if (isKart) isKart->makeActorDead();
        }
    };

    inline void executeMovement(PlayerActorHakoniwa* thisPtr) {
        #ifdef ALLOW_POWERUPS
            auto* anim   = thisPtr->mAnimator;
            auto* holder = thisPtr->mModelHolder;
            auto* model  = holder->findModelActor("Normal");
            auto* hand = al::tryGetSubActor(model, "右手");
            auto* cape = al::tryGetSubActor(model, "ケープ");
            auto* tail = al::tryGetSubActor(model, "尻尾");
            auto* blaster = al::tryGetSubActor(model, "Blaster");

            isCapeOn = cape && al::isAlive(cape);
            isBlasterOn = blaster && al::isAlive(blaster);

            bool isMove = thisPtr->mInput->isMove();
            bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
            bool isWater = al::isInWater(thisPtr);
            bool isSurface = thisPtr->mWaterSurfaceFinder->isFoundSurface();
            bool isVisible = !al::isHideModel(model);
            bool isHack = thisPtr->mHackKeeper && thisPtr->mHackKeeper->mHackActor;

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

            // Handle logic for Drill Suit
            if (!(al::isNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop))
                && rs::isCollidedWall(thisPtr->mCollider))) WallStick::update(thisPtr);

            // Add attack to drill
            if (isDrill) {
                static bool wasDrillAttack = false;
                bool isDrillAttack = isDrillAnim(anim);
                updateAttackSensor(thisPtr, "GalaxySpin", isDrillAttack, wasDrillAttack);

                if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaJump))
                    && !al::isNerve(thisPtr, getNerveAt(nrvHakoniwaFall))) isPopDrill = false;
            }

            // Handle blaster spawning
            static int holdRightFrames = 0;
            if (al::isPadHoldRight(-1)) holdRightFrames++;
            else holdRightFrames = 0;

            if (isMario && blaster
                && holdRightFrames == 30
                && !thisPtr->mInput->isMove()
                && !rs::isActiveDemo(thisPtr)
            ) {
                if (isBlasterOn
                ) {
                    al::tryEmitEffect(model, "BlasterDisappear", nullptr);
                    al::tryStartSe(thisPtr, "BlasterOpen");
                    blaster->kill();
                } else {
                    blaster->appear();
                    al::tryEmitEffect(model, "BlasterAppear", nullptr);
                    al::tryStartSe(thisPtr, "BlasterOpen");
                }
            }

            if (isBlasterOn && hand && !al::isActionPlayingSubActor(model, "右手", "AreaWaitDance03"))
                al::startActionSubActor(model, "右手", "AreaWaitDance03");
                
            // Handle fireball/iceball/blaster attack
            const char* jointName;
            const char* fireAnim;
            al::LiveActorGroup* currentPool;

            if (isBlasterOn) {
                jointName = "HandR";
                fireAnim = "BlastShoot";
                currentPool = tankBullets;
            } else {
                jointName = nextThrowLeft ? "HandL" : "HandR";
                fireAnim = nextThrowLeft ? "FireL" : "FireR";
                currentPool = isIce ? iceBalls : fireBalls;
            }

            if (!currentPool) return;
            auto* projectile = currentPool->getDeadActor();

            bool isFullBody = (!isMove && onGround && (!isWater || isSurface));
            bool isFloating = al::isActionPlaying(model, "GlideFloat")
                || al::isActionPlaying(model, "GlideFloatSuper");

            if (isBlasterOn || isMario || isFire || isIce || isBrawl || isSuper
            ) {
                if (fireStep < 0
                    && (canAction || isFloating)
                    && al::isPadTriggerR(-1)
                ) {
                    if (projectile && al::isDead(projectile)
                    ) {
                        fireStep = 0;
                        canAction = false;

                        // Increase Eye sensor range for blaster homing
                        if (isBlasterOn) al::setSensorRadius(thisPtr, "Eye", 1600.0f);

                        anim->startUpperBodyAnim(fireAnim);
                        if (isFullBody) anim->startAnim(fireAnim);
                        if (isBlasterOn) al::tryStartSe(thisPtr, "BlasterShoot");
                    }
                }
                if (fireStep >= 0
                ) {
                    bool isShooting = anim->isUpperBodyAnim("FireL") || anim->isUpperBodyAnim("FireR") || anim->isUpperBodyAnim("BlastShoot")
                        || anim->isAnim("FireL") || anim->isAnim("FireR") || anim->isAnim("BlastShoot");

                    if (!isShooting) {
                        fireStep = -1;
                        al::setSensorRadius(thisPtr, "Eye", 800.0f); // Restore default
                        return;
                    }
                    if ((fireStep == 2 && !isBlasterOn) || (fireStep == 40 && isBlasterOn)
                    ) {
                        #ifdef ALLOW_HOMING
                            // Home in on nearest target
                            isNearTarget = findNearestTarget(thisPtr, isBlasterOn ? 1600.0f : 800.0f);
                            if (isNearTarget) {
                                sead::Vector3f dir = al::getTrans(isNearTarget) - al::getTrans(thisPtr);
                                dir.normalize();
                                sead::Vector3f fwd;
                                al::calcQuatFront(&fwd, model);

                                if (fwd.dot(dir) > 0.85f) al::faceToDirection(model, al::getTrans(isNearTarget) - al::getTrans(thisPtr));
                            }
                        #endif
                        
                        hitBufferCount = 0;

                        sead::Vector3f startPos;
                        al::calcJointPos(&startPos, model, jointName);

                        if (isBlasterOn) {
                            sead::Vector3f fwd;
                            al::calcQuatFront(&fwd, model);
                            fwd.normalize();

                            ((TankBullet*)projectile)->shoot(startPos, fwd * 85.0f, 200, false, false);
                            al::tryEmitEffect(model, "Shoot", nullptr);
                            al::tryStartSe(projectile, "Shoot");
                        } else {
                            sead::Vector3f offset(0.0f, 0.0f, 0.0f);

                            if (isSuper) ((FireBrosFireBall*)projectile)->shoot(startPos, al::getQuat(model), offset, true, 0, true);
                            else ((FireBrosFireBall*)projectile)->shoot(startPos, al::getQuat(model), offset, true, 0, false);

                            if (isIce) al::tryStartSe(projectile, "IceBallShoot");
                            else al::tryStartSe(projectile, "FireBallShoot");
                        }

                        if (!isBlasterOn) nextThrowLeft = !nextThrowLeft;
                    }
                    if (anim->isUpperBodyAnimEnd()
                    ) {
                        if (isFullBody) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                        anim->clearUpperBodyAnim();
                        fireStep = -1;
                        al::setSensorRadius(thisPtr, "Eye", 800.0f); // Restore default
                    }
                    else fireStep++;
                }
            }
            canAction = false;

            // Handle cape logic for Mario/Brawl suit
            bool isGliding =
                al::isActionPlaying(model, "Glide")
                || al::isActionPlaying(model, "GlideAlt")
                || al::isActionPlaying(model, "GlideFloatStart")
                || al::isActionPlaying(model, "JumpBroad8")
                || al::isActionPlaying(model, "JumpBroad8Alt")
                || isFloating;

            /*if (isGliding && isHakoniwa->mJointControlKeeper) {
                char* dyn = *(char**)((char*)isHakoniwa->mJointControlKeeper + 0x30);
                if (dyn) {
                    float* noseVel = (float*)(*(char**)(dyn + 0x20) + 0x48);
                    noseVel[0] *= 0.0f;
                    noseVel[1] *= 0.0f;
                }
            }*/

            // Handle glide gauge
            if (isGauge && !isSuper
            ) {
                static bool wasInAir = false;
                static bool wasStartup = false;
                static bool hadStartup = false;
                bool inAir = !onGround && !isWater;
                
                bool isStartup = al::isActionPlaying(model, "JumpBroad8") 
                              || al::isActionPlaying(model, "JumpBroad8Alt");

                // Landing
                if (wasInAir && !inAir && isGauge->isAlive()) {
                    isGauge->refill();
                    isGauge->endMax();
                    hadStartup = false;
                    wasStartup = false;
                }

                // Mark first startup as consumed on its falling edge
                if (!isStartup && wasStartup) hadStartup = true;
                wasStartup = isStartup;

                // Gliding
                if (isGliding && isGauge->canUse()) {
                    isGauge->start();
                    isGauge->drain();

                    if (isStartup && hadStartup) isGauge->setRate(isGauge->getRate() - 0.004f);
                    if (isGauge->isEmpty()) isGauge->startTimer();
                }

                // Penalty
                if (isGauge->tickTimer()) {
                    if (isGliding) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                }

                wasInAir = inAir;
            }

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

            // Handle logic for Metal suit
            if (isMetal) {
                if (thisPtr->mInfo->mIsMoon) applyMetalMarioMoonConst(thisPtr->mConst);
                else applyMetalMarioConst(thisPtr->mConst);

                if (thisPtr->mJointControlKeeper) thisPtr->mJointControlKeeper->resetPartsDynamics(); // Stop physics for nose/mustache

                auto* wsf = thisPtr->mWaterSurfaceFinder;
                bool nearSurface = wsf && wsf->isFoundSurface() && wsf->getDistance() <= 80.0f;
                bool submerged = isWater && !nearSurface;

                if (submerged) {
                    if (onGround) al::limitVelocityH(thisPtr, 8.5f); // Running: hard cap, don't gradually decrease
                    else {
                        al::scaleVelocityHV(thisPtr, 0.95f, 1.0f); // Jumping: gradually lose horizontal speed
                        f32 velY = al::getVelocity(thisPtr).y;
                        if (velY < 0.0f) al::scaleVelocityY(thisPtr, 0.85f); // Floaty descent: drag on downward velocity only
                    }
                }
            }

            // Handle logic for Flying suit
            if (isFly) {
                if (!isHack && isVisible) {
                    if (isGliding) {
                        al::tryDeleteEffect(model, "FlyingState");
                        al::tryEmitEffect(model, "FlyingL", nullptr);
                        al::tryEmitEffect(model, "FlyingR", nullptr);
                    } else {
                        al::tryDeleteEffect(model, "FlyingL");
                        al::tryDeleteEffect(model, "FlyingR");
                        al::tryEmitEffect(model, "FlyingState", nullptr);
                    }
                } else {
                    al::tryDeleteEffect(model, "FlyingState");
                    al::tryDeleteEffect(model, "FlyingL");
                    al::tryDeleteEffect(model, "FlyingR");
                }
            }

            // Handle logic for Super suit
            if (isSuper) {
                applyMoonMarioConst(thisPtr->mConst); // force Moon physics

                // Add attack to Super moves
                static bool wasMoveSuper = false;
                bool isMoveSuper = speedH >= dashBorder || anim->isAnim("JumpBroad8") || anim->isAnim("Glide");
                updateAttackSensor(thisPtr, "GalaxySpin", isMoveSuper, wasMoveSuper);

                // Apply effects for DashFastSuper
                bool isDash = al::isPadHoldR(-1) && !isActionBusy()
                    && al::isActionPlaying(model, "MoveSuper") && speedH >= dashBorder;
                bool isGlide = al::isActionPlaying(model, "Glide") && !isActionBusy();

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

            bool isWait = al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait)) && isVisible;
            bool canHeal = (isMario || isNoCap) && isWait && !GameDataFunction::isPlayerHitPointMax(thisPtr);

            if (canHeal) {
                if (stillFrames < 120) stillFrames++;

                int interval = (stillFrames >= 120) ? 60 : 600;
                if (++healFrames >= interval) { GameDataFunction::recoveryPlayer(thisPtr); healFrames = 0; }
            }
            else { stillFrames = 0; healFrames = 0; }

            #ifdef ALLOW_DASH // Handles dash animations and effects

                bool isMoving = al::isActionPlaying(model, "Move")
                    || al::isActionPlaying(model, "MoveClassic")
                    || al::isActionPlaying(model, "MoveBrawl")
                    || al::isActionPlaying(model, "MoveSuper");

                static bool wasDash = false;
                bool isDashNow = al::isPadHoldR(-1)
                    && isMoving && !isActionBusy() && speedH >= dashBorder;

                if (isDashNow && !wasDash
                ) {
                    const char* fx = isSuper ? "AccelSecond" : "Accel";
                    if (!al::isEffectEmitting(model, fx)) { al::tryStartSe(thisPtr, fx); al::tryEmitEffect(model, fx, nullptr); }
                }
                wasDash = isDashNow;
            #endif

            // Handle kart spawning
            static int holdLeftFrames = 0;
            if (al::isPadHoldLeft(-1)) holdLeftFrames++;
            else holdLeftFrames = 0;

            if (isKart && holdLeftFrames == 30
                && !thisPtr->mInput->isMove() && !rs::isActiveDemo(thisPtr)
            ) {
                if (al::isAlive(isKart)
                ) {
                    if (rs::isPlayerBinding(thisPtr)) return;

                    al::tryEmitEffect(isKart, "Disappear", nullptr);
                    al::tryStartSe(isKart, "CommonVanishS");
                    isKart->kill();
                    return;
                } else {
                    sead::Vector3f front;
                    al::calcFrontDir(&front, thisPtr);
                    sead::Vector3f gravity = al::getGravity(thisPtr);
                    sead::Vector3f marioPos = al::getTrans(thisPtr);
                    sead::Vector3f target = marioPos + front * 500.0f;

                    sead::Vector3f groundPos;
                    bool hasGround = alCollisionUtil::getHitPosOnArrow(thisPtr, &groundPos, target - gravity * 1000.0f, gravity * 2000.0f, nullptr, nullptr);

                    if (!hasGround) { al::tryStartSe(thisPtr, "InvalidCapAction"); return; }
                    target = groundPos - gravity;

                    al::setTrans(isKart, target);
                    isKart->appear();
                    al::tryEmitEffect(isKart, "Appear", nullptr);
                    al::tryStartSe(isKart, "Appear");
                }
            }
        #endif
    }

    struct LiveActorMovementHook : public mallow::hook::Trampoline<LiveActorMovementHook> {
        static void Callback(al::LiveActor* actor) {
            // Check if this actor is frozen
            if (PlayerFreeze::updateFrozenActor(actor)) return; // Skip normal movement
            
            Orig(actor);

            static bool hammerHit = false;
            
            if (actor != isHammer) return;
            if (!al::isAlive(isHammer)) { hammerHit = false; return; }

            al::HitSensor* sensorHammer = al::getHitSensor(isHammer, "AttackHack");
            if (!sensorHammer || !sensorHammer->mIsValid) return;

            if (auto* sensorWall = al::tryGetCollidedWallSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorWall);
            if (auto* sensorCeiling = al::tryGetCollidedCeilingSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorCeiling);
            if (auto* sensorGround = al::tryGetCollidedGroundSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensorGround);

            if (!hammerHit && isHakoniwa->mAnimator->isAnim("HammerAttack")
                && isHakoniwa->mAnimator->getAnimFrame() >= 8.0f
                && al::isCollidedGround(isHammer)
            ) {
                al::tryEmitEffect(isHakoniwa, "HammerLandHit", nullptr);
                al::tryStartSe(isHammer, "HammerLand");
                al::tryStartSe(isHammer, "HammerHit");
                hammerHit = true;
            }
        }
    };

    struct CalcAnimHook : public mallow::hook::Trampoline<CalcAnimHook> {
        static void Callback(al::LiveActor* actor) {
            float savedLean = 0.0f;
            bool isKartAnim = typeid(*actor) == typeid(Motorcycle) && al::isAlive(actor);

            if (isKartAnim) {
                float* lean = reinterpret_cast<float*>((char*)actor + 312);
                savedLean = *lean;
                *lean = 0.0f;
            }

            Orig(actor);

            if (isKartAnim) *reinterpret_cast<float*>((char*)actor + 312) = savedLean;
            if (hammerParentModel && actor == hammerParentModel) updateHammerMtx();
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

            static bool blockGlide = false;

            if (al::isFirstStep(thisPtr)) blockGlide = (isGauge && isGauge->isEmpty());
            if (blockGlide) return;

            auto* anim   = thisPtr->mAnimator;
            auto* model = thisPtr->mModelHolder->findModelActor("Normal");
            auto* cape = al::tryGetSubActor(model, "ケープ");
            auto* keeper = static_cast<al::IUseEffectKeeper*>(model);

            if (!isMario && !isFeather && !isTanooki && !isFly && !isBrawl && !isSuper) return;

            float vy = al::getVelocity(thisPtr).y;
            if (vy < -2.5f) al::setVelocityY(thisPtr, -2.5f);

            float speed = al::calcSpeed(thisPtr);

            if (anim->isAnim("Glide")
            ) {
                sead::Vector3f camSide, marioSide;
                al::calcCameraSideDir(&camSide, thisPtr, 0);
                al::calcSideDir(&marioSide, thisPtr);

                float localLean = camSide.dot(marioSide) * al::getLeftStick(-1).x;
                glideLean = al::lerpValue(glideLean, localLean * -50.0f, 0.025f);
                glidePitch = al::lerpValue(glidePitch, fabsf(localLean) * -25.0f, 0.025f);
            } else {
                glideLean = al::lerpValue(glideLean, 0.0f, 0.2f);
                glidePitch = al::lerpValue(glidePitch, 0.0f, 0.2f);
            }

            if (al::isFirstStep(thisPtr)
            ) {
                if ((isMario || isBrawl) 
                    && cape && al::isDead(cape)
                ) {
                    cape->appear();
                    al::tryEmitEffect(keeper, "AppearBloom", nullptr);
                    al::tryStartSe(thisPtr, "Bloom");
                }
                anim->startAnim("JumpBroad8");
            }
            else if (anim->isAnimEnd() && anim->isAnim("JumpBroad8")) anim->startAnim("Glide");
            else if (speed < 10.f) {
                if (anim->isAnim("Glide")) anim->startAnim("GlideFloatStart");
                if (anim->isAnimEnd() && anim->isAnim("GlideFloatStart")) anim->startAnim("GlideFloat");
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
                        spin.resetForNewSpin();

                        spin.trigger = true;
                        al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                    }
                }
                else if (al::isPadTriggerX(-1) || al::isPadTriggerY(-1)
                ) {
                    if (!al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))
                    ) {
                        if (!thisPtr->mHackCap || !thisPtr->mHackCap->isEnableThrow()) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                        else {
                            spin.resetForNewSpin();

                            spin.trigger = false;
                            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
                        }
                    }
                }
            }
        }
    };

    struct PlayerHeadSlidingKill : public mallow::hook::Trampoline<PlayerHeadSlidingKill> {
        static void Callback(PlayerStateHeadSliding * state) {
            glideLean = 0.0f;
            glidePitch = 0.0f;
            isCapeActive = 1200;

            if (state->mAnimator) state->mAnimator->clearUpperBodyAnim();
            Orig(state);
        }
    };

    struct PlayerConstGetHeadSlidingSpeed : public mallow::hook::Trampoline<PlayerConstGetHeadSlidingSpeed> {
        static float Callback(const PlayerConst* thisPtr) {
            float speed = Orig(thisPtr);

            if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mHackActor) return speed;
            if (isSuper) speed *= 1.5f;
            return speed;
        }
    };

    struct PlayerInputFunctionIsTriggerJump : public mallow::hook::Trampoline<PlayerInputFunctionIsTriggerJump> {
        static bool Callback(const al::LiveActor* actor, s32 port) {
            if (isDrillAnim(isHakoniwa ? isHakoniwa->mAnimator : nullptr)) return false;

            return Orig(actor, port);
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

            if (isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mHackActor) return update;
            PlayerConst* playerConst = const_cast<PlayerConst*>(thisPtr->mConst);

            bool isDash = al::isPadHoldR(-1) && !isActionBusy();

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

    struct PlayerAnimatorSetAnimRateCommon : public mallow::hook::Trampoline<PlayerAnimatorSetAnimRateCommon> {
        static void Callback(PlayerAnimator* thisPtr, float rate) {
            if (isMetal && isHakoniwa
                && thisPtr == isHakoniwa->mAnimator
            ) {
                auto* wsf = isHakoniwa->mWaterSurfaceFinder;
                bool nearSurface = wsf && wsf->isFoundSurface() && wsf->getDistance() <= 80.0f;
                if (al::isInWater(isHakoniwa) && !nearSurface
                ) {
                    rate *= 0.50f;
                    thisPtr->mAnimFrameCtrl->mRate = rate;
                }
            }
            Orig(thisPtr, rate);
        }
    };

    struct ActorActionKeeperUpdatePostHook : public mallow::hook::Trampoline<ActorActionKeeperUpdatePostHook> {
        static void Callback(al::ActorActionKeeper* thisPtr) {
            if (isMetal) ::al::stopAllSeFromUser(isHakoniwa, 0, "Mouth");
            Orig(thisPtr);
        }
    };

    struct TryUpdateSeMaterialCodeHook : public mallow::hook::Trampoline<TryUpdateSeMaterialCodeHook> {
        static void Callback(al::IUseAudioKeeper* keeper, const char* material) {
            if (isMetal) return Orig(keeper, "Metal");
            Orig(keeper, material);
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
            if (isSuperRunningOnSurface) ctx->W[8] = 1;
        }
    };

    struct WaterSurfaceRunDisableSlowdown : public mallow::hook::Inline<WaterSurfaceRunDisableSlowdown> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            static sead::Vector3f garbageVec;
            if (isSuperRunningOnSurface) ctx->X[0] = reinterpret_cast<u64>(&garbageVec);
        }
    };

    struct RsIsTouchDamageCode : public mallow::hook::Trampoline<RsIsTouchDamageCode> {
        static bool Callback(const al::LiveActor* actor, const IUsePlayerCollision* coll, const IPlayerModelChanger* changer) {
            if (isMetal || isSuper) return false;
            return Orig(actor, coll, changer);
        }
    };

    struct RsIsTouchDamageFireCode : public mallow::hook::Trampoline<RsIsTouchDamageFireCode> {
        static bool Callback(const al::LiveActor* actor, const IUsePlayerCollision* coll, const IPlayerModelChanger* changer) {
            if (isSuper) return false;
            return Orig(actor, coll, changer);
        }
    };

    struct RsIsTouchDeadCode : public mallow::hook::Trampoline<RsIsTouchDeadCode> {
        static bool Callback(const al::LiveActor* actor, const IUsePlayerCollision* coll, const IPlayerModelChanger* changer, const IUseDimension* dim, float f) {
            if (isSuper) return false;
            return Orig(actor, coll, changer, dim, f);
        }
    };

    struct JudgeInWater : public mallow::hook::Trampoline<JudgeInWater> {
        static bool Callback(const PlayerJudgeInWater* thisPtr) {
            if (isMetal) return false;
            return Orig(thisPtr);
        }
    };
    
    struct ReduceOxygen : public mallow::hook::Trampoline<ReduceOxygen> {
        static void Callback(void* thisPtr) {
            if (isSuper) return;
            Orig(thisPtr);
        }
    };

    // Prevent crash when Motorcycle enters water (null OceanWave in fluid system)
    struct CalcFindWaterSurfaceFlatFix : public mallow::hook::Trampoline<CalcFindWaterSurfaceFlatFix> {
        static bool Callback(sead::Vector3f* outPos, sead::Vector3f* outNormal, const al::LiveActor* actor,
            const sead::Vector3f& pos, const sead::Vector3f& up, float range) {
            if (actor == isKart) return false;
            return Orig(outPos, outNormal, actor, pos, up, range);
        }
    };

    inline void Install() {
        #ifdef ALLOW_POWERUPS
            FireBrosFireBallInitArchive::InstallAtOffset(0x10082C);
            InitActorSuffixHook::InstallAtSymbol("_ZN2al15initActorSuffixEPNS_9LiveActorERKNS_13ActorInitInfoEPKc");
            InitActorArchiveHook::InstallAtSymbol("_ZN2al24initActorWithArchiveNameEPNS_9LiveActorERKNS_13ActorInitInfoERKN4sead14SafeStringBaseIcEEPKc");
            PlayerActorHakoniwaInitAfterPlacement::InstallAtSymbol("_ZN19PlayerActorHakoniwa18initAfterPlacementEv");

            // Handles control/movement
            LiveActorMovementHook::InstallAtSymbol("_ZN2al9LiveActor8movementEv");
            CalcAnimHook::InstallAtSymbol("_ZN2al9LiveActor8calcAnimEv");

            // Handles Hammer while Carrying
            PlayerCarryKeeperStartCarry::InstallAtSymbol("_ZN17PlayerCarryKeeper10startCarryEPN2al9HitSensorE");

            // Handles Double Jump
            PlayerActorHakoniwaExeJump::InstallAtSymbol("_ZN19PlayerActorHakoniwa7exeJumpEv");
            PlayerStateJumpTryCountUp::InstallAtSymbol("_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump");

            // Handles Glide
            PlayerActorHakoniwaExeHeadSliding::InstallAtSymbol("_ZN19PlayerActorHakoniwa14exeHeadSlidingEv");
            PlayerHeadSlidingKill::InstallAtSymbol("_ZN22PlayerStateHeadSliding4killEv");
            PlayerConstGetHeadSlidingSpeed::InstallAtSymbol("_ZNK11PlayerConst19getHeadSlidingSpeedEv");

            PlayerInputFunctionIsTriggerJump::InstallAtSymbol("_ZN19PlayerInputFunction13isTriggerJumpEPKN2al9LiveActorEi");

            #ifdef ALLOW_DASH // Handles Dash
                PlayerInputFunctionIsHoldAction::InstallAtSymbol("_ZN19PlayerInputFunction12isHoldActionEPKN2al9LiveActorEi");
                PlayerActionGroundMoveControlUpdate::InstallAtSymbol("_ZN29PlayerActionGroundMoveControl6updateEv");

                // Handles running on water
                StartWaterSurfaceRunJudge::InstallAtSymbol("_ZNK31PlayerJudgeStartWaterSurfaceRun5judgeEv");
                WaterSurfaceRunJudge::InstallAtSymbol("_ZNK26PlayerJudgeWaterSurfaceRun5judgeEv");
                RunWaterSurfaceDisableSink::InstallAtOffset(0x48023C);
                WaterSurfaceRunDisableSlowdown::InstallAtOffset(0x4184C0);
                RsIsTouchDamageCode::InstallAtSymbol("_ZN2rs17isTouchDamageCodeEPKN2al9LiveActorEPK19IUsePlayerCollision");
                RsIsTouchDamageFireCode::InstallAtSymbol("_ZN2rs21isTouchDamageFireCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChanger");
                RsIsTouchDeadCode::InstallAtSymbol("_ZN2rs15isTouchDeadCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChangerPK13IUseDimensionf");
            #endif

            // Handle Metal Mario setup
            PlayerAnimatorSetAnimRateCommon::InstallAtSymbol("_ZN14PlayerAnimator17setAnimRateCommonEf");
            ActorActionKeeperUpdatePostHook::InstallAtSymbol("_ZN2al17ActorActionKeeper10updatePostEv");
            TryUpdateSeMaterialCodeHook::InstallAtSymbol("_ZN2al23tryUpdateSeMaterialCodeEPNS_15IUseAudioKeeperEPKc");

            // Handles Metal Mario walking in water
            JudgeInWater::InstallAtSymbol("_ZNK18PlayerJudgeInWater5judgeEv");

            // Handles Super Mario breathing in water
            ReduceOxygen ::InstallAtSymbol("_ZN12PlayerOxygen6reduceEv");

            // Prevent crash with water surface calculations
            CalcFindWaterSurfaceFlatFix::InstallAtSymbol("_ZN2al24calcFindWaterSurfaceFlatEPN4sead7Vector3IfEES3_PKNS_9LiveActorERKS2_S8_f");

            // Patch PlayerJointControlKeeper capacity from 7 to 9
            exl::patch::CodePatcher jointCapPatcher(0x454F20);
            jointCapPatcher.WriteInst(0x52800121); // MOV W1, #9

            // Disable invincibility music patches
            exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
            invincibleStartPatcher.WriteInst(0x1F2003D5); // NOP
            exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
            invinciblePatcher.WriteInst(0x1F2003D5); // NOP

            // Install Wall Stick hooks
            WallStick::Install();
        #endif
    }
}