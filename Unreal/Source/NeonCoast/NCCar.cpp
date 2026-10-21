#include "NCCar.h"
#include "NCHero.h"
#include "NCPrimitives.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

ANCCar::ANCCar()
{
    PrimaryActorTick.bCanEverTick=true;
    Chassis=CreateDefaultSubobject<UBoxComponent>(TEXT("Chassis"));
    Chassis->InitBoxExtent(FVector(218,98,65));
    Chassis->SetCollisionProfileName(TEXT("Pawn"));
    Chassis->SetCollisionObjectType(ECC_Vehicle);
    RootComponent=Chassis;
    Arm=CreateDefaultSubobject<USpringArmComponent>(TEXT("VehicleCameraArm"));
    Arm->SetupAttachment(RootComponent);
    Arm->TargetArmLength=730;
    Arm->SocketOffset=FVector(0,0,180);
    Arm->bUsePawnControlRotation=true;
    Arm->bEnableCameraLag=true;
    Arm->CameraLagSpeed=10;
    Arm->bEnableCameraRotationLag=true;
    Arm->CameraRotationLagSpeed=10;
    Arm->bUseCameraLagSubstepping=true;
    Arm->CameraLagMaxTimeStep=1.f/120.f;
    Arm->CameraLagMaxDistance=180;
    Arm->PrimaryComponentTick.TickGroup=TG_PostPhysics;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("DrivingCamera"));
    Camera->SetupAttachment(Arm);
    Camera->FieldOfView=72;
}
void ANCCar::BeginPlay()
{
    Super::BeginPlay();
    NCPrimitives::Part(this,RootComponent,TEXT("Body"),TEXT("Cube"),FVector(0,0,-8),FVector(4.35,1.90,.57),Paint);
    NCPrimitives::Part(this,RootComponent,TEXT("Cabin"),TEXT("Cube"),FVector(-20,0,42),FVector(2.15,1.63,.55),FLinearColor(.035,.09,.13));
    NCPrimitives::Part(this,RootComponent,TEXT("Roof"),TEXT("Cube"),FVector(-20,0,72),FVector(1.8,1.65,.08),Paint);
    for(int32 S=-1;S<=1;S+=2)
    {
        NCPrimitives::Part(this,RootComponent,*FString::Printf(TEXT("Headlight%d"),S),TEXT("Cube"),FVector(220,S*64,5),FVector(.04,.5,.10),FLinearColor(.9,.95,1));
        NCPrimitives::Part(this,RootComponent,*FString::Printf(TEXT("Taillight%d"),S),TEXT("Cube"),FVector(-220,S*64,5),FVector(.04,.5,.10),FLinearColor(.8,.02,.03));
        for(int32 X=-1;X<=1;X+=2)
        {
            auto* Wheel=NCPrimitives::Part(this,RootComponent,*FString::Printf(TEXT("Wheel%d%d"),S,X),TEXT("Cylinder"),FVector(X*140,S*99,-35),FVector(.65,.65,.24),FLinearColor(.016,.02,.025));
            Wheel->SetRelativeRotation(FRotator(0,0,90));
            Wheels.Add(Wheel);
        }
    }
}
void ANCCar::SetRoute(const TArray<FVector>& Points) { Route=Points; RouteIndex=0; }
float ANCCar::GetDriverHealth() const { return IsValid(Driver)?Driver->GetHealth():100.f; }
bool ANCCar::CanEnter() const { return !IsValid(Driver) && GetSpeedKmh()<18 && Condition>0; }
void ANCCar::SetDriver(ANCHero* Hero) { Driver=Hero; Route.Reset(); ForwardInput=PadThrottle=PadReverse=Steer=0; bBrake=bBoost=false; }
void ANCCar::Tick(float Dt)
{
    Super::Tick(Dt);
    HitCooldown=FMath::Max(0.f,HitCooldown-Dt);
    LookIdle+=Dt;
    // Bound integration steps, so steering/acceleration do not depend on render FPS.
    float Remaining=FMath::Min(Dt,.1f);
    while(Remaining>SMALL_NUMBER)
    {
        const float Step=FMath::Min(Remaining,1.f/120.f);
        SimulateStep(Step); Remaining-=Step;
    }
    if(Controller && LookIdle>1.5f && FMath::Abs(Speed)>350)
    {
        FRotator View=Controller->GetControlRotation();
        View.Yaw=FMath::FixedTurn(View.Yaw,GetActorRotation().Yaw,70.f*Dt);
        Controller->SetControlRotation(View);
    }
    Camera->FieldOfView=FMath::FInterpTo(Camera->FieldOfView,72+FMath::Clamp(GetSpeedKmh()*.06f,0.f,10.f),Dt,3.f);
    if(GetActorLocation().Z<-1500) Recover();
}
void ANCCar::SimulateStep(float Dt)
{
    float Drive=FMath::Clamp(ForwardInput+PadThrottle-PadReverse,-1.f,1.f);
    float Turn=Steer;
    bool Stop=bBrake;
    if(!Driver && Route.Num()>0)
    {
        FVector Delta=Route[RouteIndex]-GetActorLocation(); Delta.Z=0;
        if(Delta.Size2D()<650) { RouteIndex=(RouteIndex+1)%Route.Num(); Delta=Route[RouteIndex]-GetActorLocation(); }
        const FVector Local=GetActorTransform().InverseTransformVectorNoScale(Delta.GetSafeNormal2D());
        Turn=FMath::Clamp(FMath::Atan2(Local.Y,Local.X)*1.8f,-1.f,1.f);
        const float Desired=FMath::Abs(Turn)>.35f?650.f:1150.f;
        Drive=FMath::Clamp((Desired-Speed)*.004f,-1.f,.75f);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(TrafficObstacle),false,this);
        FHitResult Obstacle;
        const FVector Start=GetActorLocation()+GetActorForwardVector()*240;
        const FVector End=Start+GetActorForwardVector()*(450+FMath::Max(Speed,0.f)*.55f);
        Stop=GetWorld()->SweepSingleByChannel(Obstacle,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(65),Params);
        if(Stop) Drive=0;
    }
    else if(!Driver) { Drive=0; Turn=0; }
    if(Condition<=0) { Drive=0; Stop=true; }
    SmoothedSteer=FMath::FInterpTo(SmoothedSteer,Turn,Dt,7.5f);
    const float MaxSpeed=bBoost?4900.f:3800.f;
    if(Stop) Speed=FMath::FInterpConstantTo(Speed,0.f,Dt,2100.f);
    else if(FMath::Abs(Drive)>.04f)
    {
        const bool Opposite=Drive*Speed<0 && FMath::Abs(Speed)>100;
        Speed+=Drive*(Opposite?2300.f:(bBoost?1800.f:1250.f))*Dt;
    }
    else Speed=FMath::FInterpConstantTo(Speed,0.f,Dt,120.f+FMath::Abs(Speed)*.025f);
    Speed=FMath::Clamp(Speed,-1000.f,MaxSpeed);
    FRotator Rotation=GetActorRotation();
    const float Rate=FMath::Lerp(75.f,42.f,FMath::Clamp(FMath::Abs(Speed)/3800.f,0.f,1.f));
    Rotation.Yaw+=SmoothedSteer*Rate*FMath::Clamp(Speed/1100.f,-1.f,1.f)*Dt*(Stop?1.2f:1.f);
    SetActorRotation(Rotation);
    FHitResult Hit;
    AddActorWorldOffset(GetActorForwardVector()*Speed*Dt,true,&Hit);
    if(Hit.IsValidBlockingHit())
    {
        const float Impact=FMath::Abs(Speed)*FMath::Abs(FVector::DotProduct(GetActorForwardVector(),Hit.Normal));
        if(Impact>500 && HitCooldown<=0) { Condition=FMath::Max(0.f,Condition-(Impact-400)*.009f); HitCooldown=.6f; }
        Speed*=FMath::Clamp(1.f-FMath::Abs(FVector::DotProduct(GetActorForwardVector(),Hit.Normal)),0.f,.85f);
    }
    FVector Position=GetActorLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CarGround),false,this);
    if(Driver) Params.AddIgnoredActor(Driver);
    FHitResult Ground;
    if(GetWorld()->LineTraceSingleByChannel(Ground,Position+FVector(0,0,35),Position-FVector(0,0,300),ECC_WorldStatic,Params) && Ground.Normal.Z>.7f)
    {
        // One continuous ground reference; paint lines and visual curbs are noncolliding.
        Position.Z=Ground.ImpactPoint.Z+72;
        SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    }
}
bool ANCCar::FindExit(FVector& Out) const
{
    const FVector Offsets[]={FVector(0,-215,0),FVector(0,215,0),FVector(-340,0,0),FVector(340,0,0)};
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BailoutClearance),false,this);
    if(Driver) Params.AddIgnoredActor(Driver);
    for(const FVector& Offset:Offsets)
    {
        FVector Point=GetActorTransform().TransformPositionNoScale(Offset);
        FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,Point+FVector(0,0,150),Point-FVector(0,0,400),ECC_WorldStatic,Params) || Floor.Normal.Z<.7f) continue;
        Point=Floor.ImpactPoint+FVector(0,0,96);
        if(GetWorld()->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(36,90),Params)) continue;
        FHitResult Path;
        const FVector Start=GetActorLocation()+FVector(0,0,25);
        if(GetWorld()->SweepSingleByChannel(Path,Start,Point,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(30),Params)) continue;
        Out=Point; return true;
    }
    return false;
}
void ANCCar::ExitCar()
{
    auto* PC=Cast<APlayerController>(Controller);
    if(!PC || !IsValid(Driver)) return;
    FVector Exit;
    if(!FindExit(Exit)) { if(GEngine) GEngine->AddOnScreenDebugMessage(-1,3,FColor::Orange,TEXT("Exit blocked. Move clear or press T to recover.")); return; }
    ANCHero* Hero=Driver;
    const FVector Momentum=GetActorForwardVector()*Speed*.65f+(Exit-GetActorLocation()).GetSafeNormal2D()*260;
    Driver=nullptr; ForwardInput=PadThrottle=PadReverse=Steer=0; bBrake=bBoost=false;
    Hero->LeaveCar(Exit,Momentum,GetSpeedKmh());
    PC->Possess(Hero);
    PC->SetControlRotation(FRotator(-12,GetActorRotation().Yaw,0));
}
void ANCCar::Recover()
{
    Speed=0; Condition=100;
    SetActorLocationAndRotation(FVector(21600,3000,80),FRotator(0,90,0),false,nullptr,ETeleportType::TeleportPhysics);
    if(Controller) Controller->SetControlRotation(FRotator(-12,90,0));
}
void ANCCar::Repair() { if(FVector::Dist2D(GetActorLocation(),FVector(21600,0,0))<1000 && GetSpeedKmh()<5) Condition=100; }
void ANCCar::LookYaw(float V) { if(FMath::Abs(V)>.001f) LookIdle=0; AddControllerYawInput(V*.8f); }
void ANCCar::LookPitch(float V) { if(FMath::Abs(V)>.001f) LookIdle=0; AddControllerPitchInput(V*.7f); }
void ANCCar::PadYaw(float V) { LookYaw(V*100*GetWorld()->GetDeltaSeconds()); }
void ANCCar::PadPitch(float V) { LookPitch(V*75*GetWorld()->GetDeltaSeconds()); }
void ANCCar::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis("Forward",this,&ANCCar::Forward); Input->BindAxis("Right",this,&ANCCar::Right);
    Input->BindAxis("Throttle",this,&ANCCar::Throttle); Input->BindAxis("Reverse",this,&ANCCar::Reverse);
    Input->BindAxis("LookYaw",this,&ANCCar::LookYaw); Input->BindAxis("LookPitch",this,&ANCCar::LookPitch);
    Input->BindAxis("PadYaw",this,&ANCCar::PadYaw); Input->BindAxis("PadPitch",this,&ANCCar::PadPitch);
    Input->BindAction("Interact",IE_Pressed,this,&ANCCar::ExitCar);
    Input->BindAction("Brake",IE_Pressed,this,&ANCCar::BrakeOn); Input->BindAction("Brake",IE_Released,this,&ANCCar::BrakeOff);
    Input->BindAction("Boost",IE_Pressed,this,&ANCCar::BoostOn); Input->BindAction("Boost",IE_Released,this,&ANCCar::BoostOff);
    Input->BindAction("Recover",IE_Pressed,this,&ANCCar::Recover); Input->BindAction("Greet",IE_Pressed,this,&ANCCar::Repair);
}
