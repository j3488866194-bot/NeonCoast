#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "NCSaveGame.generated.h"

/** Only durable player progress belongs here; a restarted race never resumes mid-lap. */
UCLASS()
class NEONCOAST_API UNCSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 Version = 1;
    UPROPERTY(SaveGame) int32 Cash = 250;
    UPROPERTY(SaveGame) int32 CollectedMask = 0;
    UPROPERTY(SaveGame) int32 CompletedDeliveries = 0;
    UPROPERTY(SaveGame) int32 RaceWins = 0;
    UPROPERTY(SaveGame) float BestRaceSeconds = 0.f;
    UPROPERTY(SaveGame) int32 MissionStage = 0;
    UPROPERTY(SaveGame) float WantedHeat = 0.f;
    UPROPERTY(SaveGame) FVector LastCrimeLocation = FVector::ZeroVector;

    bool IsSane() const
    {
        return Version == 1 && Cash >= 0 && Cash <= 100000000
            && (CollectedMask & ~255) == 0 && CollectedMask >= 0
            && CompletedDeliveries >= 0 && CompletedDeliveries <= 1000000
            && RaceWins >= 0 && RaceWins <= 1000000
            && FMath::IsFinite(BestRaceSeconds) && BestRaceSeconds >= 0.f && BestRaceSeconds <= 95.f
            && MissionStage >= 0 && MissionStage <= 3
            && FMath::IsFinite(WantedHeat) && WantedHeat >= 0.f && WantedHeat <= 3.f
            && !LastCrimeLocation.ContainsNaN()
            && FMath::Abs(LastCrimeLocation.X) < 1000000.f
            && FMath::Abs(LastCrimeLocation.Y) < 1000000.f
            && FMath::Abs(LastCrimeLocation.Z) < 1000000.f;
    }
};
