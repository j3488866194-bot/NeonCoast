#pragma once
#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "NCCar.generated.h"
class USpringArmComponent;
class UCameraComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
class USpotLightComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UChaosWheeledVehicleMovementComponent;
class ANCHero;
UCLASS()
class NEONCOAST_API ANCCar : public AWheeledVehiclePawn
{
    GENERATED_BODY()
    friend class ANCValidationRunner;
public:
    ANCCar();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
    float GetSpeedKmh() const;
    float GetCondition() const { return Condition; }
    float GetDriverHealth() const;
    ANCHero* GetDriverHero() const { return Driver; }
    bool WasPlayerDriven() const { return bHasBeenPlayerDriven; }
    bool CanEnter() const;
    void SetDriver(ANCHero* Hero);
    void ExitCar();
    void Recover();
    void Repair();
    void ResetHeldInput();
    void TryService();
    void SetRoute(const TArray<FVector>& Points);
    void SetDrivingInput(float Throttle, float Steering, bool Brake);
    bool IsPoliceVehicle() const { return bPolice; }
    void SetPolice(bool bEnabled);
    void SetPursuitTarget(AActor* Target);
    void SetPursuitLocation(FVector Location);
    void ToggleHeadlights();
    bool AreHeadlightsOn() const { return bHeadlights; }
    FLinearColor Paint = FLinearColor(.85,.22,.12);
protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UChaosWheeledVehicleMovementComponent> Vehicle;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Glass;
    UPROPERTY() TObjectPtr<ANCHero> Driver;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
    UPROPERTY() TArray<TObjectPtr<USpotLightComponent>> Headlights;
    UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> PoliceLights;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PaintMaterials;
    TWeakObjectPtr<AActor> PursuitTarget;
    TArray<FVector> Route;
    TArray<FVector> PursuitRoute;
    FVector PursuitLocation=FVector::ZeroVector;
    FVector PlannedPursuitLocation=FVector::ZeroVector;
    FVector PreviousVelocity=FVector::ZeroVector;
    FTransform LastSafeTransform;
    int32 RouteIndex=0, PursuitRouteIndex=0;
    float Speed=0, Steer=0, ForwardInput=0, PadThrottle=0, PadReverse=0;
    float Condition=100, HitCooldown=0, LookIdle=0, StuckTime=0;
    float ReverseTime=0, UnstickCooldown=0, RoutePlanTime=0, SafeTime=0;
    float TrafficWaitTime=0, AvoidanceTime=0;
    FVector AvoidanceTarget=FVector::ZeroVector;
    bool bBrake=false, bBoost=false, bPolice=false, bHeadlights=true;
    bool bExternalDrivingInput=false, bHasPursuitLocation=false, bLastBoost=false;
    bool bHasBeenPlayerDriven=false;
    void ApplyControls(float Drive, float Turn, float Brake, bool Handbrake);
    void ComputeTrafficControls(float Dt, float& Drive, float& Turn, float& Brake);
    void BuildPursuitRoute();
    bool FindExit(FVector& Out) const;
    void ClearDrivingInput();
    void UpdateLights();
    void UpdatePaint();
    UFUNCTION()
    void OnVehicleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
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
