#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCValidationRunner.generated.h"
class ANCCar;
class ANCHero;
class ANCPedestrian;

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
    TMap<TWeakObjectPtr<ANCPedestrian>,FVector> PedestrianStart;
    float Clock=0, LastZ=0, MaxVerticalStep=0;
    int32 Phase=0, Failures=0, Checks=0;
    FVector ExitPosition=FVector::ZeroVector;
    void Check(bool Passed, const TCHAR* Name);
    void CheckFrameRates();
    void Finish();
};
