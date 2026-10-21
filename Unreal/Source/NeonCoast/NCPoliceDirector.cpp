#include "NCPoliceDirector.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCGameplaySubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

namespace
{
    bool IsAwayAndUnobserved(UWorld* World,APawn* Player,ANCCar* Car)
    {
        if(!Player || !Car || Car->WasPlayerDriven() || Car->GetDriverHero() || FVector::Dist2D(Player->GetActorLocation(),Car->GetActorLocation())<6500.f) return false;
        auto* PC=UGameplayStatics::GetPlayerController(World,0);
        if(!PC || !PC->PlayerCameraManager) return false;
        const FVector Camera=PC->PlayerCameraManager->GetCameraLocation();
        const FVector ToCar=Car->GetActorLocation()+FVector(0,0,80)-Camera;
        if(FVector::DotProduct(ToCar.GetSafeNormal(),PC->PlayerCameraManager->GetCameraRotation().Vector())<.15f) return true;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceRetirementVisibility),false,Player);
        Params.AddIgnoredActor(Car);
        if(auto* Occupied=Cast<ANCCar>(Player)) if(Occupied->GetDriverHero()) Params.AddIgnoredActor(Occupied->GetDriverHero());
        FHitResult Block;
        return World->LineTraceSingleByChannel(Block,Camera,Camera+ToCar,ECC_WorldStatic,Params);
    }
    TArray<FVector> LocalPatrol(const FVector& Location)
    {
        const float X=FMath::Clamp(FMath::FloorToFloat(Location.X/7200.f)*7200.f,-21600.f,14400.f);
        const float Y=FMath::Clamp(FMath::FloorToFloat(Location.Y/7200.f)*7200.f,-21600.f,14400.f);
        const TArray<FVector> Loop={FVector(X+350,Y+350,0),FVector(X+350,Y+6850,0),
            FVector(X+6850,Y+6850,0),FVector(X+6850,Y+350,0)};
        int32 Nearest=0;
        for(int32 I=1;I<Loop.Num();++I)
            if(FVector::DistSquared2D(Location,Loop[I])<FVector::DistSquared2D(Location,Loop[Nearest])) Nearest=I;
        TArray<FVector> Result;
        for(int32 I=0;I<Loop.Num();++I) Result.Add(Loop[(Nearest+I)%Loop.Num()]);
        return Result;
    }
}

ANCPoliceDirector::ANCPoliceDirector()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=.2f;
}

ANCCar* ANCPoliceDirector::SpawnUnit(APawn* Target,const FVector& DispatchLocation)
{
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    const FVector PlayerPosition=Target->GetActorLocation();
    const FVector Camera=PC && PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraLocation():PlayerPosition;
    const FVector View=PC && PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraRotation().Vector():FVector::ForwardVector;
    TSet<FIntPoint> Tried;
    for(int32 Attempt=0;Attempt<16;++Attempt)
    {
        const float Angle=(SpawnSerial+Attempt)*PI/4.f;
        const float Radius=Attempt<8?7200.f:14400.f;
        // Dispatch is based on a reported crime or sighting. The current player
        // position is used only to avoid visibly creating a car beside the player.
        FVector Spot=DispatchLocation+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius;
        const int32 GridX=FMath::Clamp(FMath::RoundToInt(Spot.X/7200.f),-3,3);
        const int32 GridY=FMath::Clamp(FMath::RoundToInt(Spot.Y/7200.f),-3,3);
        const FIntPoint Grid(GridX,GridY);
        if(Tried.Contains(Grid)) continue;
        Tried.Add(Grid);
        Spot=FVector(GridX*7200.f+350.f,GridY*7200.f+350.f,0.f);
        if(FVector::Dist2D(PlayerPosition,Spot)<4500.f) continue;
        if(FVector::DotProduct((Spot-Camera).GetSafeNormal(),View)>.55f) continue;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceSpawn),false,Target);
        FHitResult Ground;
        if(!GetWorld()->LineTraceSingleByChannel(Ground,Spot+FVector(0,0,600),Spot-FVector(0,0,200),ECC_WorldStatic,Params) || Ground.ImpactNormal.Z<.8f) continue;
        Spot.Z=Ground.ImpactPoint.Z+85.f;
        const FVector Toward=DispatchLocation-Spot;
        const float Yaw=FMath::Abs(Toward.X)>FMath::Abs(Toward.Y)?(Toward.X>0.f?0.f:180.f):(Toward.Y>0.f?90.f:-90.f);
        const FRotator Facing(0,Yaw,0);
        if(GetWorld()->OverlapBlockingTestByChannel(Spot+FVector(0,0,45),Facing.Quaternion(),ECC_Vehicle,
            FCollisionShape::MakeBox(FVector(260,125,65)),Params)) continue;
        const FTransform Transform(Facing,Spot);
        auto* Unit=GetWorld()->SpawnActorDeferred<ANCCar>(ANCCar::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(Unit)
        {
            Unit->SetPolice(true);
            Unit->SetPursuitLocation(DispatchLocation);
            UGameplayStatics::FinishSpawningActor(Unit,Transform);
            ++SpawnSerial;
        }
        return Unit;
    }
    return nullptr;
}

void ANCPoliceDirector::RetireUnit(ANCCar* Unit)
{
    if(!IsValid(Unit) || Unit->WasPlayerDriven() || Unit->GetDriverHero()) return;
    Unit->SetRoute(Unit->GetCondition()>0.f?LocalPatrol(Unit->GetActorLocation()):TArray<FVector>());
    RetiringUnits.AddUnique(Unit);
    RetiredAt.FindOrAdd(TWeakObjectPtr<ANCCar>(Unit))=GetWorld()->GetTimeSeconds();
}

void ANCPoliceDirector::CleanRetiredUnits(APawn* Player)
{
    const float Now=GetWorld()->GetTimeSeconds();
    for(int32 I=RetiringUnits.Num()-1;I>=0;--I)
    {
        ANCCar* Unit=RetiringUnits[I];
        const TWeakObjectPtr<ANCCar> Key(Unit);
        if(!IsValid(Unit) || Unit->WasPlayerDriven() || Unit->GetDriverHero())
        {
            // Ownership is handed to the player permanently. Never delete or
            // repossess this car later after the player gets out of it again.
            RetiredAt.Remove(Key);
            RetiringUnits.RemoveAtSwap(I);
            continue;
        }
        const float* Since=RetiredAt.Find(Key);
        if(Since && Now-*Since>45.f && IsAwayAndUnobserved(GetWorld(),Player,Unit))
        {
            Unit->Destroy();
            RetiredAt.Remove(Key);
            RetiringUnits.RemoveAtSwap(I);
        }
    }
}

void ANCPoliceDirector::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* State=GetWorld()->GetSubsystem<UNCGameplaySubsystem>();
    APawn* Target=UGameplayStatics::GetPlayerPawn(this,0);
    if(!State || !Target) return;
    CleanRetiredUnits(Target);
    for(int32 I=Units.Num()-1;I>=0;--I)
    {
        ANCCar* Unit=Units[I];
        if(!IsValid(Unit) || Unit->WasPlayerDriven() || Unit->GetDriverHero()) Units.RemoveAtSwap(I);
        else if(Unit->GetCondition()<=0.f)
        {
            RetireUnit(Unit);
            Units.RemoveAtSwap(I);
        }
    }
    const int32 Wanted=State->GetWantedLevel();
    SpawnClock+=Dt;
    AttackClock+=Dt;
    if(Wanted<=0)
    {
        CalmClock+=Dt;
        for(ANCCar* Unit:Units) RetireUnit(Unit);
        Units.Reset();
        LastSeen=FVector::ZeroVector;
        State->ReportPoliceSight(false);
        return;
    }
    CalmClock=0.f;
    // This field changes only on a crime or a real sight report, never simply
    // because the target moved while hidden. (0,0,0) is a valid reported location.
    LastSeen=State->GetLastCrimeLocation();
    const int32 DesiredUnits=Wanted+1;
    while(Units.Num()>DesiredUnits)
    {
        RetireUnit(Units.Last());
        Units.Pop();
    }
    // Healthy patrols can answer a new dispatch; wrecks and player-owned vehicles
    // remain outside the response budget so they cannot prevent reinforcements.
    for(int32 I=RetiringUnits.Num()-1;I>=0 && Units.Num()<DesiredUnits;--I)
    {
        ANCCar* Unit=RetiringUnits[I];
        if(IsValid(Unit) && !Unit->WasPlayerDriven() && !Unit->GetDriverHero() && Unit->GetCondition()>0.f)
        {
            Unit->SetPursuitLocation(LastSeen);
            Units.Add(Unit);
            RetiredAt.Remove(TWeakObjectPtr<ANCCar>(Unit));
            RetiringUnits.RemoveAtSwap(I);
        }
    }
    if(Units.Num()<DesiredUnits && SpawnClock>3.f)
    {
        SpawnClock=0.f;
        if(auto* Unit=SpawnUnit(Target,LastSeen)) Units.Add(Unit);
    }
    bool Seen=false;
    for(ANCCar* Unit:Units)
    {
        if(!IsValid(Unit) || Unit->GetCondition()<=0.f || Unit->GetDriverHero()) continue;
        const FVector To=Target->GetActorLocation()-Unit->GetActorLocation();
        bool UnitSees=false;
        if(To.Size2D()<8500.f && (To.Size2D()<2200.f || FVector::DotProduct(To.GetSafeNormal2D(),Unit->GetActorForwardVector())>-.1f))
        {
            FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceSight),false,Unit);
            Params.AddIgnoredActor(Target);
            if(auto* PlayerCar=Cast<ANCCar>(Target)) if(PlayerCar->GetDriverHero()) Params.AddIgnoredActor(PlayerCar->GetDriverHero());
            FHitResult Hit;
            UnitSees=!GetWorld()->LineTraceSingleByChannel(Hit,Unit->GetActorLocation()+FVector(0,0,110),Target->GetActorLocation()+FVector(0,0,60),ECC_Visibility,Params);
        }
        if(UnitSees)
        {
            Seen=true;
            LastSeen=Target->GetActorLocation();
            Unit->SetPursuitTarget(Target);
            if(To.Size2D()<1900.f && AttackClock>1.7f && Wanted>=2)
            {
                AttackClock=0.f;
                UGameplayStatics::ApplyDamage(Target,Wanted*3.f,Unit->GetController(),Unit,nullptr);
                DrawDebugLine(GetWorld(),Unit->GetActorLocation()+FVector(0,0,100),Target->GetActorLocation()+FVector(0,0,30),FColor(255,185,85),false,.06f,0,1.f);
            }
        }
        else Unit->SetPursuitLocation(LastSeen);
    }
    State->ReportPoliceSight(Seen);
}
