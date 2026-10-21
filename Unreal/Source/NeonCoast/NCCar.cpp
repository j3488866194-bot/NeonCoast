#include "NCCar.h"
#include "NCVehicleWheels.h"
#include "NCHero.h"
#include "NCPedestrian.h"
#include "NCGameplaySubsystem.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float RoadSpacing=7200.f;
    constexpr float RoadLimit=21600.f;
    float RoadCoordinate(float Value)
    {
        return FMath::Clamp(FMath::RoundToFloat(Value/RoadSpacing)*RoadSpacing,-RoadLimit,RoadLimit);
    }
    FVector NearestRoad(FVector Position)
    {
        Position.X=FMath::Clamp(Position.X,-RoadLimit,RoadLimit);
        Position.Y=FMath::Clamp(Position.Y,-RoadLimit,RoadLimit);
        const float X=RoadCoordinate(Position.X), Y=RoadCoordinate(Position.Y);
        if(FMath::Abs(Position.X-X)<FMath::Abs(Position.Y-Y)) Position.X=X;
        else Position.Y=Y;
        Position.Z=0;
        return Position;
    }
    void ConfigureCosmetic(UStaticMeshComponent* Component)
    {
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
    }
}

ANCCar::ANCCar()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    bUseControllerRotationYaw=false;
    GetMesh()->SetCollisionProfileName(TEXT("Vehicle"));
    GetMesh()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    GetMesh()->SetNotifyRigidBodyCollision(true);
    GetMesh()->SetGenerateOverlapEvents(true);
    GetMesh()->SetCanEverAffectNavigation(false);
    GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Rig(TEXT("/Game/Vehicles/SportsCar/SKM_SportsCar.SKM_SportsCar"));
    static ConstructorHelpers::FObjectFinder<UPhysicsAsset> Physics(TEXT("/Game/Vehicles/SportsCar/PA_SportsCar.PA_SportsCar"));
    static ConstructorHelpers::FClassFinder<UAnimInstance> Animation(TEXT("/Game/Vehicles/SportsCar/ABP_SportsCar"));
    if(Rig.Succeeded()) GetMesh()->SetSkeletalMesh(Rig.Object);
    if(Physics.Succeeded()) GetMesh()->SetPhysicsAsset(Physics.Object);
    if(Animation.Succeeded()) GetMesh()->SetAnimInstanceClass(Animation.Class);
    GetMesh()->SetSimulatePhysics(true);

    Vehicle=CastChecked<UChaosWheeledVehicleMovementComponent>(GetVehicleMovement());
    Vehicle->SetRequiresControllerForInputs(false);
    Vehicle->bReverseAsBrake=false;
    Vehicle->Mass=1500.f;
    Vehicle->ChassisHeight=144.f;
    Vehicle->DragCoefficient=.31f;
    Vehicle->bLegacyWheelFrictionPosition=true;
    Vehicle->WheelSetups.SetNum(4);
    const FName WheelBones[]={TEXT("Phys_Wheel_FL"),TEXT("Phys_Wheel_FR"),TEXT("Phys_Wheel_BL"),TEXT("Phys_Wheel_BR")};
    for(int32 I=0;I<4;++I)
    {
        Vehicle->WheelSetups[I].WheelClass=I<2?UNCFrontWheel::StaticClass():UNCRearWheel::StaticClass();
        Vehicle->WheelSetups[I].BoneName=WheelBones[I];
        Vehicle->WheelSetups[I].AdditionalOffset=FVector::ZeroVector;
    }
    Vehicle->DifferentialSetup.DifferentialType=EVehicleDifferential::RearWheelDrive;
    Vehicle->EngineSetup.MaxTorque=750.f;
    Vehicle->EngineSetup.MaxRPM=7000.f;
    Vehicle->EngineSetup.EngineIdleRPM=900.f;
    Vehicle->EngineSetup.EngineBrakeEffect=.2f;
    Vehicle->EngineSetup.EngineRevUpMOI=5.f;
    Vehicle->EngineSetup.EngineRevDownRate=600.f;
    // A nonzero curve is required: Chaos normalizes it by its maximum value.
    FRichCurve* Torque=Vehicle->EngineSetup.TorqueCurve.GetRichCurve();
    Torque->AddKey(0.f,.35f);
    Torque->AddKey(900.f,.5f);
    Torque->AddKey(2500.f,.85f);
    Torque->AddKey(4500.f,1.f);
    Torque->AddKey(6000.f,.92f);
    Torque->AddKey(7000.f,.7f);
    Vehicle->TransmissionSetup.bUseAutomaticGears=true;
    Vehicle->TransmissionSetup.bUseAutoReverse=false;
    Vehicle->TransmissionSetup.FinalRatio=2.81f;
    Vehicle->TransmissionSetup.ChangeUpRPM=6000.f;
    Vehicle->TransmissionSetup.ChangeDownRPM=2000.f;
    Vehicle->TransmissionSetup.GearChangeTime=.2f;
    Vehicle->TransmissionSetup.TransmissionEfficiency=.9f;
    Vehicle->TransmissionSetup.ForwardGearRatios={4.25f,2.52f,1.66f,1.22f,1.f};
    Vehicle->TransmissionSetup.ReverseGearRatios={4.04f};
    Vehicle->SteeringSetup.SteeringType=ESteeringType::Ackermann;
    Vehicle->SteeringSetup.AngleRatio=.7f;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CarBody(TEXT("/Game/Vehicles/SportsCar/SM_SportsCar.SM_SportsCar"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CarGlass(TEXT("/Game/Vehicles/SportsCar/SM_SportsCar_Glass.SM_SportsCar_Glass"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CarWheel(TEXT("/Game/Vehicles/SportsCar/SM_SportsCar_Wheel.SM_SportsCar_Wheel"));
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SportsCarBody"));
    Body->SetupAttachment(GetMesh());
    if(CarBody.Succeeded()) Body->SetStaticMesh(CarBody.Object);
    ConfigureCosmetic(Body);
    Glass=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SportsCarGlass"));
    Glass->SetupAttachment(GetMesh());
    if(CarGlass.Succeeded()) Glass->SetStaticMesh(CarGlass.Object);
    ConfigureCosmetic(Glass);
    for(int32 I=0;I<4;++I)
    {
        auto* Wheel=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wheel%d"),I));
        Wheel->SetupAttachment(GetMesh(),WheelBones[I]);
        if(CarWheel.Succeeded()) Wheel->SetStaticMesh(CarWheel.Object);
        // Match BP_SportsCar_Pawn's wheel-to-bone rotations: left -90, right +90 yaw.
        Wheel->SetRelativeRotation(FRotator(0.f,(I==0 || I==2)?-90.f:90.f,0.f));
        ConfigureCosmetic(Wheel);
        Wheels.Add(Wheel);
    }
    for(int32 I=0;I<2;++I)
    {
        auto* Light=CreateDefaultSubobject<USpotLightComponent>(*FString::Printf(TEXT("Headlight%d"),I));
        Light->SetupAttachment(GetMesh());
        Light->SetRelativeLocation(FVector(235.f,I?70.f:-70.f,65.f));
        Light->SetRelativeRotation(FRotator(-4.f,0.f,0.f));
        Light->SetIntensity(18000.f);
        Light->SetLightColor(FLinearColor(.88f,.94f,1.f));
        Light->SetAttenuationRadius(4200.f);
        Light->SetInnerConeAngle(20.f);
        Light->SetOuterConeAngle(35.f);
        Light->SetCastShadows(false);
        Light->SetVisibility(false);
        Headlights.Add(Light);
        auto* Beacon=CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("PoliceBeacon%d"),I));
        Beacon->SetupAttachment(GetMesh());
        Beacon->SetRelativeLocation(FVector(-35.f,I?45.f:-45.f,145.f));
        Beacon->SetLightColor(I?FLinearColor(.05f,.2f,1.f):FLinearColor(1.f,.03f,.02f));
        Beacon->SetIntensity(6000.f);
        Beacon->SetAttenuationRadius(550.f);
        Beacon->SetCastShadows(false);
        Beacon->SetVisibility(false);
        PoliceLights.Add(Beacon);
    }
    Arm=CreateDefaultSubobject<USpringArmComponent>(TEXT("VehicleCameraArm"));
    Arm->SetupAttachment(GetMesh());
    Arm->TargetArmLength=730.f;
    Arm->SocketOffset=FVector(0.f,0.f,200.f);
    Arm->bUsePawnControlRotation=true;
    Arm->bInheritRoll=false;
    Arm->bEnableCameraLag=true;
    Arm->CameraLagSpeed=12.f;
    Arm->bEnableCameraRotationLag=true;
    Arm->CameraRotationLagSpeed=10.f;
    Arm->bUseCameraLagSubstepping=true;
    Arm->bClampToMaxPhysicsDeltaTime=true;
    Arm->CameraLagMaxTimeStep=1.f/120.f;
    Arm->CameraLagMaxDistance=150.f;
    Arm->PrimaryComponentTick.TickGroup=TG_PostPhysics;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("DrivingCamera"));
    Camera->SetupAttachment(Arm,USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation=false;
    Camera->FieldOfView=72.f;
}

void ANCCar::BeginPlay()
{
    Super::BeginPlay();
    LastSafeTransform=GetActorTransform();
    GetMesh()->OnComponentHit.AddDynamic(this,&ANCCar::OnVehicleHit);
    UpdatePaint();
    UpdateLights();
    UE_LOG(LogTemp,Display,TEXT("NEON_COAST_CHAOS_CAR actor=%s rig=%s wheels=%d"),
        *GetName(),*GetNameSafe(GetMesh()->GetSkeletalMeshAsset()),Vehicle->WheelSetups.Num());
}

float ANCCar::GetSpeedKmh() const
{
    return Vehicle?FMath::Abs(Vehicle->GetForwardSpeed())*.036f:0.f;
}
float ANCCar::GetDriverHealth() const { return IsValid(Driver)?Driver->GetHealth():100.f; }
bool ANCCar::CanEnter() const
{
    return !IsValid(Driver) && GetSpeedKmh()<18.f && Condition>0.f && GetActorUpVector().Z>.35f;
}
void ANCCar::ClearDrivingInput()
{
    ForwardInput=PadThrottle=PadReverse=Steer=0.f;
    bBrake=bBoost=bExternalDrivingInput=false;
    Vehicle->SetThrottleInput(0.f);
    Vehicle->SetBrakeInput(0.f);
    Vehicle->SetSteeringInput(0.f);
    Vehicle->SetHandbrakeInput(false);
}
void ANCCar::SetDriver(ANCHero* Hero)
{
    Driver=Hero;
    bHasBeenPlayerDriven|=Hero!=nullptr;
    Route.Reset();
    PursuitRoute.Reset();
    PursuitTarget.Reset();
    bHasPursuitLocation=false;
    ClearDrivingInput();
    Vehicle->SetSleeping(false);
    UpdateLights();
}
void ANCCar::SetDrivingInput(float ThrottleValue,float SteeringValue,bool BrakeValue)
{
    bExternalDrivingInput=true;
    ForwardInput=FMath::Clamp(ThrottleValue,-1.f,1.f);
    PadThrottle=PadReverse=0.f;
    Steer=FMath::Clamp(SteeringValue,-1.f,1.f);
    bBrake=BrakeValue;
}
void ANCCar::SetRoute(const TArray<FVector>& Points)
{
    // Switching back to a patrol is a state change, not an extra route below a
    // still-active pursuit. The director uses this when the search is called off.
    PursuitTarget.Reset();
    PursuitRoute.Reset();
    bHasPursuitLocation=false;
    Route=Points;
    RouteIndex=0;
    if(Route.Num()>1 && FVector::Dist2D(GetActorLocation(),Route[0])<1000.f) RouteIndex=1;
    StuckTime=TrafficWaitTime=AvoidanceTime=ReverseTime=0.f;
    bExternalDrivingInput=false;
    Vehicle->SetSleeping(false);
    UpdateLights();
}
void ANCCar::SetPolice(bool bEnabled)
{
    bPolice=bEnabled;
    if(bPolice) Paint=FLinearColor(.035f,.055f,.085f);
    UpdatePaint();
    UpdateLights();
}
void ANCCar::SetPursuitTarget(AActor* Target)
{
    PursuitTarget=Target;
    if(IsValid(Target))
    {
        if(!bHasPursuitLocation) PursuitRoute.Reset();
        PursuitLocation=Target->GetActorLocation();
        bHasPursuitLocation=true;
        bExternalDrivingInput=false;
    }
    else if(!bHasPursuitLocation) PursuitRoute.Reset();
}
void ANCCar::SetPursuitLocation(FVector Location)
{
    if(Location.ContainsNaN()) return;
    PursuitTarget.Reset();
    if(!bHasPursuitLocation || FVector::Dist2D(Location,PursuitLocation)>250.f) PursuitRoute.Reset();
    bHasPursuitLocation=true;
    PursuitLocation=Location;
    bExternalDrivingInput=false;
}

void ANCCar::Tick(float Dt)
{
    Super::Tick(Dt);
    Speed=Vehicle->GetForwardSpeed();
    HitCooldown=FMath::Max(0.f,HitCooldown-Dt);
    UnstickCooldown=FMath::Max(0.f,UnstickCooldown-Dt);
    RoutePlanTime-=Dt;
    LookIdle+=Dt;
    float Drive=FMath::Clamp(ForwardInput+PadThrottle-PadReverse,-1.f,1.f);
    float Turn=Steer, Brake=0.f;
    if(!Driver && !bExternalDrivingInput)
    {
        if(Route.Num()>0 || (bPolice && bHasPursuitLocation)) ComputeTrafficControls(Dt,Drive,Turn,Brake);
        else { Drive=Turn=0.f; }
    }
    if(Condition<=0.f) { Drive=0.f; Brake=1.f; }
    ApplyControls(Drive,Turn,Brake,bBrake);
    if(bBoost!=bLastBoost)
    {
        Vehicle->SetMaxEngineTorque(bBoost?900.f:750.f);
        bLastBoost=bBoost;
    }
    // Physics owns the actor transform. Only input and presentation are updated here.
    if(Controller && LookIdle>1.5f && FMath::Abs(Speed)>350.f)
    {
        FRotator View=Controller->GetControlRotation();
        View.Yaw=FMath::FixedTurn(View.Yaw,GetActorRotation().Yaw,70.f*Dt);
        View.Roll=0.f;
        Controller->SetControlRotation(View);
    }
    Camera->FieldOfView=FMath::FInterpTo(Camera->FieldOfView,72.f+FMath::Clamp(GetSpeedKmh()*.06f,0.f,10.f),Dt,3.f);
    if(bPolice) UpdateLights();
    SafeTime+=Dt;
    if(SafeTime>1.f)
    {
        SafeTime=0.f;
        const FVector P=GetActorLocation();
        if(P.Z>-40.f && P.Z<180.f && GetActorUpVector().Z>.85f &&
            FVector::Dist2D(P,NearestRoad(P))<650.f) LastSafeTransform=GetActorTransform();
    }
    if(GetActorLocation().Z<-1500.f) Recover();
    PreviousVelocity=GetMesh()->GetPhysicsLinearVelocity();
}

void ANCCar::ApplyControls(float Drive,float Turn,float Brake,bool Handbrake)
{
    float ThrottleAmount=0.f;
    if(!Handbrake && Brake<.01f && Condition>0.f)
    {
        if(Drive>.01f)
        {
            if(Speed<-100.f) Brake=Drive;
            else
            {
                if(Vehicle->GetTargetGear()<=0) Vehicle->SetTargetGear(1,true);
                ThrottleAmount=Drive;
            }
        }
        else if(Drive<-.01f)
        {
            if(Speed>100.f) Brake=-Drive;
            else
            {
                if(Vehicle->GetTargetGear()>=0) Vehicle->SetTargetGear(-1,true);
                ThrottleAmount=-Drive;
            }
        }
    }
    if((Speed>0.f && GetSpeedKmh()>(bBoost?185.f:165.f)) || Speed<-1300.f) ThrottleAmount=0.f;
    Vehicle->SetThrottleInput(FMath::Clamp(ThrottleAmount,0.f,1.f));
    Vehicle->SetBrakeInput(FMath::Clamp(Handbrake?FMath::Max(Brake,.35f):Brake,0.f,1.f));
    Vehicle->SetSteeringInput(FMath::Clamp(Turn,-1.f,1.f));
    Vehicle->SetHandbrakeInput(Handbrake);
}

void ANCCar::BuildPursuitRoute()
{
    PursuitRoute.Reset();
    PursuitRouteIndex=0;
    const FVector Position=GetActorLocation();
    const FVector RoadStart=NearestRoad(Position), RoadEnd=NearestRoad(PursuitLocation);
    FVector Junction(RoadCoordinate(RoadStart.X),RoadCoordinate(RoadStart.Y),0.f);
    const FVector Facing=GetActorForwardVector();
    // Enter the next intersection in the current direction, avoiding mid-block U-turns.
    if(FMath::Abs(Facing.Y)>FMath::Abs(Facing.X))
        Junction.Y=FMath::Clamp((Facing.Y>0.f?FMath::CeilToFloat(RoadStart.Y/RoadSpacing):FMath::FloorToFloat(RoadStart.Y/RoadSpacing))*RoadSpacing,-RoadLimit,RoadLimit);
    else
        Junction.X=FMath::Clamp((Facing.X>0.f?FMath::CeilToFloat(RoadStart.X/RoadSpacing):FMath::FloorToFloat(RoadStart.X/RoadSpacing))*RoadSpacing,-RoadLimit,RoadLimit);
    if(FVector::Dist2D(Position,Junction)>650.f) PursuitRoute.Add(Junction);
    const FVector EndJunction(RoadCoordinate(RoadEnd.X),RoadCoordinate(RoadEnd.Y),0.f);
    for(int32 I=0;I<12 && FVector::Dist2D(Junction,EndJunction)>100.f;++I)
    {
        const float DX=EndJunction.X-Junction.X, DY=EndJunction.Y-Junction.Y;
        if(FMath::Abs(DX)>100.f && (FMath::Abs(DX)>=FMath::Abs(DY) || FMath::Abs(DY)<100.f))
            Junction.X+=FMath::Sign(DX)*RoadSpacing;
        else Junction.Y+=FMath::Sign(DY)*RoadSpacing;
        PursuitRoute.Add(Junction);
    }
    PursuitRoute.Add(RoadEnd);
    PlannedPursuitLocation=PursuitLocation;
    RoutePlanTime=1.f;
}

void ANCCar::ComputeTrafficControls(float Dt,float& Drive,float& Turn,float& Brake)
{
    const bool bPursuing=bPolice && bHasPursuitLocation;
    if(bPursuing)
    {
        if(PursuitTarget.IsValid()) PursuitLocation=PursuitTarget->GetActorLocation();
        if(PursuitRoute.IsEmpty() || (RoutePlanTime<=0.f && FVector::Dist2D(PlannedPursuitLocation,PursuitLocation)>1300.f)) BuildPursuitRoute();
    }
    TArray<FVector>& Points=bPursuing?PursuitRoute:Route;
    int32& Index=bPursuing?PursuitRouteIndex:RouteIndex;
    if(Points.IsEmpty()) { Drive=Turn=0.f; Brake=.7f; return; }
    Index=FMath::Clamp(Index,0,Points.Num()-1);
    FVector Delta=Points[Index]-GetActorLocation(); Delta.Z=0.f;
    const bool Passed=FVector::DotProduct(Delta,GetActorForwardVector())<0.f && Delta.Size2D()<1700.f;
    if(Delta.Size2D()<850.f || Passed)
    {
        if(Index<Points.Num()-1) ++Index;
        else if(!bPursuing) Index=0;
        Delta=Points[Index]-GetActorLocation(); Delta.Z=0.f;
    }
    if(bPursuing && FVector::Dist2D(GetActorLocation(),NearestRoad(PursuitLocation))<500.f && !PursuitTarget.IsValid())
    {
        Drive=Turn=0.f; Brake=1.f; return;
    }
    // A visible target on the same nearby roadway may be approached directly.
    if(bPursuing && PursuitTarget.IsValid() && FVector::Dist2D(GetActorLocation(),PursuitLocation)<1800.f &&
        FVector::Dist2D(PursuitLocation,NearestRoad(PursuitLocation))<650.f)
    {
        FHitResult Block;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceRoadApproach),false,this);
        if(!GetWorld()->LineTraceSingleByChannel(Block,GetActorLocation()+FVector(0,0,100),PursuitLocation+FVector(0,0,100),ECC_WorldStatic,Params))
        { Delta=PursuitLocation-GetActorLocation(); Delta.Z=0.f; }
    }
    const FVector Local=GetActorTransform().InverseTransformVectorNoScale(Delta.GetSafeNormal2D());
    const float Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    Turn=FMath::Clamp(Angle/38.f,-1.f,1.f);
    const float CornerWeight=FMath::Clamp(FMath::Abs(Angle)/65.f,0.f,1.f);
    float Desired=FMath::Lerp(bPursuing?2600.f:1350.f,bPursuing?750.f:600.f,CornerWeight);
    if(Delta.Size2D()<1300.f) Desired=FMath::Min(Desired,bPursuing?950.f:750.f);
    if(bPursuing && Delta.Size2D()<550.f) Desired=FMath::Min(Desired,300.f);
    Drive=FMath::Clamp(.12f+(Desired-Speed)*.0015f,0.f,bPursuing?.85f:.6f);
    Brake=Speed>Desired+120.f?FMath::Clamp((Speed-Desired)*.0015f,0.f,.9f):0.f;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(TrafficObstacle),false,this);
    FHitResult Obstacle;
    const FVector Forward=GetActorForwardVector().GetSafeNormal2D();
    const FVector Start=GetActorLocation()+Forward*290.f+FVector(0,0,80.f);
    const FVector End=Start+Forward*(350.f+FMath::Max(Speed,0.f)*.7f);
    const bool bBlocked=GetWorld()->SweepSingleByChannel(Obstacle,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(70.f),Params);
    const bool bPedestrianAhead=bBlocked && (Cast<ANCPedestrian>(Obstacle.GetActor()) || Cast<ANCHero>(Obstacle.GetActor()));
    const ANCCar* CarAhead=bBlocked?Cast<ANCCar>(Obstacle.GetActor()):nullptr;
    const bool bCarAhead=CarAhead!=nullptr;
    if(bBlocked)
    {
        Drive=0.f;
        Brake=1.f;
        if(auto* Person=Cast<ANCPedestrian>(Obstacle.GetActor())) Person->PanicFrom(GetActorLocation(),2.f);
    }
    TrafficWaitTime=bCarAhead && FMath::Abs(Speed)<100.f?TrafficWaitTime+Dt:0.f;
    if(AvoidanceTime<=0.f && TrafficWaitTime>2.f && CarAhead &&
        (CarAhead->GetCondition()<=0.f || CarAhead->GetSpeedKmh()<2.f))
    {
        // Pass a stationary obstruction only if a capsule-sized corridor remains
        // on a road and is clear of cars, people and buildings. Do not endlessly
        // queue behind a wreck, or replace it by teleporting out of the scene.
        for(float Side:{1.f,-1.f})
        {
            const FVector Candidate=GetActorLocation()+Forward*1100.f+GetActorRightVector().GetSafeNormal2D()*Side*600.f;
            if(FVector::Dist2D(Candidate,NearestRoad(Candidate))>600.f) continue;
            FHitResult Clearance;
            const FVector ProbeStart=GetActorLocation()+FVector(0,0,150);
            const FVector ProbeEnd=Candidate+FVector(0,0,150);
            if(!GetWorld()->SweepSingleByChannel(Clearance,ProbeStart,ProbeEnd,FQuat::Identity,ECC_Visibility,
                FCollisionShape::MakeSphere(120.f),Params))
            {
                AvoidanceTarget=Candidate;
                AvoidanceTime=3.f;
                TrafficWaitTime=0.f;
                break;
            }
        }
    }
    if(AvoidanceTime>0.f)
    {
        AvoidanceTime-=Dt;
        const FVector AvoidDelta=AvoidanceTarget-GetActorLocation();
        FHitResult Clearance;
        const FVector ProbeStart=GetActorLocation()+FVector(0,0,150);
        if(AvoidDelta.Size2D()<250.f || GetWorld()->SweepSingleByChannel(Clearance,ProbeStart,
            AvoidanceTarget+FVector(0,0,150),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(120.f),Params))
            AvoidanceTime=0.f;
        else
        {
            const FVector AvoidLocal=GetActorTransform().InverseTransformVectorNoScale(AvoidDelta.GetSafeNormal2D());
            Turn=FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(AvoidLocal.Y,AvoidLocal.X))/38.f,-1.f,1.f);
            Drive=FMath::Clamp((650.f-Speed)*.001f,0.f,.3f);
            Brake=Speed>700.f?.6f:0.f;
            return;
        }
    }
    // A stopped vehicle queues behind people and other cars. For a wall or missed turn,
    // it checks behind before a short reversing manoeuvre; no visible respawn/teleport.
    if(FMath::Abs(Speed)<80.f && Desired>200.f && !bPedestrianAhead && !bCarAhead) StuckTime+=Dt;
    else StuckTime=0.f;
    if(StuckTime>4.f && UnstickCooldown<=0.f)
    {
        FHitResult Behind;
        const FVector BackStart=GetActorLocation()-Forward*280.f+FVector(0,0,130.f);
        if(!GetWorld()->SweepSingleByChannel(Behind,BackStart,BackStart-Forward*600.f,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(95.f),Params))
        { ReverseTime=1.7f; UnstickCooldown=7.f; }
        StuckTime=0.f;
    }
    if(ReverseTime>0.f)
    {
        FHitResult Behind;
        const FVector BackStart=GetActorLocation()-Forward*280.f+FVector(0,0,130.f);
        if(GetWorld()->SweepSingleByChannel(Behind,BackStart,BackStart-Forward*500.f,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(95.f),Params))
        {
            ReverseTime=0.f;
            Drive=0.f;
            Brake=1.f;
            return;
        }
        ReverseTime-=Dt;
        Drive=-.35f; Brake=0.f; Turn=-FMath::Sign(Angle)*.7f;
        if(ReverseTime<=0.f && bPursuing) BuildPursuitRoute();
    }
}

bool ANCCar::FindExit(FVector& Out) const
{
    // Use yaw only so a rolled car does not rotate the exit probes underground.
    const FTransform ExitFrame(FRotator(0.f,GetActorRotation().Yaw,0.f),GetActorLocation());
    const FVector Offsets[]={FVector(0,-225,0),FVector(0,225,0),FVector(-360,0,0),FVector(360,0,0)};
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BailoutClearance),false,this);
    if(Driver) Params.AddIgnoredActor(Driver);
    for(const FVector& Offset:Offsets)
    {
        FVector Point=ExitFrame.TransformPositionNoScale(Offset);
        FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,Point+FVector(0,0,250),Point-FVector(0,0,400),ECC_WorldStatic,Params) || Floor.Normal.Z<.7f) continue;
        Point=Floor.ImpactPoint+FVector(0,0,100);
        if(GetWorld()->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(36,94),Params)) continue;
        FHitResult Path;
        const FVector Start=GetActorLocation()+FVector(0,0,100);
        if(GetWorld()->SweepSingleByChannel(Path,Start,Point,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(30),Params)) continue;
        Out=Point;
        return true;
    }
    return false;
}
void ANCCar::ExitCar()
{
    auto* PC=Cast<APlayerController>(Controller);
    if(!PC || !IsValid(Driver)) return;
    FVector Exit;
    if(!FindExit(Exit))
    {
        if(GEngine) GEngine->AddOnScreenDebugMessage(-1,3.f,FColor::Orange,TEXT("Exit blocked. Move clear or press T for road recovery."));
        return;
    }
    ANCHero* Hero=Driver;
    const float ExitSpeed=GetSpeedKmh();
    const FVector Momentum=GetMesh()->GetPhysicsLinearVelocity()*.65f+(Exit-GetActorLocation()).GetSafeNormal2D()*260.f;
    Driver=nullptr;
    ClearDrivingInput();
    Hero->LeaveCar(Exit,Momentum,ExitSpeed);
    PC->Possess(Hero);
    PC->SetControlRotation(FRotator(-12.f,GetActorRotation().Yaw,0.f));
    UpdateLights();
}

void ANCCar::OnVehicleHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit)
{
    if(!IsValid(OtherActor) || OtherActor==this || OtherActor==Driver || HitCooldown>0.f) return;
    const bool bPerson=Cast<ANCPedestrian>(OtherActor)!=nullptr;
    const bool bHero=Cast<ANCHero>(OtherActor)!=nullptr;
    const bool bCar=Cast<ANCCar>(OtherActor)!=nullptr;
    if(!bPerson && !bHero && !bCar && Hit.ImpactNormal.Z>.6f) return;
    const FVector Relative=PreviousVelocity-OtherActor->GetVelocity();
    const float Approach=FMath::Abs(FVector::DotProduct(Relative,Hit.ImpactNormal));
    const float ImpulseSpeed=NormalImpulse.Size()/FMath::Max(100.f,Vehicle->Mass);
    const float Impact=FMath::Max(Approach,ImpulseSpeed);
    if(Impact<220.f) return;
    HitCooldown=.65f;
    const float VehicleDamage=FMath::Clamp((Impact-350.f)*.008f,0.f,35.f);
    Condition=FMath::Max(0.f,Condition-VehicleDamage);
    if(Condition<=0.f) UpdateLights();
    if(bPerson || bHero)
        UGameplayStatics::ApplyDamage(OtherActor,FMath::Clamp((Impact-120.f)*.035f,5.f,85.f),GetController(),this,UDamageType::StaticClass());
    if(Driver && Impact>1500.f)
        UGameplayStatics::ApplyDamage(Driver,FMath::Clamp((Impact-1500.f)*.004f,0.f,12.f),nullptr,OtherActor,UDamageType::StaticClass());
    if(Driver && GetController() && GetController()->IsPlayerController() && bCar && PreviousVelocity.Size2D()>400.f)
        if(auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>())
            Gameplay->ReportCrime(Cast<ANCCar>(OtherActor)->IsPoliceVehicle()?1.f:.35f,GetActorLocation());
}
float ANCCar::TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser)
{
    if(!FMath::IsFinite(DamageAmount) || DamageAmount<=0.f || Condition<=0.f) return 0.f;
    const float Applied=FMath::Min(Condition,DamageAmount);
    Condition-=Applied;
    if(Condition<=0.f) UpdateLights();
    if((EventInstigator && EventInstigator->IsPlayerController()) || Cast<ANCHero>(DamageCauser))
        if(auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>()) Gameplay->ReportCrime(bPolice?1.f:.5f,GetActorLocation());
    return Applied;
}

void ANCCar::Recover()
{
    const FVector Current=GetActorLocation().Z<-1000.f?LastSafeTransform.GetLocation():GetActorLocation();
    FVector Position=NearestRoad(Current);
    const bool bVertical=FMath::Abs(Position.X-RoadCoordinate(Position.X))<1.f;
    const FRotator Rotation(0.f,bVertical?90.f:0.f,0.f);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleRecovery),false,this);
    if(Driver) Params.AddIgnoredActor(Driver);
    bool bFound=false;
    const float Offsets[]={0.f,850.f,-850.f,1700.f,-1700.f};
    for(float Offset:Offsets)
    {
        FVector Candidate=Position+(bVertical?FVector(0,Offset,0):FVector(Offset,0,0));
        if(FMath::Abs(Candidate.X)>RoadLimit || FMath::Abs(Candidate.Y)>RoadLimit) continue;
        FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,Candidate+FVector(0,0,600),Candidate-FVector(0,0,200),ECC_WorldStatic,Params)) continue;
        Candidate.Z=Floor.ImpactPoint.Z+85.f;
        if(GetWorld()->OverlapBlockingTestByChannel(Candidate,Rotation.Quaternion(),ECC_Vehicle,FCollisionShape::MakeBox(FVector(260,125,65)),Params)) continue;
        Position=Candidate; bFound=true; break;
    }
    if(!bFound) return;
    ClearDrivingInput();
    Vehicle->StopMovementImmediately();
    SetActorLocationAndRotation(Position,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
    GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
    GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    Vehicle->ResetVehicle();
    Vehicle->SetSleeping(false);
    Condition=FMath::Max(Condition,40.f);
    Speed=0.f;
    PreviousVelocity=FVector::ZeroVector;
    if(Controller) Controller->SetControlRotation(FRotator(-12.f,Rotation.Yaw,0.f));
    RoutePlanTime=0.f;
    if(bHasPursuitLocation) BuildPursuitRoute();
}
void ANCCar::Repair() { Condition=100.f; UpdateLights(); }
void ANCCar::TryService()
{
    if(auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>()) Gameplay->TryInteract(this);
}
void ANCCar::UpdatePaint()
{
    if(!Body || !HasActorBegunPlay()) return;
    if(PaintMaterials.IsEmpty())
        for(int32 I=0;I<Body->GetNumMaterials();++I)
            if(auto* Material=Body->CreateAndSetMaterialInstanceDynamic(I)) PaintMaterials.Add(Material);
    for(const auto& Material:PaintMaterials) if(Material) Material->SetVectorParameterValue(TEXT("Paint Tint"),Paint);
}
void ANCCar::ToggleHeadlights() { bHeadlights=!bHeadlights; UpdateLights(); }
void ANCCar::UpdateLights()
{
    // Street cars retain their baked lamp surfaces; dynamic beams are reserved for
    // the currently occupied car and active police to keep the street inexpensive.
    const bool bBeams=bHeadlights && Condition>0.f && (Driver || bPolice);
    for(const auto& Light:Headlights) Light->SetVisibility(bBeams);
    const bool bAlert=bPolice && !Driver && bHasPursuitLocation && Condition>0.f;
    const int32 Flash=GetWorld()?FMath::FloorToInt(GetWorld()->GetTimeSeconds()*7.f)%2:0;
    for(int32 I=0;I<PoliceLights.Num();++I) PoliceLights[I]->SetVisibility(bAlert && I==Flash);
}
void ANCCar::LookYaw(float V) { if(FMath::Abs(V)>.001f) LookIdle=0.f; AddControllerYawInput(V*.8f); }
void ANCCar::LookPitch(float V) { if(FMath::Abs(V)>.001f) LookIdle=0.f; AddControllerPitchInput(V*.7f); }
void ANCCar::PadYaw(float V) { LookYaw(V*100.f*GetWorld()->GetDeltaSeconds()); }
void ANCCar::PadPitch(float V) { LookPitch(V*75.f*GetWorld()->GetDeltaSeconds()); }
void ANCCar::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis("Forward",this,&ANCCar::Forward); Input->BindAxis("Right",this,&ANCCar::Right);
    Input->BindAxis("Throttle",this,&ANCCar::Throttle); Input->BindAxis("Reverse",this,&ANCCar::Reverse);
    Input->BindAxis("LookYaw",this,&ANCCar::LookYaw); Input->BindAxis("LookPitch",this,&ANCCar::LookPitch);
    Input->BindAxis("PadYaw",this,&ANCCar::PadYaw); Input->BindAxis("PadPitch",this,&ANCCar::PadPitch);
    Input->BindAction("Interact",IE_Pressed,this,&ANCCar::ExitCar);
    // A release in a pause/map menu must not leave the handbrake or boost latched.
    Input->BindAction("Brake",IE_Pressed,this,&ANCCar::BrakeOn); Input->BindAction("Brake",IE_Released,this,&ANCCar::BrakeOff).bExecuteWhenPaused=true;
    Input->BindAction("Boost",IE_Pressed,this,&ANCCar::BoostOn); Input->BindAction("Boost",IE_Released,this,&ANCCar::BoostOff).bExecuteWhenPaused=true;
    Input->BindAction("Recover",IE_Pressed,this,&ANCCar::Recover);
    Input->BindAction("Greet",IE_Pressed,this,&ANCCar::TryService);
    Input->BindAction("Headlights",IE_Pressed,this,&ANCCar::ToggleHeadlights);
}
