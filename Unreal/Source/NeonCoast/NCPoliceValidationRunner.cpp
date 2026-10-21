#include "NCPoliceValidationRunner.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCGameplaySubsystem.h"
#include "NCPoliceDirector.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"

ANCPoliceValidationRunner::ANCPoliceValidationRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void ANCPoliceValidationRunner::Check(bool Passed, const TCHAR* Name)
{
    ++Checks;
    if (!Passed) ++Failures;
    UE_LOG(LogTemp, Display, TEXT("NC_POLICE_CHECK %s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
}

void ANCPoliceValidationRunner::Advance(int32 NextPhase)
{
    Phase = NextPhase;
    PhaseClock = 0.f;
}

TArray<ANCCar*> ANCPoliceValidationRunner::HealthyPolice() const
{
    TArray<ANCCar*> Result;
    for (TActorIterator<ANCCar> It(GetWorld()); It; ++It)
        if (It->IsPoliceVehicle() && It->GetCondition() > 0.f && !It->WasPlayerDriven() && !It->GetDriverHero())
            Result.Add(*It);
    return Result;
}

void ANCPoliceValidationRunner::PlacePlayer(FVector Location)
{
    Hero->GetCharacterMovement()->StopMovementImmediately();
    // A stationary target makes sight assertions independent of gravity/input.
    // Neither police state nor gameplay search time is overridden by the fixture.
    Hero->GetCharacterMovement()->DisableMovement();
    Hero->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
}

void ANCPoliceValidationRunner::PlaceUnit(ANCCar* Unit, FVector Location)
{
    Unit->SetActorLocationAndRotation(Location, FRotator(0, 90, 0), false, nullptr, ETeleportType::TeleportPhysics);
    Unit->GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Unit->GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    if (auto* Movement = Cast<UChaosWheeledVehicleMovementComponent>(Unit->GetVehicleMovementComponent()))
    {
        Movement->ResetVehicle();
        Movement->SetSleeping(false);
    }
    // Deliberately no SetDrivingInput/SetPursuitTarget: the real director and
    // vehicle AI retain control after this deterministic position setup.
}

bool ANCPoliceValidationRunner::BuildHideout()
{
    Hideout = GetWorld()->SpawnActor<AActor>();
    if (!Hideout) return false;
    auto* Root = NewObject<USceneComponent>(Hideout, TEXT("PoliceTestCoverRoot"));
    Hideout->SetRootComponent(Root);
    Root->RegisterComponent();
    Hideout->AddInstanceComponent(Root);
    Hideout->SetActorLocation(FVector(SightPoint.X, SightPoint.Y, 0));
    const FVector Centers[] = { FVector(-1200,0,600), FVector(1200,0,600), FVector(0,-1200,600), FVector(0,1200,600) };
    const FVector Extents[] = { FVector(50,1250,600), FVector(50,1250,600), FVector(1250,50,600), FVector(1250,50,600) };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto* Wall = NewObject<UBoxComponent>(Hideout, *FString::Printf(TEXT("PoliceTestCover%d"), Index));
        Wall->SetupAttachment(Root);
        Wall->SetRelativeLocation(Centers[Index]);
        Wall->InitBoxExtent(Extents[Index]);
        Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Wall->SetCollisionObjectType(ECC_WorldStatic);
        Wall->SetCollisionResponseToAllChannels(ECR_Block);
        Wall->SetCanEverAffectNavigation(false);
        Wall->RegisterComponent();
        Hideout->AddInstanceComponent(Wall);
    }
    return true;
}

bool ANCPoliceValidationRunner::SightRayHitsHideout(ANCCar* Unit) const
{
    if (!Unit || !Hero || !Hideout) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceTestOcclusion), false, Unit);
    Params.AddIgnoredActor(Hero);
    FHitResult Hit;
    return GetWorld()->LineTraceSingleByChannel(Hit, Unit->GetActorLocation() + FVector(0,0,110),
        Hero->GetActorLocation() + FVector(0,0,60), ECC_Visibility, Params) && Hit.GetActor() == Hideout;
}

void ANCPoliceValidationRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Phase == 99) return;
    Clock += DeltaSeconds;
    PhaseClock += DeltaSeconds;
    if (Clock > 110.f) { Check(false, TEXT("police integration finishes within 110 world seconds")); Finish(); return; }
    auto* Gameplay = GetWorld()->GetSubsystem<UNCGameplaySubsystem>();
    auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (Phase == 0)
    {
        if (PhaseClock < 2.f) return;
        Hero = PC ? Cast<ANCHero>(PC->GetPawn()) : nullptr;
        Check(Hero != nullptr && Gameplay != nullptr, TEXT("police fixture has real possessed hero and gameplay subsystem"));
        if (!Hero || !Gameplay) { Finish(); return; }
        Check(!Gameplay->IsPersistenceEnabled(), TEXT("police test cannot read or write player save slots"));
        int32 Directors = 0;
        for (TActorIterator<ANCPoliceDirector> It(GetWorld()); It; ++It) ++Directors;
        Check(Directors == 1, TEXT("exactly one normal police director is running"));
        Check(HealthyPolice().IsEmpty(), TEXT("no police actors exist before the first reported crime"));
        if (Directors != 1 || Gameplay->IsPersistenceEnabled()) { Finish(); return; }
        PlacePlayer(FVector(21600, -12000, 96));
        PC->SetControlRotation(FRotator(-12, 90, 0));
        Gameplay->ReportCrime(.85f, Hero->GetActorLocation());
        Check(Gameplay->GetWantedLevel() == 1, TEXT("public crime report creates one-star dispatch"));
        NextCrimeAt = Clock + 4.f;
        Advance(1);
        return;
    }
    if (!Hero || !Gameplay || !PC) { Check(false, TEXT("police fixture actors remain valid")); Finish(); return; }

    // Keep dispatch active while waiting for naturally spawned response units.
    // This is an actual public crime event, never a forced sight/heat assignment.
    if ((Phase == 1 || Phase == 2 || Phase == 5) && Clock >= NextCrimeAt)
    {
        Gameplay->ReportCrime(.005f, Hero->GetActorLocation());
        NextCrimeAt = Clock + 4.f;
    }
    const TArray<ANCCar*> Units = HealthyPolice();
    switch (Phase)
    {
    case 1:
        if (Units.Num() < 2 && PhaseClock < 16.f) break;
        Check(Units.Num() >= 2, TEXT("one-star response spawns two healthy police vehicles through the normal director"));
        if (Units.Num() < 2) { Finish(); return; }
        ObservedUnit = Units[0];
        for (ANCCar* Unit : Units) InitialUnitPositions.Add(Unit, Unit->GetActorLocation());
        Advance(2);
        break;
    case 2:
        for (const auto& Entry : InitialUnitPositions)
            if (Entry.Key.IsValid()) MaxNaturalTravel = FMath::Max(MaxNaturalTravel,
                float(FVector::Dist2D(Entry.Value, Entry.Key->GetActorLocation())));
        if (PhaseClock < 4.f) break;
        Check(MaxNaturalTravel > 200.f, TEXT("normally dispatched police move more than two metres using world physics and vehicle AI"));
        if (!IsValid(ObservedUnit) || ObservedUnit->GetCondition() <= 0.f) { Check(false, TEXT("observed police unit survives dispatch")); Finish(); return; }
        PlaceUnit(ObservedUnit, FVector(21600, -18000, 85));
        SightPoint = FVector(21600, -14000, 96);
        PlacePlayer(SightPoint);
        Advance(3);
        break;
    case 3:
        if (PhaseClock < .8f) break;
        if (!Gameplay->IsPoliceSeeingPlayer() && PhaseClock < 5.f) break;
        Check(Gameplay->IsPoliceSeeingPlayer(), TEXT("real officer line of sight reacquires the player on an open road"));
        Check(FVector::Dist(Gameplay->GetLastCrimeLocation(), SightPoint) < 100.f,
            TEXT("sight report updates last known position without a new crime"));
        if (!Gameplay->IsPoliceSeeingPlayer()) { Finish(); return; }
        SightPoint += FVector(0, 600, 0);
        PlacePlayer(SightPoint);
        Advance(4);
        break;
    case 4:
        if (PhaseClock < 1.f) break;
        Check(Gameplay->IsPoliceSeeingPlayer() && Gameplay->GetWantedLevel() > 0,
            TEXT("visible target remains under pursuit"));
        Check(FVector::Dist(Gameplay->GetLastCrimeLocation(), SightPoint) < 100.f,
            TEXT("normal sight reports track a player's changed visible position"));
        BeforeWreckUnits.Reset();
        for (ANCCar* Unit : Units) BeforeWreckUnits.Add(Unit);
        HealthyBeforeWreck = Units.Num();
        Wreck = ObservedUnit;
        // Neutral fixture damage isolates replacement policy from added crime severity.
        UGameplayStatics::ApplyDamage(Wreck, 150.f, nullptr, this, UDamageType::StaticClass());
        Check(Wreck->GetCondition() <= 0.f, TEXT("public damage API creates an actual police wreck"));
        NextCrimeAt = Clock + 4.f;
        Advance(5);
        break;
    case 5:
    {
        ANCCar* Replacement = nullptr;
        for (ANCCar* Unit : Units)
            if (!BeforeWreckUnits.Contains(TWeakObjectPtr<ANCCar>(Unit))) { Replacement = Unit; break; }
        if ((!Replacement || Units.Num() < HealthyBeforeWreck) && PhaseClock < 16.f) break;
        Check(Replacement != nullptr && Units.Num() >= HealthyBeforeWreck,
            TEXT("wreck does not consume a response slot: a new healthy unit restores the dispatch count"));
        Check(IsValid(Wreck) && Wreck->GetCondition() <= 0.f,
            TEXT("replacement is a new actor rather than silently repairing the wreck"));
        if (!Replacement || Units.IsEmpty()) { Finish(); return; }
        ObservedUnit = Replacement;
        // Place all live response cars outside a reproducible physical hideout.
        // They remain AI-controlled and continue to tick throughout the search.
        int32 Index = 0;
        // Use a different road segment so the genuine wreck remains untouched
        // and cannot accidentally serve as the next fixture's sight blocker.
        for (ANCCar* Unit : Units)
            PlaceUnit(Unit, FVector(21600, 10000 - Index++ * 1300, 85));
        SightPoint = FVector(21600, 14000, 96);
        PlacePlayer(SightPoint);
        Advance(6);
        break;
    }
    case 6:
        if (PhaseClock < .8f) break;
        if (!Gameplay->IsPoliceSeeingPlayer() && PhaseClock < 4.f) break;
        Check(Gameplay->IsPoliceSeeingPlayer(), TEXT("search fixture establishes a real last sighting before cover"));
        if (!Gameplay->IsPoliceSeeingPlayer()) { Finish(); return; }
        LastSeenBeforeCover = Gameplay->GetLastCrimeLocation();
        if (!BuildHideout()) { Check(false, TEXT("physical visibility cover can be created")); Finish(); return; }
        PlacePlayer(SightPoint + FVector(600, 300, 0));
        // The first vehicle may not be the nearest after array reordering; test
        // every live unit and require at least one matching visibility blocker.
        {
            bool bConfirmedBlock = false;
            for (ANCCar* Unit : Units) bConfirmedBlock |= SightRayHitsHideout(Unit);
            Check(bConfirmedBlock, TEXT("world collision ray confirms physical cover blocks officer-to-player sight"));
        }
        SearchStartAt = Clock;
        Advance(7);
        break;
    case 7:
        if (PhaseClock > 1.2f && Gameplay->IsPoliceSeeingPlayer()) bSearchWasSeen = true;
        if (!bCheckedSearchDelay && PhaseClock >= 6.f)
        {
            bCheckedSearchDelay = true;
            Check(!Gameplay->IsPoliceSeeingPlayer(), TEXT("normal director loses sight behind real collision cover"));
            Check(Gameplay->GetWantedLevel() > 0, TEXT("wanted level remains during the genuine initial search delay"));
            Check(FVector::Dist(Gameplay->GetLastCrimeLocation(), LastSeenBeforeCover) < 100.f
                && FVector::Dist(Gameplay->GetLastCrimeLocation(), Hero->GetActorLocation()) > 500.f,
                TEXT("hidden movement does not update the police last known search location"));
        }
        if (Gameplay->GetWantedLevel() > 0 && PhaseClock < 35.f) break;
        SearchSeconds = Clock - SearchStartAt;
        Check(!bSearchWasSeen, TEXT("all normal officers remain unable to see the covered target throughout search"));
        Check(Gameplay->GetWantedLevel() == 0 && SearchSeconds > 8.f,
            TEXT("wanted clears only after actual elapsed search and heat decay while police continue ticking"));
        Check(FVector::Dist(Gameplay->GetLastCrimeLocation(), LastSeenBeforeCover) < 100.f,
            TEXT("search never omnisciently follows the hidden target"));
        Finish();
        break;
    default: break;
    }
}

void ANCPoliceValidationRunner::Finish()
{
    Phase = 99;
    UE_LOG(LogTemp, Display, TEXT("NC_POLICE_METRIC naturalTravelCm=%.2f searchSeconds=%.2f healthyBeforeWreck=%d worldSeconds=%.2f"),
        MaxNaturalTravel, SearchSeconds, HealthyBeforeWreck, Clock);
    UE_LOG(LogTemp, Display, TEXT("NEON_COAST_POLICE_%s checks=%d failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures);
    FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
