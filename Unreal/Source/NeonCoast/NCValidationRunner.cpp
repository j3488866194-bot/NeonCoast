#include "NCValidationRunner.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCPedestrian.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
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
void ANCValidationRunner::Check(bool Passed, const TCHAR* Name)
{
    ++Checks;
    if(!Passed) ++Failures;
    UE_LOG(LogTemp,Display,TEXT("NC_CHECK %s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),Name);
}
void ANCValidationRunner::CheckFrameRates()
{
    // Same four seconds of driving with different render-frame durations.
    // Actors use the real collision floor and drivetrain, with the same inputs.
    FVector Results[3];
    float Speeds[3];
    const int32 Rates[]={30,60,144};
    for(int32 R=0;R<3;++R)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Probe=GetWorld()->SpawnActor<ANCCar>(FVector(21600,-18000,72),FRotator(0,90,0),Params);
        if(!Probe) { Check(false,TEXT("frame-rate probe created")); return; }
        Probe->SetActorHiddenInGame(true);
        Probe->Driver=Hero;
        Probe->ForwardInput=.6f;
        for(int32 Frame=0;Frame<Rates[R]*4;++Frame)
        {
            float Remaining=1.f/Rates[R];
            while(Remaining>SMALL_NUMBER)
            {
                const float Step=FMath::Min(Remaining,1.f/120.f);
                Probe->SimulateStep(Step);
                Remaining-=Step;
            }
        }
        Results[R]=Probe->GetActorLocation(); Speeds[R]=Probe->GetSpeedKmh();
        Probe->Destroy();
    }
    Check(FVector::Dist(Results[0],Results[1])<2.f && FVector::Dist(Results[0],Results[2])<25.f,TEXT("30/60/144 FPS distance agrees within 25 cm"));
    Check(FMath::Abs(Speeds[0]-Speeds[2])<.5f,TEXT("30/60/144 FPS speed agrees within 0.5 km/h"));
}
void ANCValidationRunner::Tick(float Dt)
{
    Super::Tick(Dt);
    Clock+=Dt;
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    if(Phase==0 && Clock>2)
    {
        Hero=PC?Cast<ANCHero>(PC->GetPawn()):nullptr;
        Check(IsValid(Hero),TEXT("player starts on foot"));
        int32 Cars=0;
        float Nearest=MAX_flt;
        for(TActorIterator<ANCCar> It(GetWorld());It;++It)
        {
            ++Cars;
            const float D=Hero?FVector::Dist(Hero->GetActorLocation(),It->GetActorLocation()):MAX_flt;
            if(D<Nearest) { Nearest=D; Car=*It; }
        }
        for(TActorIterator<ANCPedestrian> It(GetWorld());It;++It) PedestrianStart.Add(*It,It->GetActorLocation());
        Check(Cars==9,TEXT("nine vehicles spawned"));
        Check(PedestrianStart.Num()==64,TEXT("64 pedestrians spawned"));
        Check(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_Coast.M_Coast"))!=nullptr,TEXT("authored material loads"));
        if(!Hero || !Car) { Finish(); return; }
        AddTickPrerequisiteActor(PC);
        CheckFrameRates();
        Phase=1;
    }
    else if(Phase==1 && Clock>5)
    {
        int32 Moving=0;
        for(const auto& Pair:PedestrianStart)
            if(Pair.Key.IsValid() && FVector::Dist2D(Pair.Key->GetActorLocation(),Pair.Value)>50) ++Moving;
        Check(Moving>=40,TEXT("at least 40 pedestrians walk more than 50 cm"));
        Hero->EnterCar(Car);
        Car->AddTickPrerequisiteActor(this);
        Check(PC && PC->GetPawn()==Car && Hero->IsHidden(),TEXT("enter car transfers possession and hides hero"));
        LastZ=Car->GetActorLocation().Z;
        Phase=2;
    }
    else if(Phase==2)
    {
        // Set control after the input tick; the next game frame consumes it.
        Car->ForwardInput=.8f;
        const float Z=Car->GetActorLocation().Z;
        MaxVerticalStep=FMath::Max(MaxVerticalStep,FMath::Abs(Z-LastZ)); LastZ=Z;
        if(Clock>8)
        {
            Check(Car->GetSpeedKmh()>45,TEXT("car accelerates above 45 km/h"));
            Check(MaxVerticalStep<.5f,TEXT("flat-road chassis has less than 5 mm vertical step"));
            Car->ExitCar();
            Check(PC && PC->GetPawn()==Hero,TEXT("high-speed exit transfers possession to hero"));
            Check(!Hero->IsHidden() && Hero->GetActorEnableCollision(),TEXT("bailout restores visible colliding character"));
            Check(Hero->IsRecovering() && Hero->GetHealth()<100,TEXT("high-speed bailout applies recovery and damage"));
            ExitPosition=Hero->GetActorLocation();
            Phase=3;
        }
    }
    else if(Phase==3 && Clock>10)
    {
        Check(FVector::Dist2D(ExitPosition,Hero->GetActorLocation())>100,TEXT("bailout carries momentum into character movement"));
        Check(!Hero->IsRecovering(),TEXT("bailout recovery ends"));
        Check(Hero->GetActorLocation().Z>0 && Hero->GetActorLocation().Z<120,TEXT("hero lands on road"));
        Hero->Jump();
        Phase=4;
    }
    else if(Phase==4 && Clock>10.3f)
    {
        Check(Hero->GetActorLocation().Z>150,TEXT("on-foot jump leaves ground"));
        Hero->StopJumping();
        Finish();
    }
    if(Clock>20 && Phase!=99) { Check(false,TEXT("smoke test timeout")); Finish(); }
}
void ANCValidationRunner::Finish()
{
    Phase=99;
    UE_LOG(LogTemp,Display,TEXT("NEON_COAST_SMOKE_%s checks=%d failures=%d maxVerticalStepCm=%.4f"),Failures?TEXT("FAIL"):TEXT("PASS"),Checks,Failures,MaxVerticalStep);
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
