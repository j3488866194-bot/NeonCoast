#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NCGameplaySubsystem.generated.h"

class AActor;
class APawn;
class ANCHero;
class ANCCar;

/** Single-player jobs, street circuit, discoveries, services and local progress. */
UCLASS()
class NEONCOAST_API UNCGameplaySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;

    int32 GetCash() const { return Cash; }
    int32 GetWantedLevel() const;
    FString GetObjectiveText() const;
    FString GetStatusText() const;
    FVector GetObjectiveLocation() const;
    bool HasObjective() const;
    float GetRaceTimeRemaining() const { return bRaceActive ? FMath::Max(0.f, RaceLimit - RaceElapsed) : 0.f; }
    int32 GetCollectibleCount() const;
    int32 GetMissionStage() const { return MissionStage; }
    int32 GetCompletedDeliveries() const { return CompletedDeliveries; }
    int32 GetRaceWins() const { return RaceWins; }
    float GetBestRaceTime() const { return BestRaceSeconds; }
    bool IsRaceActive() const { return bRaceActive; }
    int32 GetRaceCheckpointIndex() const { return RaceCheckpoint; }
    int32 GetRaceCheckpointCount() const { return RaceGates.Num(); }
    float GetWantedSearchProgress() const;
    bool IsPoliceSeeingPlayer() const { return Clock - LastPoliceSightTime < 1.f; }
    FVector GetLastCrimeLocation() const { return LastCrimeLocation; }

    void Interact(AActor* Player) { TryInteract(Player); }
    /** True consumes E/X, including an unavailable service with an explanatory message. */
    bool TryInteract(AActor* Player);
    FString GetInteractionPrompt(AActor* Player) const;
    void ReportCrime(float Severity, FVector Location);
    /** Call once after aggregating every officer's line-of-sight result. */
    void ReportPoliceSight(bool bSeen);
    void OnPlayerDefeated();
    bool SaveProgress();

    const TArray<FVector>& GetCollectibleLocations() const { return CollectibleLocations; }
    bool IsCollectibleCollected(int32 Index) const { return Index >= 0 && Index < 8 && (CollectedMask & (1 << Index)) != 0; }
    const TArray<FVector>& GetRaceGates() const { return RaceGates; }
    FVector GetDepotLocation() const { return DepotLocation; }
    FVector GetGarageLocation() const { return GarageLocation; }
    FVector GetClinicLocation() const { return ClinicLocation; }
    FVector GetRaceStartLocation() const { return RaceStartLocation; }
    /** An art actor may replace the basic in-world navigation rings. */
    void SetDrawGameplayMarkers(bool bEnabled) { bDrawGameplayMarkers = bEnabled; }
    bool IsPersistenceEnabled() const { return bPersistenceEnabled; }

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    int32 Cash = 250;
    int32 CollectedMask = 0;
    int32 CompletedDeliveries = 0;
    int32 RaceWins = 0;
    int32 MissionStage = 0;
    float BestRaceSeconds = 0.f;
    float WantedHeat = 0.f;
    float Clock = 0.f;
    float LastCrimeTime = -1000.f;
    float LastPoliceSightTime = -1000.f;
    FVector LastCrimeLocation = FVector::ZeroVector;
    float RaceElapsed = 0.f;
    int32 RaceCheckpoint = 0;
    bool bRaceActive = false;
    bool bPersistenceEnabled = false;
    bool bDirty = false;
    bool bDrawGameplayMarkers = true;
    float NextAutosave = 0.f;
    FString Status;
    float StatusUntil = 0.f;
    FString SaveWarning;

    const FVector DepotLocation = FVector(21600, 3000, 12);
    const FVector DeliveryLocation = FVector(14400, 7200, 12);
    const FVector GarageLocation = FVector(21600, 0, 12);
    const FVector ClinicLocation = FVector(21600, 7200, 12);
    const FVector RaceStartLocation = FVector(21600, -7200, 12);
    static constexpr float RaceLimit = 95.f;
    static constexpr float InteractionRadius = 650.f;
    TArray<FVector> CollectibleLocations;
    TArray<FVector> RaceGates;

    void LoadProgress();
    void MarkDirty() { bDirty = true; }
    void SetStatus(const FString& Message, float Seconds = 5.f);
    void TickWanted(float DeltaTime);
    void TickRace(float DeltaTime, APawn* Pawn);
    void TickCollectibles(APawn* Pawn);
    void DrawMarkers(APawn* Pawn) const;
    void CompleteRace();
    bool IsNear(AActor* Player, const FVector& Point) const;
    bool IsStopped(AActor* Player) const;
    ANCHero* ResolveHero(AActor* Player) const;
    void AwardCash(int32 Amount);
};
