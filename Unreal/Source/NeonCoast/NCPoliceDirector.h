#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCPoliceDirector.generated.h"
class ANCCar;

UCLASS(NotBlueprintable)
class ANCPoliceDirector : public AActor
{
    GENERATED_BODY()
public:
    ANCPoliceDirector();
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TArray<TObjectPtr<ANCCar>> Units;
    UPROPERTY() TArray<TObjectPtr<ANCCar>> RetiringUnits;
    TMap<TWeakObjectPtr<ANCCar>,float> RetiredAt;
    FVector LastSeen=FVector::ZeroVector;
    float SpawnClock=0, AttackClock=0, CalmClock=0;
    int32 SpawnSerial=0;
    ANCCar* SpawnUnit(APawn* Target,const FVector& DispatchLocation);
    void RetireUnit(ANCCar* Unit);
    void CleanRetiredUnits(APawn* Player);
};
