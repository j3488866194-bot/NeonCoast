#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "NCCar.generated.h"
class UBoxComponent;
class USpringArmComponent;
class UCameraComponent;
class ANCHero;
UCLASS()
class NEONCOAST_API ANCCar : public APawn
{
    GENERATED_BODY()
    friend class ANCValidationRunner;
public:
    ANCCar();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    float GetSpeedKmh() const { return FMath::Abs(Speed)*.036f; }
    float GetCondition() const { return Condition; }
    float GetDriverHealth() const;
    bool CanEnter() const;
    void SetDriver(ANCHero* Hero);
    void ExitCar();
    void Recover();
    void Repair();
    void SetRoute(const TArray<FVector>& Points);
    FLinearColor Paint = FLinearColor(.85,.22,.12);
protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Chassis;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<ANCHero> Driver;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Wheels;
    TArray<FVector> Route;
    int32 RouteIndex=0;
    float Speed=0, Steer=0, SmoothedSteer=0, ForwardInput=0, PadThrottle=0, PadReverse=0;
    float Condition=100, HitCooldown=0, LookIdle=0, StuckTime=0;
    bool bBrake=false, bBoost=false;
    void SimulateStep(float Dt);
    bool FindExit(FVector& Out) const;
    void Forward(float V) { ForwardInput=V; }
    void Right(float V) { Steer=V; }
    void Throttle(float V) { PadThrottle=V; }
    void Reverse(float V) { PadReverse=V; }
    void BrakeOn() { bBrake=true; }
    void BrakeOff() { bBrake=false; }
    void BoostOn() { bBoost=true; }
    void BoostOff() { bBoost=false; }
    void LookYaw(float V);
    void LookPitch(float V);
    void PadYaw(float V);
    void PadPitch(float V);
};
