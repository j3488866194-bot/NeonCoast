#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCValidationRunner.generated.h"
class ANCCar;
class ANCHero;
class ANCPedestrian;
class APlayerController;
class UNCGameplaySubsystem;

// Spawned only by the explicit -NeonCoastSmokeTest development command line.
UCLASS(NotBlueprintable, Transient)
class ANCValidationRunner : public AActor
{
    GENERATED_BODY()
public:
    ANCValidationRunner();
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<ANCCar> Car;
    UPROPERTY() TObjectPtr<ANCHero> Hero;
    UPROPERTY() TObjectPtr<ANCPedestrian> Target;
    UPROPERTY() TObjectPtr<AActor> MuzzleWall;
    TMap<TWeakObjectPtr<ANCPedestrian>,FVector> PedestrianStart;
    float Clock=0, PhaseClock=0, LastZ=0, MaxVerticalStep=0;
    float MinDriveZ=MAX_flt, MaxDriveZ=-MAX_flt, PeakSpeed=0, DriveDistance=0;
    float MinIdleZ=MAX_flt, MaxIdleZ=-MAX_flt, MaxVerticalRate=0;
    float JumpStartZ=0, MaxJumpZ=0, TargetHealthBefore=0, HealthBeforeExit=0;
    float PeakBailoutSpeed=0;
    float SearchStartedAt=0, SearchDuration=0, RaceStartedAt=0;
    bool bCheckedSearchDelay=false;
    int32 AmmoBefore=0, ReserveBefore=0, CashBefore=0;
    int32 DeliveriesBefore=0, RaceWinsBefore=0, ExpectedGate=0, TokenIndex=0, TokensBefore=0;
    int32 Phase=0, Failures=0, Checks=0;
    FVector ExitPosition=FVector::ZeroVector;
    FVector DriveStart=FVector::ZeroVector;
    void Check(bool Passed, const TCHAR* Name);
    void Advance(int32 NextPhase);
    bool SetUpCombat(APlayerController* PC);
    void AimAtTarget(APlayerController* PC);
    bool SetUpMuzzleWall(APlayerController* PC);
    void PlaceCar(const FVector& RoadPoint);
    void CheckEarnedProgressSerialization(UNCGameplaySubsystem* Gameplay);
    void Finish();
};
