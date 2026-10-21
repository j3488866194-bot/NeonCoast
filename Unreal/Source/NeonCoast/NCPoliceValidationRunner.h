#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCPoliceValidationRunner.generated.h"

class ANCCar;
class ANCHero;
class UNCGameplaySubsystem;

/** Explicit development-only police integration fixture; normal director and world ticks run. */
UCLASS(NotBlueprintable, Transient)
class ANCPoliceValidationRunner : public AActor
{
    GENERATED_BODY()
public:
    ANCPoliceValidationRunner();
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY() TObjectPtr<ANCHero> Hero;
    UPROPERTY() TObjectPtr<ANCCar> ObservedUnit;
    UPROPERTY() TObjectPtr<ANCCar> Wreck;
    UPROPERTY() TObjectPtr<AActor> Hideout;
    TSet<TWeakObjectPtr<ANCCar>> BeforeWreckUnits;
    TMap<TWeakObjectPtr<ANCCar>, FVector> InitialUnitPositions;
    FVector SightPoint = FVector::ZeroVector;
    FVector LastSeenBeforeCover = FVector::ZeroVector;
    float Clock = 0.f;
    float PhaseClock = 0.f;
    float NextCrimeAt = 0.f;
    float SearchStartAt = 0.f;
    float SearchSeconds = 0.f;
    float MaxNaturalTravel = 0.f;
    int32 Phase = 0;
    int32 Checks = 0;
    int32 Failures = 0;
    int32 HealthyBeforeWreck = 0;
    bool bCheckedSearchDelay = false;
    bool bSearchWasSeen = false;

    void Check(bool Passed, const TCHAR* Name);
    void Advance(int32 NextPhase);
    TArray<ANCCar*> HealthyPolice() const;
    void PlaceUnit(ANCCar* Unit, FVector Location);
    void PlacePlayer(FVector Location);
    bool BuildHideout();
    bool SightRayHitsHideout(ANCCar* Unit) const;
    void Finish();
};
