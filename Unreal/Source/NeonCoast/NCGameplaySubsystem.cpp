#include "NCGameplaySubsystem.h"
#include "NCSaveGame.h"
#include "NCCar.h"
#include "NCHero.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    const FString ProgressSlot(TEXT("NeonCoastProgress_v1"));
    const FString BackupSlot(TEXT("NeonCoastProgress_v1_backup"));
    constexpr float SearchDelay = 8.f;
    constexpr float HeatDecayPerSecond = .075f;
    constexpr int32 GarageCost = 100;
    constexpr int32 ClinicCost = 75;
}

bool UNCGameplaySubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNCGameplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    CollectibleLocations = {
        FVector(22600, 1200, 110), FVector(22600, 6100, 110),
        FVector(22600, 11200, 110), FVector(22600, 17300, 110),
        FVector(14400, 14400, 110), FVector(7200, -7200, 110),
        FVector(0, 0, 110), FVector(-7200, 7200, 110)
    };
    // Every leg follows a road; no gate asks the player to cut through a city block.
    RaceGates = {
        FVector(21600, -14400, 12), FVector(14400, -14400, 12),
        FVector(14400, -7200, 12), FVector(7200, -7200, 12),
        FVector(7200, 0, 12), FVector(14400, 0, 12),
        FVector(21600, 0, 12), RaceStartLocation
    };
    bPersistenceEnabled = !FParse::Param(FCommandLine::Get(), TEXT("NeonCoastSmokeTest"))
        && !FParse::Param(FCommandLine::Get(), TEXT("NeonCoastPoliceTest"))
        && !IsRunningCommandlet();
    if (bPersistenceEnabled) LoadProgress();
    else SetStatus(TEXT("Validation session - progress is isolated from local saves."), 2.f);
    NextAutosave = 10.f;
}

void UNCGameplaySubsystem::Deinitialize()
{
    if (bDirty && bPersistenceEnabled) SaveProgress();
    Super::Deinitialize();
}

bool UNCGameplaySubsystem::IsTickable() const
{
    return !IsTemplate() && IsInitialized() && GetWorld() && GetWorld()->HasBegunPlay();
}

TStatId UNCGameplaySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UNCGameplaySubsystem, STATGROUP_Tickables);
}

void UNCGameplaySubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    Clock += DeltaTime;
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    TickWanted(DeltaTime);
    TickRace(DeltaTime, Pawn);
    TickCollectibles(Pawn);
    if (MissionStage == 2 && GetWantedLevel() == 0)
    {
        MissionStage = 3;
        SetStatus(TEXT("The search is over. Return to the coast depot for your payment."), 7.f);
        MarkDirty();
        SaveProgress();
    }
    if (bDrawGameplayMarkers && Pawn) DrawMarkers(Pawn);
    if (bDirty && Clock >= NextAutosave)
    {
        SaveProgress();
        NextAutosave = Clock + 10.f;
    }
}

int32 UNCGameplaySubsystem::GetWantedLevel() const
{
    return WantedHeat > .001f ? FMath::Clamp(FMath::CeilToInt(WantedHeat), 1, 3) : 0;
}

int32 UNCGameplaySubsystem::GetCollectibleCount() const
{
    int32 Count = 0;
    for (int32 Index = 0; Index < 8; ++Index) Count += IsCollectibleCollected(Index) ? 1 : 0;
    return Count;
}

void UNCGameplaySubsystem::SetStatus(const FString& Message, float Seconds)
{
    Status = Message;
    StatusUntil = Clock + Seconds;
    UE_LOG(LogTemp, Display, TEXT("NC_GAMEPLAY %s"), *Message);
}

FString UNCGameplaySubsystem::GetObjectiveText() const
{
    if (bRaceActive)
        return FString::Printf(TEXT("COAST CIRCUIT  |  Drive through gate %d / %d"), RaceCheckpoint + 1, RaceGates.Num());
    switch (MissionStage)
    {
    case 1: return TEXT("NIGHT COURIER  |  Drive the sealed case to the inland drop");
    case 2: return TEXT("NIGHT COURIER  |  Break police sight and lose the search");
    case 3: return TEXT("NIGHT COURIER  |  Return to the coast depot for $750");
    default:
        if (GetWantedLevel() > 0) return TEXT("Lose the police before starting another job");
        return TEXT("Visit the coast depot for a job, or the south road for a street race");
    }
}

FString UNCGameplaySubsystem::GetStatusText() const
{
    if (!SaveWarning.IsEmpty()) return SaveWarning;
    if (Clock < StatusUntil) return Status;
    if (GetWantedLevel() > 0)
    {
        if (IsPoliceSeeingPlayer()) return TEXT("PURSUIT  |  Police have you in sight - break line of sight");
        if (Clock - FMath::Max(LastCrimeTime, LastPoliceSightTime) < SearchDelay)
            return TEXT("SEARCHING  |  Get out of view and stay clear of patrols");
        return TEXT("LOSING HEAT  |  Stay out of police sight");
    }
    if (bRaceActive)
        return FString::Printf(TEXT("Ordered gates only  |  Best %s"),
            BestRaceSeconds > 0.f ? *FString::Printf(TEXT("%.1f s"), BestRaceSeconds) : TEXT("--"));
    return FString::Printf(TEXT("Coast tokens %d / 8  |  Jobs %d  |  Races won %d"),
        GetCollectibleCount(), CompletedDeliveries, RaceWins);
}

FVector UNCGameplaySubsystem::GetObjectiveLocation() const
{
    if (bRaceActive && RaceGates.IsValidIndex(RaceCheckpoint)) return RaceGates[RaceCheckpoint];
    return MissionStage == 1 ? DeliveryLocation : DepotLocation;
}

bool UNCGameplaySubsystem::HasObjective() const
{
    return bRaceActive || (MissionStage != 2 && !(MissionStage == 0 && GetWantedLevel() > 0));
}

float UNCGameplaySubsystem::GetWantedSearchProgress() const
{
    if (GetWantedLevel() == 0) return 1.f;
    if (IsPoliceSeeingPlayer()) return 0.f;
    return FMath::Clamp((Clock - FMath::Max(LastCrimeTime, LastPoliceSightTime)) / SearchDelay, 0.f, 1.f);
}

void UNCGameplaySubsystem::ReportCrime(float Severity, FVector Location)
{
    if (!FMath::IsFinite(Severity) || Severity <= 0.f || Location.ContainsNaN()) return;
    WantedHeat = FMath::Clamp(WantedHeat + Severity, 0.f, 3.f);
    LastCrimeTime = Clock;
    LastCrimeLocation = Location;
    MarkDirty();
}

void UNCGameplaySubsystem::ReportPoliceSight(bool bSeen)
{
    if (bSeen && GetWantedLevel() > 0)
    {
        LastPoliceSightTime = Clock;
        if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0)) LastCrimeLocation = Pawn->GetActorLocation();
    }
}

void UNCGameplaySubsystem::TickWanted(float DeltaTime)
{
    if (WantedHeat <= 0.f) return;
    // Both recent crime and a real officer's sight report prevent escape progress.
    if (Clock - FMath::Max(LastCrimeTime, LastPoliceSightTime) < SearchDelay) return;
    WantedHeat = FMath::Max(0.f, WantedHeat - DeltaTime * HeatDecayPerSecond);
    if (WantedHeat <= .001f)
    {
        WantedHeat = 0.f;
        SetStatus(TEXT("Police search ended. You are clear."), 5.f);
    }
    MarkDirty();
}

bool UNCGameplaySubsystem::IsNear(AActor* Player, const FVector& Point) const
{
    return IsValid(Player) && FVector::Dist2D(Player->GetActorLocation(), Point) <= InteractionRadius;
}

bool UNCGameplaySubsystem::IsStopped(AActor* Player) const
{
    const ANCCar* Car = Cast<ANCCar>(Player);
    return !Car || Car->GetSpeedKmh() < 5.f;
}

ANCHero* UNCGameplaySubsystem::ResolveHero(AActor* Player) const
{
    if (ANCHero* Hero = Cast<ANCHero>(Player)) return Hero;
    if (const ANCCar* Car = Cast<ANCCar>(Player)) return Car->GetDriverHero();
    return nullptr;
}

FString UNCGameplaySubsystem::GetInteractionPrompt(AActor* Player) const
{
    if (!IsValid(Player)) return FString();
    const FString Controls = Cast<ANCCar>(Player) ? TEXT("E / X") : TEXT("E / D-pad Right");
    if (MissionStage == 1 && IsNear(Player, DeliveryLocation))
        return !Cast<ANCCar>(Player) ? TEXT("Bring the case here in a car")
            : IsStopped(Player) ? FString::Printf(TEXT("%s  Deliver the sealed case"), *Controls) : TEXT("Stop to deliver the case");
    if (MissionStage == 3 && IsNear(Player, DepotLocation))
        return GetWantedLevel() > 0 ? TEXT("Lose your wanted level before returning")
            : IsStopped(Player) ? FString::Printf(TEXT("%s  Finish courier job  +$750"), *Controls) : TEXT("Stop to finish the job");
    if (MissionStage == 0 && !bRaceActive && IsNear(Player, DepotLocation))
        return GetWantedLevel() > 0 ? TEXT("Lose your wanted level before starting a job")
            : IsStopped(Player) ? FString::Printf(TEXT("%s  Accept NIGHT COURIER  $750"), *Controls) : TEXT("Stop to accept the job");
    if (IsNear(Player, GarageLocation))
        return Cast<ANCCar>(Player) ? (IsStopped(Player) ? TEXT("E / X  Repair vehicle  $100") : TEXT("Stop in the garage to repair"))
            : TEXT("Bring a vehicle to the garage for repairs");
    if (IsNear(Player, ClinicLocation))
        return IsStopped(Player) ? FString::Printf(TEXT("%s  Medical treatment  $75"), *Controls) : TEXT("Stop for medical treatment");
    if (!bRaceActive && IsNear(Player, RaceStartLocation))
    {
        if (MissionStage != 0) return TEXT("Finish your courier job before racing");
        if (GetWantedLevel() > 0) return TEXT("Lose the police before starting a race");
        if (!Cast<ANCCar>(Player)) return TEXT("Bring a car to start the COAST CIRCUIT");
        return IsStopped(Player) ? TEXT("E / X  Start COAST CIRCUIT  $450") : TEXT("Stop at the start line");
    }
    return FString();
}

bool UNCGameplaySubsystem::TryInteract(AActor* Player)
{
    // Services cannot be triggered remotely or through an unpossessed parked car.
    if (!IsValid(Player) || Player != UGameplayStatics::GetPlayerPawn(this, 0)) return false;
    if (GetInteractionPrompt(Player).IsEmpty()) return false;
    if (!IsStopped(Player)) { SetStatus(TEXT("Come to a stop to interact.")); return true; }
    if (MissionStage == 1 && IsNear(Player, DeliveryLocation))
    {
        if (!Cast<ANCCar>(Player)) { SetStatus(TEXT("Bring the sealed case to the drop in a car.")); return true; }
        MissionStage = 2;
        ReportCrime(1.4f, Player->GetActorLocation());
        SetStatus(TEXT("Case delivered. The exchange was spotted - lose the police!"), 7.f);
    }
    else if (MissionStage == 3 && IsNear(Player, DepotLocation))
    {
        if (GetWantedLevel() > 0) { SetStatus(TEXT("The contact will not meet while police are searching for you.")); return true; }
        MissionStage = 0;
        ++CompletedDeliveries;
        AwardCash(750);
        SetStatus(TEXT("NIGHT COURIER complete  +$750. Another job is available here."), 7.f);
    }
    else if (MissionStage == 0 && !bRaceActive && IsNear(Player, DepotLocation))
    {
        if (GetWantedLevel() > 0) { SetStatus(TEXT("Lose the police before accepting a courier job.")); return true; }
        MissionStage = 1;
        SetStatus(TEXT("NIGHT COURIER: take a car to the inland drop. Stop there and press E to deliver."), 8.f);
    }
    else if (IsNear(Player, GarageLocation))
    {
        ANCCar* Car = Cast<ANCCar>(Player);
        if (!Car) { SetStatus(TEXT("Drive a vehicle into the garage for repairs.")); return true; }
        if (Car->GetCondition() >= 99.9f) { SetStatus(TEXT("Your vehicle is already in good condition. No charge.")); return true; }
        if (Cash < GarageCost) { SetStatus(TEXT("Vehicle repairs cost $100. Earn cash from jobs, races or tokens.")); return true; }
        Cash -= GarageCost;
        Car->Repair();
        SetStatus(TEXT("Vehicle repaired  -$100"));
    }
    else if (IsNear(Player, ClinicLocation))
    {
        ANCHero* Hero = ResolveHero(Player);
        if (!Hero) return false;
        if (Hero->GetHealth() >= 99.9f) { SetStatus(TEXT("You are already healthy. No charge.")); return true; }
        if (Cash < ClinicCost) { SetStatus(TEXT("Medical treatment costs $75. Earn cash from jobs, races or tokens.")); return true; }
        Cash -= ClinicCost;
        Hero->RestoreHealth();
        SetStatus(TEXT("Health restored  -$75"));
    }
    else if (!bRaceActive && IsNear(Player, RaceStartLocation))
    {
        if (MissionStage != 0) { SetStatus(TEXT("Finish your courier job before entering a street race.")); return true; }
        if (GetWantedLevel() > 0) { SetStatus(TEXT("Lose the police before starting a street race.")); return true; }
        if (!Cast<ANCCar>(Player)) { SetStatus(TEXT("A car is required for the COAST CIRCUIT.")); return true; }
        bRaceActive = true;
        RaceElapsed = 0.f;
        RaceCheckpoint = 0;
        SetStatus(TEXT("COAST CIRCUIT started! Drive through eight ordered gates in 95 seconds."), 6.f);
        return true;
    }
    else return false;
    MarkDirty();
    SaveProgress();
    return true;
}

void UNCGameplaySubsystem::TickRace(float DeltaTime, APawn* Pawn)
{
    if (!bRaceActive) return;
    RaceElapsed += DeltaTime;
    if (RaceElapsed >= RaceLimit)
    {
        bRaceActive = false;
        RaceCheckpoint = 0;
        SetStatus(TEXT("Time expired. Return to the south-road start line to try again."), 7.f);
        return;
    }
    const ANCCar* Car = Cast<ANCCar>(Pawn);
    // Walking through a gate, coasting in an abandoned car, and visiting later gates do not count.
    if (!Car || !Car->IsPlayerControlled() || !RaceGates.IsValidIndex(RaceCheckpoint)) return;
    if (FVector::Dist2D(Car->GetActorLocation(), RaceGates[RaceCheckpoint]) > 800.f) return;
    ++RaceCheckpoint;
    if (RaceCheckpoint >= RaceGates.Num()) CompleteRace();
    else SetStatus(FString::Printf(TEXT("Gate %d / %d clear. Follow the next marker."), RaceCheckpoint, RaceGates.Num()), 2.f);
}

void UNCGameplaySubsystem::CompleteRace()
{
    bRaceActive = false;
    ++RaceWins;
    const bool bPersonalBest = BestRaceSeconds <= 0.f || RaceElapsed < BestRaceSeconds;
    if (bPersonalBest) BestRaceSeconds = RaceElapsed;
    AwardCash(450);
    SetStatus(FString::Printf(TEXT("COAST CIRCUIT complete  %.1f s  +$450%s"), RaceElapsed,
        bPersonalBest ? TEXT("  |  NEW BEST") : TEXT("")), 8.f);
    MarkDirty();
    SaveProgress();
}

void UNCGameplaySubsystem::TickCollectibles(APawn* Pawn)
{
    if (!Cast<ANCHero>(Pawn)) return;
    for (int32 Index = 0; Index < CollectibleLocations.Num(); ++Index)
    {
        if (IsCollectibleCollected(Index) || FVector::Dist2D(Pawn->GetActorLocation(), CollectibleLocations[Index]) > 190.f) continue;
        CollectedMask |= 1 << Index;
        AwardCash(75);
        const bool bAllFound = GetCollectibleCount() == CollectibleLocations.Num();
        if (bAllFound) AwardCash(400);
        SetStatus(bAllFound ? TEXT("All 8 coast tokens found!  +$75  +$400 exploration bonus")
            : FString::Printf(TEXT("Coast token %d / 8  +$75"), GetCollectibleCount()), 6.f);
        MarkDirty();
        SaveProgress();
    }
}

void UNCGameplaySubsystem::AwardCash(int32 Amount)
{
    Cash = FMath::Clamp(Cash + Amount, 0, 100000000);
    MarkDirty();
}

void UNCGameplaySubsystem::OnPlayerDefeated()
{
    const int32 Fee = FMath::Min(Cash, 125);
    Cash -= Fee;
    WantedHeat = 0.f;
    LastCrimeTime = LastPoliceSightTime = -1000.f;
    MissionStage = 0;
    bRaceActive = false;
    RaceCheckpoint = 0;
    SetStatus(FString::Printf(TEXT("Medical recovery  -$%d. Active jobs and races cancelled."), Fee), 9.f);
    MarkDirty();
    SaveProgress();
}

void UNCGameplaySubsystem::LoadProgress()
{
    UNCSaveGame* Save = Cast<UNCSaveGame>(UGameplayStatics::LoadGameFromSlot(ProgressSlot, 0));
    const bool bPrimaryExists = UGameplayStatics::DoesSaveGameExist(ProgressSlot, 0);
    bool bRecovered = false;
    if (!Save || !Save->IsSane())
    {
        Save = Cast<UNCSaveGame>(UGameplayStatics::LoadGameFromSlot(BackupSlot, 0));
        bRecovered = Save && Save->IsSane();
    }
    if (!Save || !Save->IsSane())
    {
        SetStatus(bPrimaryExists ? TEXT("Saved progress could not be read. Starting a fresh local session.")
            : TEXT("Welcome to Neon Coast. Visit the nearby depot for your first job."), 9.f);
        return;
    }
    Cash = Save->Cash;
    CollectedMask = Save->CollectedMask;
    CompletedDeliveries = Save->CompletedDeliveries;
    RaceWins = Save->RaceWins;
    BestRaceSeconds = Save->BestRaceSeconds;
    MissionStage = Save->MissionStage;
    WantedHeat = Save->WantedHeat;
    LastCrimeLocation = Save->LastCrimeLocation;
    if (WantedHeat > 0.f) LastCrimeTime = Clock;
    SetStatus(bRecovered ? TEXT("Progress recovered from the local backup. Active races restart at the start line.")
        : TEXT("Local progress loaded. Active races restart at the start line."), 7.f);
    if (bRecovered) MarkDirty();
}

bool UNCGameplaySubsystem::SaveProgress()
{
    if (!bPersistenceEnabled) { bDirty = false; return true; }
    UNCSaveGame* NewSave = Cast<UNCSaveGame>(UGameplayStatics::CreateSaveGameObject(UNCSaveGame::StaticClass()));
    if (!NewSave) { SaveWarning = TEXT("Unable to create a local save. Progress remains in this session."); return false; }
    NewSave->Cash = Cash;
    NewSave->CollectedMask = CollectedMask;
    NewSave->CompletedDeliveries = CompletedDeliveries;
    NewSave->RaceWins = RaceWins;
    NewSave->BestRaceSeconds = BestRaceSeconds;
    NewSave->MissionStage = MissionStage;
    NewSave->WantedHeat = WantedHeat;
    NewSave->LastCrimeLocation = LastCrimeLocation;
    if (!NewSave->IsSane()) { SaveWarning = TEXT("Progress validation failed. Previous local save preserved."); return false; }
    // Keep the previous valid main save; never replace a good backup with corrupt data.
    UNCSaveGame* Previous = Cast<UNCSaveGame>(UGameplayStatics::LoadGameFromSlot(ProgressSlot, 0));
    bool bBackupOK = true;
    if (Previous && Previous->IsSane()) bBackupOK = UGameplayStatics::SaveGameToSlot(Previous, BackupSlot, 0);
    if (!UGameplayStatics::SaveGameToSlot(NewSave, ProgressSlot, 0))
    {
        SaveWarning = TEXT("Local save failed. Progress remains in this session; check free disk space.");
        UE_LOG(LogTemp, Warning, TEXT("NC_GAMEPLAY_SAVE_FAILED"));
        return false;
    }
    bDirty = false;
    SaveWarning = bBackupOK ? FString() : TEXT("Progress saved, but the previous-save backup could not be updated.");
    UE_LOG(LogTemp, Display, TEXT("NC_GAMEPLAY_SAVED cash=%d mission=%d tokens=%d"), Cash, MissionStage, GetCollectibleCount());
    return true;
}

void UNCGameplaySubsystem::DrawMarkers(APawn* Pawn) const
{
    const UWorld* World = GetWorld();
    if (!World || !Pawn) return;
    const FVector PlayerLocation = Pawn->GetActorLocation();
    const float Pulse = 1.f + FMath::Sin(Clock * 2.f) * .06f;
    auto Ring = [World, Pulse](const FVector& Point, float Radius, FColor Color)
    {
        DrawDebugCircle(World, Point + FVector(0, 0, 8), Radius * Pulse, 40, Color, false, 0.f, 0, 5.f,
            FVector::ForwardVector, FVector::RightVector, false);
        DrawDebugLine(World, Point + FVector(0, 0, 30), Point + FVector(0, 0, 650), Color, false, 0.f, 0, 7.f);
    };
    if (HasObjective()) Ring(GetObjectiveLocation(), bRaceActive ? 700.f : 470.f, FColor(255, 170, 62));
    const FVector ServicePoints[] = { GarageLocation, ClinicLocation, RaceStartLocation };
    const FColor ServiceColors[] = { FColor(65, 215, 222), FColor(115, 240, 167), FColor(206, 120, 250) };
    for (int32 Index = 0; Index < 3; ++Index)
        if (FVector::Dist2D(PlayerLocation, ServicePoints[Index]) < 15000.f)
            Ring(ServicePoints[Index], 280.f, ServiceColors[Index]);
    for (int32 Index = 0; Index < CollectibleLocations.Num(); ++Index)
    {
        const FVector Point = CollectibleLocations[Index] + FVector(0, 0, FMath::Sin(Clock * 2.f + Index) * 22.f);
        if (!IsCollectibleCollected(Index) && FVector::Dist2D(PlayerLocation, Point) < 10000.f)
        {
            DrawDebugSphere(World, Point, 60.f, 12, FColor(255, 219, 113), false, 0.f, 0, 5.f);
            DrawDebugLine(World, Point + FVector(0, 0, 65), Point + FVector(0, 0, 280), FColor(255, 219, 113), false, 0.f, 0, 3.f);
        }
    }
}
