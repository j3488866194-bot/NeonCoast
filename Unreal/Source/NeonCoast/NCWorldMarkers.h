#pragma once

#include "CoreMinimal.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Actor.h"
#include "NCWorldMarkers.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class URotatingMovementComponent;
class UNCGameplaySubsystem;

/** Low segmented ground rings, an objective chevron and gold coast tokens; replaces the debug navigation markers. */
UCLASS()
class NEONCOAST_API ANCWorldMarkers : public AActor
{
    GENERATED_BODY()
public:
    ANCWorldMarkers();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY() TObjectPtr<USceneComponent> Root;
    UPROPERTY() TObjectPtr<USceneComponent> ObjectivePivot;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ObjectiveSegments;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> ServicePivots;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> TokenPivots;
    UPROPERTY() TArray<TObjectPtr<URotatingMovementComponent>> TokenSpinners;

    TArray<FVector> ServiceLocations;
    TArray<bool> ServiceShown;
    TArray<bool> TokenShown;
    float ObjectiveRadius = 0.f;
    bool bObjectiveShown = true;
    bool bBuilt = false;

    UNCGameplaySubsystem* GetGameplay() const;
    USceneComponent* MakePivot(const FString& Name, const FVector& WorldLocation);
    URotatingMovementComponent* MakeSpinner(const FString& Name, USceneComponent* Target, float DegreesPerSecond);
    void BuildRing(USceneComponent* Pivot, const FString& Prefix, int32 Count, float Radius, float Width,
        const FLinearColor& Color, float Emissive, TArray<TObjectPtr<UStaticMeshComponent>>& OutSegments);
    static void LayoutRing(const TArray<TObjectPtr<UStaticMeshComponent>>& Segments, float Radius, float Width);
    static void Finish(UStaticMeshComponent* Mesh, float Emissive, float Metallic, float Roughness);
    static void Show(USceneComponent* Pivot, bool& bCached, bool bVisible);
};
