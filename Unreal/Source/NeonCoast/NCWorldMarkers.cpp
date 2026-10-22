#include "NCWorldMarkers.h"
#include "NCGameplaySubsystem.h"
#include "NCPrimitives.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    // Palette: warm amber objective, aqua garage, mint clinic, violet race, brushed gold tokens.
    // Objective amber sits slightly more orange and saturated than the LineYellow centre line.
    const FLinearColor ObjectiveColor(.85f, .45f, .06f);
    const FLinearColor ServiceColors[] = { FLinearColor(.12f, .78f, .86f), FLinearColor(.32f, .94f, .58f), FLinearColor(.6f, .32f, .98f) };
    const FLinearColor GoldColor(1.f, .7f, .24f);
    const FLinearColor GoldInsetColor(.82f, .46f, .1f);
    constexpr float RaceRadius = 700.f;
    constexpr float ObjectiveRadiusDefault = 470.f;
    constexpr float ObjectiveWidth = 34.f;
    constexpr float ServiceRadius = 300.f;
    constexpr float ServiceShowDistance = 15000.f;
    constexpr float TokenShowDistance = 10000.f;
    // Marker points sit 12 cm above the road top, so the centred cube dash is lowered to read as paint:
    // centre 1.75 cm, bottom 1.0 cm, top 2.5 cm above the road - clear of z-fighting, no visible side face.
    constexpr float RingLift = -10.25f;
    constexpr float RingThickness = 1.5f;
}

ANCWorldMarkers::ANCWorldMarkers()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .1f;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    SetRootComponent(Root);
    SetActorEnableCollision(false);
}

UNCGameplaySubsystem* ANCWorldMarkers::GetGameplay() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UNCGameplaySubsystem>() : nullptr;
}

USceneComponent* ANCWorldMarkers::MakePivot(const FString& Name, const FVector& WorldLocation)
{
    USceneComponent* Pivot = NewObject<USceneComponent>(this, FName(*Name));
    Pivot->SetMobility(EComponentMobility::Movable);
    Pivot->SetupAttachment(Root);
    Pivot->RegisterComponent();
    AddInstanceComponent(Pivot);
    Pivot->SetWorldLocation(WorldLocation);
    return Pivot;
}

URotatingMovementComponent* ANCWorldMarkers::MakeSpinner(const FString& Name, USceneComponent* Target, float DegreesPerSecond)
{
    URotatingMovementComponent* Spinner = NewObject<URotatingMovementComponent>(this, FName(*Name));
    Spinner->RotationRate = FRotator(0.f, DegreesPerSecond, 0.f);
    Spinner->SetUpdatedComponent(Target);
    Spinner->RegisterComponent();
    AddInstanceComponent(Spinner);
    return Spinner;
}

void ANCWorldMarkers::Finish(UStaticMeshComponent* Mesh, float Emissive, float Metallic, float Roughness)
{
    if (!Mesh) return;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(false);
    Mesh->SetReceivesDecals(false);
    if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
    {
        Material->SetScalarParameterValue(TEXT("EmissiveStrength"), Emissive);
        Material->SetScalarParameterValue(TEXT("Metallic"), Metallic);
        Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
    }
}

void ANCWorldMarkers::LayoutRing(const TArray<TObjectPtr<UStaticMeshComponent>>& Segments, float Radius, float Width)
{
    const int32 Count = Segments.Num();
    if (Count == 0) return;
    // Each dash covers 58% of its arc, leaving clean gaps that read as a dashed ground ring.
    const float DashLength = 2.f * PI * Radius / Count * .58f;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        UStaticMeshComponent* Segment = Segments[Index];
        if (!Segment) continue;
        const float Angle = 2.f * PI * Index / Count;
        Segment->SetRelativeLocationAndRotation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, RingLift),
            FRotator(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f));
        Segment->SetRelativeScale3D(FVector(DashLength / 100.f, Width / 100.f, RingThickness / 100.f));
    }
}

void ANCWorldMarkers::BuildRing(USceneComponent* Pivot, const FString& Prefix, int32 Count, float Radius, float Width,
    const FLinearColor& Color, float Emissive, TArray<TObjectPtr<UStaticMeshComponent>>& OutSegments)
{
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FString Name = FString::Printf(TEXT("%s_Seg%d"), *Prefix, Index);
        UStaticMeshComponent* Segment = NCPrimitives::Part(this, Pivot, *Name, TEXT("Cube"), FVector::ZeroVector, FVector::OneVector, Color);
        // Road-paint roughness so the dashes read as markings, not light bars.
        Finish(Segment, Emissive, 0.f, .6f);
        OutSegments.Add(Segment);
    }
    LayoutRing(OutSegments, Radius, Width);
}

void ANCWorldMarkers::BeginPlay()
{
    Super::BeginPlay();
    SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    UNCGameplaySubsystem* Gameplay = GetGameplay();
    if (!Gameplay) return;
    Gameplay->SetDrawGameplayMarkers(false);

    // Objective: dashed amber ring plus a small crossed chevron pointing down, slowly turning.
    ObjectiveRadius = Gameplay->IsRaceActive() ? RaceRadius : ObjectiveRadiusDefault;
    ObjectivePivot = MakePivot(TEXT("ObjectivePivot"), Gameplay->GetObjectiveLocation());
    BuildRing(ObjectivePivot, TEXT("Objective"), 24, ObjectiveRadius, ObjectiveWidth, ObjectiveColor, .7f, ObjectiveSegments);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const float Side = (Index % 2) ? 1.f : -1.f;
        const bool bCross = Index >= 2;
        const FVector Offset = (bCross ? FVector::RightVector : FVector::ForwardVector) * 20.f * Side + FVector(0.f, 0.f, 270.f);
        UStaticMeshComponent* Bar = NCPrimitives::Part(this, ObjectivePivot, *FString::Printf(TEXT("ObjectiveChevron%d"), Index),
            TEXT("Cube"), Offset, FVector(.6f, .09f, .09f), ObjectiveColor);
        Bar->SetRelativeRotation(FRotator(45.f * Side, bCross ? 90.f : 0.f, 0.f));
        Finish(Bar, 1.f, 0.f, .35f);
    }
    MakeSpinner(TEXT("ObjectiveSpinner"), ObjectivePivot, 10.f);

    // Services: Garage, Clinic, Race start - narrower rings in their own colour.
    ServiceLocations = { Gameplay->GetGarageLocation(), Gameplay->GetClinicLocation(), Gameplay->GetRaceStartLocation() };
    const TCHAR* ServiceNames[] = { TEXT("Garage"), TEXT("Clinic"), TEXT("Race") };
    for (int32 Index = 0; Index < ServiceLocations.Num(); ++Index)
    {
        USceneComponent* Pivot = MakePivot(FString::Printf(TEXT("%sPivot"), ServiceNames[Index]), ServiceLocations[Index]);
        TArray<TObjectPtr<UStaticMeshComponent>> Segments;
        BuildRing(Pivot, ServiceNames[Index], 14, ServiceRadius, 22.f, ServiceColors[Index], .6f, Segments);
        ServicePivots.Add(Pivot);
        ServiceShown.Add(true);
    }

    // Tokens: upright metallic gold coins with a recessed inset, spinning in place.
    const TArray<FVector>& Tokens = Gameplay->GetCollectibleLocations();
    for (int32 Index = 0; Index < Tokens.Num(); ++Index)
    {
        USceneComponent* Pivot = MakePivot(FString::Printf(TEXT("TokenPivot%d"), Index), Tokens[Index]);
        UStaticMeshComponent* Coin = NCPrimitives::Part(this, Pivot, *FString::Printf(TEXT("TokenCoin%d"), Index),
            TEXT("Cylinder"), FVector::ZeroVector, FVector(.8f, .8f, .1f), GoldColor);
        Coin->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
        // Gold reads through sun specular; only a trace of emission keeps it findable in shade.
        Finish(Coin, .1f, 1.f, .22f);
        UStaticMeshComponent* Inset = NCPrimitives::Part(this, Pivot, *FString::Printf(TEXT("TokenInset%d"), Index),
            TEXT("Cylinder"), FVector::ZeroVector, FVector(.5f, .5f, .14f), GoldInsetColor);
        Inset->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
        Finish(Inset, .15f, 1.f, .35f);
        TokenPivots.Add(Pivot);
        TokenSpinners.Add(MakeSpinner(FString::Printf(TEXT("TokenSpinner%d"), Index), Pivot, 90.f));
        TokenShown.Add(true);
    }
    bBuilt = true;
}

void ANCWorldMarkers::Show(USceneComponent* Pivot, bool& bCached, bool bVisible)
{
    if (!Pivot || bCached == bVisible) return;
    bCached = bVisible;
    Pivot->SetVisibility(bVisible, true);
}

void ANCWorldMarkers::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const UNCGameplaySubsystem* Gameplay = GetGameplay();
    if (!bBuilt || !Gameplay) return;
    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    const FVector PlayerLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;

    const bool bObjective = Pawn && Gameplay->HasObjective();
    if (bObjective)
    {
        // Race checkpoints and job stages both flow through the same objective location.
        const FVector Target = Gameplay->GetObjectiveLocation();
        if (!ObjectivePivot->GetComponentLocation().Equals(Target, 1.f)) ObjectivePivot->SetWorldLocation(Target);
        const float Radius = Gameplay->IsRaceActive() ? RaceRadius : ObjectiveRadiusDefault;
        if (!FMath::IsNearlyEqual(Radius, ObjectiveRadius))
        {
            ObjectiveRadius = Radius;
            LayoutRing(ObjectiveSegments, ObjectiveRadius, ObjectiveWidth);
        }
    }
    Show(ObjectivePivot, bObjectiveShown, bObjective);

    for (int32 Index = 0; Index < ServicePivots.Num(); ++Index)
        Show(ServicePivots[Index], ServiceShown[Index],
            Pawn && FVector::Dist2D(PlayerLocation, ServiceLocations[Index]) < ServiceShowDistance);

    for (int32 Index = 0; Index < TokenPivots.Num(); ++Index)
    {
        const bool bVisible = Pawn && !Gameplay->IsCollectibleCollected(Index)
            && FVector::Dist2D(PlayerLocation, TokenPivots[Index]->GetComponentLocation()) < TokenShowDistance;
        if (TokenShown[Index] != bVisible && TokenSpinners[Index]) TokenSpinners[Index]->SetComponentTickEnabled(bVisible);
        Show(TokenPivots[Index], TokenShown[Index], bVisible);
    }
}

void ANCWorldMarkers::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UNCGameplaySubsystem* Gameplay = GetGameplay()) Gameplay->SetDrawGameplayMarkers(true);
    Super::EndPlay(EndPlayReason);
}
