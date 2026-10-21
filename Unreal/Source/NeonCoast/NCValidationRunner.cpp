#include "NCValidationRunner.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCPedestrian.h"
#include "NCGameplaySubsystem.h"
#include "NCSaveGame.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInterface.h"

ANCValidationRunner::ANCValidationRunner()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
}
void ANCValidationRunner::Check(bool Passed,const TCHAR* Name)
{
    ++Checks;
    if(!Passed) ++Failures;
    UE_LOG(LogTemp,Display,TEXT("NC_CHECK %s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),Name);
}
void ANCValidationRunner::Advance(int32 NextPhase)
{
    Phase=NextPhase;
    PhaseClock=0.f;
}
bool ANCValidationRunner::SetUpCombat(APlayerController* PC)
{
    Hero->EndFire(); Hero->EndAim(); Hero->StopJumping();
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->SetActorLocationAndRotation(FVector(21600,-18000,100),FRotator(0,90,0),false,nullptr,ETeleportType::TeleportPhysics);
    PC->SetControlRotation(FRotator(0,90,0));
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Target=GetWorld()->SpawnActor<ANCPedestrian>(FVector(21600,-17100,88),FRotator(0,-90,0),Params);
    if(!Target) return false;
    // Keep the actual damage target still; the ambient pedestrians were tested walking.
    Target->GetCharacterMovement()->StopMovementImmediately();
    Target->GetCharacterMovement()->DisableMovement();
    Target->SetActorTickEnabled(false);
    Hero->BeginAim();
    return true;
}
void ANCValidationRunner::AimAtTarget(APlayerController* PC)
{
    if(IsValid(Target) && IsValid(Hero) && PC)
        PC->SetControlRotation((Target->GetActorLocation()-Hero->Camera->GetComponentLocation()).Rotation());
}
bool ANCValidationRunner::SetUpMuzzleWall(APlayerController* PC)
{
    FVector CameraPosition; FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraPosition,CameraRotation);
    const FVector View=CameraRotation.Vector();
    const FVector Muzzle=Hero->GetActorLocation()+FVector(0,0,48)+View*55.f+CameraRotation.RotateVector(FVector(0,18,0));
    const FVector WallPosition=FMath::Lerp(Muzzle,Target->GetActorLocation(),.17f);
    MuzzleWall=GetWorld()->SpawnActor<AActor>();
    if(!MuzzleWall) return false;
    auto* Box=NewObject<UBoxComponent>(MuzzleWall,TEXT("SmokeMuzzleOccluder"));
    MuzzleWall->SetRootComponent(Box);
    Box->InitBoxExtent(FVector(14,10,130));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->SetCanEverAffectNavigation(false);
    Box->RegisterComponent();
    MuzzleWall->AddInstanceComponent(Box);
    MuzzleWall->SetActorLocationAndRotation(WallPosition,FRotator(0,CameraRotation.Yaw,0));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NeonCoastSmokeCover),false,Hero);
    FHitResult CameraHit,MuzzleHit;
    const bool CameraSeesTarget=GetWorld()->LineTraceSingleByChannel(CameraHit,CameraPosition,Target->GetActorLocation(),ECC_Visibility,Query)
        && CameraHit.GetActor()==Target;
    const bool MuzzleHitsWall=GetWorld()->LineTraceSingleByChannel(MuzzleHit,Muzzle,Target->GetActorLocation(),ECC_Visibility,Query)
        && MuzzleHit.GetActor()==MuzzleWall;
    Check(CameraSeesTarget,TEXT("cover fixture leaves shoulder camera line of sight clear"));
    Check(MuzzleHitsWall,TEXT("cover fixture blocks the physical muzzle path"));
    return CameraSeesTarget && MuzzleHitsWall;
}
void ANCValidationRunner::PlaceCar(const FVector& RoadPoint)
{
    // Teleport only travel between fixtures. Control, possession, interaction,
    // suspension and all mission/race advancement still run through the game.
    Car->SetDrivingInput(0.f,0.f,true);
    Car->SetActorLocationAndRotation(FVector(RoadPoint.X,RoadPoint.Y,85),FRotator(0,90,0),false,nullptr,ETeleportType::TeleportPhysics);
    Car->GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Car->GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    if(auto* Vehicle=Cast<UChaosWheeledVehicleMovementComponent>(Car->GetVehicleMovementComponent()))
    {
        Vehicle->ResetVehicle();
        Vehicle->SetSleeping(false);
    }
}
void ANCValidationRunner::CheckEarnedProgressSerialization(UNCGameplaySubsystem* Gameplay)
{
    auto* Snapshot=NewObject<UNCSaveGame>(this);
    Snapshot->Cash=Gameplay->GetCash();
    Snapshot->CompletedDeliveries=Gameplay->GetCompletedDeliveries();
    Snapshot->RaceWins=Gameplay->GetRaceWins();
    Snapshot->BestRaceSeconds=Gameplay->GetBestRaceTime();
    Snapshot->MissionStage=Gameplay->GetMissionStage();
    Snapshot->LastCrimeLocation=Gameplay->GetLastCrimeLocation();
    for(int32 I=0;I<Gameplay->GetCollectibleLocations().Num();++I)
        if(Gameplay->IsCollectibleCollected(I)) Snapshot->CollectedMask|=1<<I;
    Check(Gameplay->GetWantedLevel()==0,TEXT("earned-progress save fixture has no active search"));
    Check(Snapshot->IsSane(),TEXT("actual earned mission/race/token progress satisfies save validation"));
    TArray<uint8> Bytes;
    const bool Saved=UGameplayStatics::SaveGameToMemory(Snapshot,Bytes);
    Check(Saved && Bytes.Num()>0,TEXT("earned progress serializes to memory without a disk save slot"));
    auto* Restored=Saved?Cast<UNCSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)):nullptr;
    Check(Restored && Restored->IsSane(),TEXT("serialized earned progress restores to a valid save class"));
    if(Restored)
    {
        Check(Restored->Cash==Gameplay->GetCash(),TEXT("earned courier/race/token money survives serialization"));
        Check(Restored->CompletedDeliveries==Gameplay->GetCompletedDeliveries() && Restored->RaceWins==Gameplay->GetRaceWins(),TEXT("earned job and race counters survive serialization"));
        Check(Restored->BestRaceSeconds==Gameplay->GetBestRaceTime() && Restored->CollectedMask==Snapshot->CollectedMask,TEXT("timed race record and token collection survive serialization"));
    }
}
void ANCValidationRunner::Tick(float Dt)
{
    Super::Tick(Dt);
    if(Phase==99) return;
    Clock+=Dt; PhaseClock+=Dt;
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>();
    if(Clock>85.f)
    {
        Check(false,TEXT("runtime smoke test completed within 85 seconds"));
        Finish(); return;
    }
    if(Phase>0 && (!IsValid(Hero) || !IsValid(Car) || !PC || !Gameplay))
    {
        Check(false,TEXT("required gameplay actors remain valid"));
        Finish(); return;
    }
    switch(Phase)
    {
    case 0:
        if(PhaseClock<1.f) break;
        Hero=PC?Cast<ANCHero>(PC->GetPawn()):nullptr;
        Check(IsValid(Hero),TEXT("player starts on foot"));
        Check(Gameplay!=nullptr,TEXT("gameplay subsystem exists"));
        if(!Hero || !PC || !Gameplay) { Finish(); return; }
        {
            int32 Cars=0; float Nearest=MAX_flt;
            for(TActorIterator<ANCCar> It(GetWorld());It;++It)
            {
                // The smoke command disables police; these are ambient/base vehicles.
                if(It->IsPoliceVehicle()) continue;
                ++Cars;
                const float Distance=FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation());
                if(Distance<Nearest) { Nearest=Distance; Car=*It; }
            }
            for(TActorIterator<ANCPedestrian> It(GetWorld());It;++It)
                PedestrianStart.Add(*It,It->GetActorLocation());
            Check(Cars>=9,TEXT("at least nine base vehicles spawned"));
            Check(PedestrianStart.Num()>=64,TEXT("at least 64 ambient pedestrians spawned"));
        }
        Check(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Coast.M_Coast"))!=nullptr,TEXT("authored coast material loads"));
        Check(!Gameplay->IsPersistenceEnabled(),TEXT("smoke test cannot overwrite player saves"));
        if(!Car) { Check(false,TEXT("initial vehicle is available")); Finish(); return; }
        // Apply controls after input is consumed, before vehicle and character physics.
        AddTickPrerequisiteActor(PC);
        Car->AddTickPrerequisiteActor(this);
        Hero->AddTickPrerequisiteActor(this);
        Hero->GetCharacterMovement()->AddTickPrerequisiteActor(this);
        Advance(1);
        break;
    case 1:
        if(PhaseClock<4.f) break;
        {
            int32 Moving=0;
            for(const auto& Pair:PedestrianStart)
                if(Pair.Key.IsValid() && FVector::Dist2D(Pair.Key->GetActorLocation(),Pair.Value)>50.f) ++Moving;
            Check(Moving>=40,TEXT("at least 40 ambient pedestrians walk more than 50 cm"));
            UE_LOG(LogTemp,Display,TEXT("NC_METRIC walkingPedestrians=%d totalPedestrians=%d"),Moving,PedestrianStart.Num());
        }
        Check(Car->CanEnter(),TEXT("settled initial vehicle can be entered"));
        Hero->EnterCar(Car);
        Check(PC->GetPawn()==Car && Car->GetDriverHero()==Hero && Hero->IsHidden(),TEXT("vehicle entry transfers possession and hides hero"));
        if(PC->GetPawn()!=Car) { Finish(); return; }
        LastZ=Car->GetActorLocation().Z;
        Advance(2);
        break;
    case 2:
        Car->SetDrivingInput(0.f,0.f,true);
        if(PhaseClock>.75f)
        {
            MinIdleZ=FMath::Min(MinIdleZ,float(Car->GetActorLocation().Z));
            MaxIdleZ=FMath::Max(MaxIdleZ,float(Car->GetActorLocation().Z));
        }
        if(PhaseClock<1.75f) break;
        {
            auto* Vehicle=Cast<UChaosWheeledVehicleMovementComponent>(Car->GetVehicleMovementComponent());
            Check(Car->GetMesh() && Car->GetMesh()->IsSimulatingPhysics(),TEXT("vehicle skeletal body runs rigid-body physics"));
            Check(Vehicle && Vehicle->GetNumWheels()==4,TEXT("Chaos creates four physical wheel states"));
            int32 Contacts=0;
            if(Vehicle) for(int32 I=0;I<Vehicle->GetNumWheels();++I)
                if(Vehicle->GetWheelState(I).bInContact) ++Contacts;
            Check(Contacts>=3,TEXT("settled vehicle has at least three grounded wheels"));
        }
        Check(MaxIdleZ-MinIdleZ<8.f,TEXT("settled parked suspension stays within an 8 cm vertical range"));
        DriveStart=Car->GetActorLocation(); LastZ=DriveStart.Z;
        Advance(3);
        break;
    case 3:
        Car->SetDrivingInput(.8f,0.f,false);
        PeakSpeed=FMath::Max(PeakSpeed,Car->GetSpeedKmh());
        DriveDistance=FVector::Dist2D(DriveStart,Car->GetActorLocation());
        if(PhaseClock>1.f)
        {
            const float Z=Car->GetActorLocation().Z;
            MaxVerticalStep=FMath::Max(MaxVerticalStep,FMath::Abs(Z-LastZ));
            MaxVerticalRate=FMath::Max(MaxVerticalRate,FMath::Abs(Z-LastZ)/FMath::Max(Dt,.001f));
            MinDriveZ=FMath::Min(MinDriveZ,Z); MaxDriveZ=FMath::Max(MaxDriveZ,Z);
        }
        LastZ=Car->GetActorLocation().Z;
        if((PhaseClock<3.5f || Car->GetSpeedKmh()<45.f) && PhaseClock<9.f) break;
        Check(Car->GetSpeedKmh()>35.f,TEXT("real world physics accelerates car above 35 km/h"));
        Check(DriveDistance>1200.f,TEXT("driving moves car more than 12 metres"));
        Check(MaxVerticalStep<20.f && MaxDriveZ-MinDriveZ<35.f,TEXT("flat-road driving has no large suspension jumps"));
        HealthBeforeExit=Hero->GetHealth();
        Car->SetDrivingInput(0.f,0.f,false);
        Car->ExitCar();
        Check(PC->GetPawn()==Hero,TEXT("moving vehicle exit returns possession to hero"));
        if(PC->GetPawn()!=Hero) { Finish(); return; }
        Check(!Hero->IsHidden() && Hero->GetActorEnableCollision(),TEXT("bailout restores a visible colliding character"));
        Check(Hero->IsRecovering() && Hero->GetHealth()<HealthBeforeExit,TEXT("high-speed bailout applies recovery and damage"));
        ExitPosition=Hero->GetActorLocation();
        Advance(4);
        break;
    case 4:
        PeakBailoutSpeed=FMath::Max(PeakBailoutSpeed,float(Hero->GetVelocity().Size2D()));
        if(PhaseClock<2.5f) break;
        if(!Hero->GetCharacterMovement()->IsMovingOnGround() && PhaseClock<5.f) break;
        Check(PeakBailoutSpeed>300.f && FVector::Dist2D(ExitPosition,Hero->GetActorLocation())>100.f,TEXT("bailout transfers vehicle momentum into character movement"));
        Check(!Hero->IsRecovering(),TEXT("bailout recovery expires"));
        Check(Hero->GetCharacterMovement()->IsMovingOnGround(),TEXT("hero lands on walkable ground after bailout"));
        JumpStartZ=MaxJumpZ=Hero->GetActorLocation().Z;
        Hero->TryJump();
        Advance(5);
        break;
    case 5:
        MaxJumpZ=FMath::Max(MaxJumpZ,float(Hero->GetActorLocation().Z));
        if(PhaseClock>.25f) Hero->StopJumping();
        if(PhaseClock<1.4f) break;
        Check(MaxJumpZ-JumpStartZ>60.f,TEXT("on-foot jump rises more than 60 cm"));
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Gameplay->GetDepotLocation()+FVector(0,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->Greet();
        Check(Gameplay->GetMissionStage()==1,TEXT("depot interaction starts a real job before the defeat test"));
        Check(SetUpCombat(PC),TEXT("combat fixture spawns an actual pedestrian target"));
        if(!Target) { Finish(); return; }
        Advance(6);
        break;
    case 6:
        AimAtTarget(PC);
        if(PhaseClock<1.5f) break;
        Check(Hero->IsAiming(),TEXT("aim input enters shoulder aim state"));
        AmmoBefore=Hero->GetAmmo(); TargetHealthBefore=Target->GetHealth();
        Hero->BeginFire(); Hero->EndFire();
        Advance(7);
        break;
    case 7:
        AimAtTarget(PC);
        if(PhaseClock<.35f) break;
        Check(Hero->GetAmmo()==AmmoBefore-1,TEXT("a single shot consumes exactly one round"));
        Check(Target->GetHealth()<TargetHealthBefore && !Target->IsIncapacitated(),TEXT("camera and muzzle hitscan damages a living pedestrian"));
        Check(Target->IsPanicking(),TEXT("shot victim enters panic state"));
        Check(Gameplay->GetWantedLevel()>0,TEXT("firing and injury trigger a wanted response"));
        AmmoBefore=Hero->GetAmmo(); ReserveBefore=Hero->GetReserveAmmo();
        Hero->Reload();
        Check(Hero->IsReloading(),TEXT("reload starts with a partially spent magazine"));
        Advance(8);
        break;
    case 8:
        AimAtTarget(PC);
        if(PhaseClock<1.6f) break;
        Check(!Hero->IsReloading() && Hero->GetAmmo()==12,TEXT("reload completes after elapsed world time"));
        Check(Hero->GetReserveAmmo()==ReserveBefore-(12-AmmoBefore),TEXT("reload transfers rounds from finite reserve"));
        if(!SetUpMuzzleWall(PC))
        {
            Check(false,TEXT("muzzle obstruction fixture is geometrically valid"));
            Finish(); return;
        }
        AmmoBefore=Hero->GetAmmo(); TargetHealthBefore=Target->GetHealth();
        Hero->BeginFire(); Hero->EndFire();
        Advance(9);
        break;
    case 9:
        if(PhaseClock<.35f) break;
        Check(Hero->GetAmmo()==AmmoBefore-1,TEXT("blocked shot still consumes ammunition"));
        Check(FMath::IsNearlyEqual(Target->GetHealth(),TargetHealthBefore),TEXT("muzzle wall prevents damage despite a clear camera view"));
        MuzzleWall->Destroy(); MuzzleWall=nullptr;
        Target->Destroy(); Target=nullptr;
        Hero->EndAim(); Hero->EndFire();
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Gameplay->GetClinicLocation()+FVector(0,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        CashBefore=Gameplay->GetCash();
        Check(Hero->GetHealth()<100.f && CashBefore>=75,TEXT("clinic test has an injured funded player"));
        Hero->Greet();
        Check(Hero->GetHealth()==100.f && Gameplay->GetCash()==CashBefore-75,TEXT("E interaction at clinic heals player and charges 75 dollars"));
        CashBefore=Gameplay->GetCash();
        Hero->Greet();
        Check(Gameplay->GetCash()==CashBefore,TEXT("clinic does not charge an already healthy player"));
        CashBefore=Gameplay->GetCash(); AmmoBefore=Hero->GetAmmo();
        UGameplayStatics::ApplyDamage(Hero,150.f,PC,this,UDamageType::StaticClass());
        Check(Hero->IsDefeated() && Hero->IsRecovering(),TEXT("lethal damage starts the recovery state"));
        Hero->BeginAim(); Hero->BeginFire(); Hero->EndFire();
        Check(!Hero->IsAiming() && Hero->GetAmmo()==AmmoBefore,TEXT("defeated player cannot aim or fire"));
        Advance(10);
        break;
    case 10:
        if(PhaseClock<3.5f) break;
        Check(!Hero->IsDefeated() && !Hero->IsRecovering() && Hero->GetHealth()==100.f,TEXT("death recovery restores health after three seconds"));
        Check(PC->GetPawn()==Hero && !Hero->IsHidden() && FVector::Dist2D(Hero->GetActorLocation(),FVector(21900,3150,0))<100.f,TEXT("death recovery returns a playable hero to the safe waterfront"));
        Check(Gameplay->GetCash()==FMath::Max(0,CashBefore-125),TEXT("death recovery charges the bounded medical fee once"));
        Check(Gameplay->GetWantedLevel()==0,TEXT("medical recovery clears wanted heat"));
        Check(Gameplay->GetMissionStage()==0,TEXT("medical recovery resets the active courier job"));
        Hero->Greet();
        Check(Gameplay->GetMissionStage()==1 && Gameplay->HasObjective(),TEXT("E interaction at the waterfront accepts a courier job"));
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Gameplay->GetObjectiveLocation()+FVector(0,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->Greet();
        Check(Gameplay->GetMissionStage()==1,TEXT("courier drop rejects delivery on foot"));
        Check(!Gameplay->TryInteract(Car),TEXT("an unpossessed car cannot remotely trigger a service or delivery"));
        PlaceCar(Gameplay->GetObjectiveLocation());
        Hero->SetActorLocation(Car->GetActorLocation()+Car->GetActorRightVector()*330.f+FVector(0,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        Advance(11);
        break;
    case 11:
        Car->SetDrivingInput(0.f,0.f,true);
        if(PhaseClock<1.5f) break;
        Hero->TryEnterCar();
        Check(PC->GetPawn()==Car,TEXT("nearby interact enters the delivery vehicle"));
        if(PC->GetPawn()!=Car) { Finish(); return; }
        CashBefore=Gameplay->GetCash(); DeliveriesBefore=Gameplay->GetCompletedDeliveries();
        Car->TryService();
        Check(Gameplay->GetMissionStage()==2 && Gameplay->GetWantedLevel()==2,TEXT("stopped vehicle delivery starts the two-star courier search"));
        Check(Gameplay->GetCash()==CashBefore,TEXT("courier payment is withheld until search and return complete"));
        SearchStartedAt=Clock; bCheckedSearchDelay=false;
        Advance(12);
        break;
    case 12:
        Car->SetDrivingInput(0.f,0.f,true);
        if(!bCheckedSearchDelay && PhaseClock>=6.f)
        {
            Check(Gameplay->GetWantedLevel()==2 && Gameplay->GetWantedSearchProgress()<1.f,TEXT("crime heat cannot clear before the real search delay"));
            Check(!Gameplay->IsPoliceSeeingPlayer(),TEXT("isolated search test has no officer sight reports"));
            bCheckedSearchDelay=true;
        }
        if(Gameplay->GetWantedLevel()>0 && PhaseClock<33.f) break;
        SearchDuration=Clock-SearchStartedAt;
        Check(Gameplay->GetWantedLevel()==0 && SearchDuration>=26.f && SearchDuration<33.f,TEXT("courier heat decays through elapsed world time with no police sight"));
        Check(Gameplay->GetWantedSearchProgress()==1.f && Gameplay->GetMissionStage()==3,TEXT("cleared search advances courier job to the return stage"));
        PlaceCar(Gameplay->GetDepotLocation());
        Advance(13);
        break;
    case 13:
        Car->SetDrivingInput(0.f,0.f,true);
        if(PhaseClock<1.25f) break;
        Car->TryService();
        Check(Gameplay->GetMissionStage()==0 && Gameplay->GetCompletedDeliveries()==DeliveriesBefore+1,TEXT("return interaction completes one courier job"));
        Check(Gameplay->GetCash()==CashBefore+750,TEXT("completed courier route pays exactly 750 dollars"));
        CashBefore=Gameplay->GetCash(); RaceWinsBefore=Gameplay->GetRaceWins();
        PlaceCar(Gameplay->GetRaceStartLocation());
        Advance(14);
        break;
    case 14:
        Car->SetDrivingInput(0.f,0.f,true);
        if(PhaseClock<1.25f) break;
        Car->TryService();
        Check(Gameplay->IsRaceActive() && Gameplay->GetRaceCheckpointIndex()==0,TEXT("stopped car interaction starts a fresh street circuit"));
        Check(Gameplay->GetRaceGates().Num()==8,TEXT("street circuit exposes eight ordered gates"));
        if(!Gameplay->IsRaceActive() || Gameplay->GetRaceGates().Num()!=8) { Finish(); return; }
        RaceStartedAt=Clock;
        PlaceCar(Gameplay->GetRaceGates()[1]);
        Advance(15);
        break;
    case 15:
        Car->SetDrivingInput(0.f,0.f,true);
        if(PhaseClock<.4f) break;
        Check(Gameplay->GetRaceCheckpointIndex()==0,TEXT("visiting a later race gate does not skip the first gate"));
        Car->ExitCar();
        Check(PC->GetPawn()==Hero,TEXT("stopped race car can be exited"));
        if(PC->GetPawn()!=Hero) { Finish(); return; }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Gameplay->GetRaceGates()[0]+FVector(0,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        Advance(16);
        break;
    case 16:
        Car->SetDrivingInput(0.f,0.f,true);
        if(PhaseClock<.4f) break;
        Check(Gameplay->IsRaceActive() && Gameplay->GetRaceCheckpointIndex()==0,TEXT("walking through the correct gate does not advance a vehicle race"));
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Car->GetActorLocation()+Car->GetActorRightVector()*330.f+FVector(0,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->TryEnterCar();
        Check(PC->GetPawn()==Car,TEXT("race resumes after entering the nearby car"));
        if(PC->GetPawn()!=Car) { Finish(); return; }
        ExpectedGate=0;
        PlaceCar(Gameplay->GetRaceGates()[ExpectedGate]);
        Advance(17);
        break;
    case 17:
        Car->SetDrivingInput(0.f,0.f,true);
        if(PhaseClock<.45f) break;
        Check(Gameplay->GetRaceCheckpointIndex()==ExpectedGate+1,*FString::Printf(TEXT("ordered race gate %d advances exactly one checkpoint through world Tick"),ExpectedGate+1));
        ++ExpectedGate;
        if(ExpectedGate<Gameplay->GetRaceGates().Num())
        {
            PlaceCar(Gameplay->GetRaceGates()[ExpectedGate]);
            Advance(17);
            break;
        }
        Check(!Gameplay->IsRaceActive() && Gameplay->GetRaceWins()==RaceWinsBefore+1,TEXT("all eight ordered gates finish one street race"));
        Check(Gameplay->GetCash()==CashBefore+450,TEXT("completed street circuit pays exactly 450 dollars"));
        Check(Gameplay->GetBestRaceTime()>0.f && Gameplay->GetBestRaceTime()<95.f && FMath::Abs(Gameplay->GetBestRaceTime()-(Clock-RaceStartedAt))<.6f,TEXT("successful circuit records its actual elapsed world time"));
        Car->ExitCar();
        Check(PC->GetPawn()==Hero,TEXT("player can return to exploration after a race"));
        if(PC->GetPawn()!=Hero) { Finish(); return; }
        CashBefore=Gameplay->GetCash(); TokensBefore=Gameplay->GetCollectibleCount(); TokenIndex=0;
        while(TokenIndex<Gameplay->GetCollectibleLocations().Num() && Gameplay->IsCollectibleCollected(TokenIndex)) ++TokenIndex;
        if(TokenIndex>=Gameplay->GetCollectibleLocations().Num())
        {
            Check(false,TEXT("collection fixture begins with an uncollected token"));
            Finish(); return;
        }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(Gameplay->GetCollectibleLocations()[TokenIndex],false,nullptr,ETeleportType::TeleportPhysics);
        Advance(18);
        break;
    case 18:
        if(PhaseClock<.25f) break;
        Check(Gameplay->IsCollectibleCollected(TokenIndex),*FString::Printf(TEXT("proximity collects coast token %d through world Tick"),TokenIndex+1));
        ++TokenIndex;
        while(TokenIndex<Gameplay->GetCollectibleLocations().Num() && Gameplay->IsCollectibleCollected(TokenIndex)) ++TokenIndex;
        if(TokenIndex<Gameplay->GetCollectibleLocations().Num())
        {
            Hero->GetCharacterMovement()->StopMovementImmediately();
            Hero->SetActorLocation(Gameplay->GetCollectibleLocations()[TokenIndex],false,nullptr,ETeleportType::TeleportPhysics);
            Advance(18);
            break;
        }
        Check(Gameplay->GetCollectibleCount()==8,TEXT("all eight distinct coast tokens are recorded"));
        Check(Gameplay->GetCash()==CashBefore+(8-TokensBefore)*75+400,TEXT("tokens pay 75 each plus the single 400 dollar completion bonus"));
        CashBefore=Gameplay->GetCash();
        Advance(19);
        break;
    case 19:
        if(PhaseClock<.5f) break;
        Check(Gameplay->GetCash()==CashBefore && Gameplay->GetCollectibleCount()==8,TEXT("remaining at a collected token cannot duplicate its reward"));
        CheckEarnedProgressSerialization(Gameplay);
        UE_LOG(LogTemp,Display,TEXT("NC_METRIC courierSearchSeconds=%.2f deliveries=%d raceWins=%d raceBestSeconds=%.3f tokens=%d cash=%d"),
            SearchDuration,Gameplay->GetCompletedDeliveries(),Gameplay->GetRaceWins(),Gameplay->GetBestRaceTime(),Gameplay->GetCollectibleCount(),Gameplay->GetCash());
        Finish();
        break;
    default:
        Check(false,TEXT("smoke test phase is valid")); Finish(); break;
    }
}
void ANCValidationRunner::Finish()
{
    Phase=99;
    if(IsValid(MuzzleWall)) MuzzleWall->Destroy();
    if(IsValid(Target)) Target->Destroy();
    const float ParkedRange=MaxIdleZ>=MinIdleZ?MaxIdleZ-MinIdleZ:0.f;
    const float DrivingRange=MaxDriveZ>=MinDriveZ?MaxDriveZ-MinDriveZ:0.f;
    UE_LOG(LogTemp,Display,TEXT("NC_METRIC peakSpeedKmh=%.2f driveDistanceCm=%.2f parkedRangeCm=%.3f drivingRangeCm=%.3f maxVerticalStepCm=%.3f maxVerticalRateCmS=%.3f bailoutSpeedCmS=%.2f jumpHeightCm=%.2f worldSeconds=%.2f"),
        PeakSpeed,DriveDistance,ParkedRange,DrivingRange,MaxVerticalStep,MaxVerticalRate,PeakBailoutSpeed,MaxJumpZ-JumpStartZ,Clock);
    UE_LOG(LogTemp,Display,TEXT("NEON_COAST_SMOKE_%s checks=%d failures=%d maxVerticalStepCm=%.4f"),Failures?TEXT("FAIL"):TEXT("PASS"),Checks,Failures,MaxVerticalStep);
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
