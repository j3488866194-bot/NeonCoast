#include "NCGameMode.h"
#include "NCCityVisual.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCPedestrian.h"
#include "NCPlayerController.h"
#include "NCHUD.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "NCValidationRunner.h"
#include "NCPoliceValidationRunner.h"
#include "NCPoliceDirector.h"
#include "NCWorldMarkers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ANCGameMode::ANCGameMode()
{
    DefaultPawnClass=nullptr;
    PlayerControllerClass=ANCPlayerController::StaticClass();
    HUDClass=ANCHUD::StaticClass();
}
void ANCGameMode::StartPlay()
{
    Super::StartPlay();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<ANCCityVisual>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    GetWorld()->SpawnActor<ANCWorldMarkers>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    auto* Car=GetWorld()->SpawnActor<ANCCar>(FVector(21600,3000,80),FRotator(0,90,0),Params);
    auto* Hero=GetWorld()->SpawnActor<ANCHero>(FVector(21920,3050,110),FRotator(0,90,0),Params);
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
    {
        PC->Possess(Hero); PC->SetControlRotation(FRotator(-12,90,0));
    }
    // Walking traffic is concentrated around the arrival, then extended along the coast.
    for(int32 I=0;I<64;++I)
    {
        FVector Origin;
        if(I<20) Origin=FVector(I%2?22600:20600,1400+(I/2)*390,92);
        else if(I<44) Origin=FVector(I%2?22600:24300,-21000+(I-20)*1750,92);
        else Origin=FVector(-20600+(I%6)*7200,-16000+((I-44)/6)*9000,92);
        auto* Person=GetWorld()->SpawnActorDeferred<ANCPedestrian>(ANCPedestrian::StaticClass(),FTransform(FRotator::ZeroRotator,Origin),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(Person) { Person->AppearanceSeed=I*17+3; Person->RouteHalfLength=I<20?280.f:700.f; UGameplayStatics::FinishSpawningActor(Person,FTransform(FRotator::ZeroRotator,Origin)); }
    }
    for(int32 I=0;I<8;++I)
    {
        const float X=I<4?14400.f:-14400.f;
        const float Y=-14400.f+(I%4)*7200.f;
        TArray<FVector> Route={FVector(X+400,Y+400,80),FVector(X+400,Y+6800,80),FVector(X+6800,Y+6800,80),FVector(X+6800,Y+400,80)};
        auto* Traffic=GetWorld()->SpawnActor<ANCCar>(Route[0],FRotator(0,90,0),Params);
        if(Traffic) Traffic->SetRoute(Route);
    }
    UE_LOG(LogTemp,Display,TEXT("NEON_COAST_UE_READY pedestrians=64 vehicles=9 hero=%s car=%s"),*GetNameSafe(Hero),*GetNameSafe(Car));
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("NeonCoastPoliceTest")))
    {
        GetWorld()->SpawnActor<ANCPoliceDirector>();
        GetWorld()->SpawnActor<ANCPoliceValidationRunner>();
    }
    else if(FParse::Param(FCommandLine::Get(),TEXT("NeonCoastSmokeTest")))
        GetWorld()->SpawnActor<ANCValidationRunner>();
    else GetWorld()->SpawnActor<ANCPoliceDirector>();
#else
    GetWorld()->SpawnActor<ANCPoliceDirector>();
#endif
}
