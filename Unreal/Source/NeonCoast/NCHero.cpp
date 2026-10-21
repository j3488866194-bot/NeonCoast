#include "NCHero.h"
#include "NCCar.h"
#include "NCPedestrian.h"
#include "NCPrimitives.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

ANCHero::ANCHero()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);
    GetCharacterMovement()->MaxWalkSpeed = 510.f;
    GetCharacterMovement()->JumpZVelocity = 560.f;
    GetCharacterMovement()->AirControl = .32f;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0, 560, 0);
    bUseControllerRotationYaw = false;
    Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("FollowArm"));
    Arm->SetupAttachment(RootComponent);
    Arm->TargetArmLength = 450;
    Arm->SocketOffset = FVector(0, 35, 65);
    Arm->bUsePawnControlRotation = true;
    Arm->bEnableCameraLag = true;
    Arm->CameraLagSpeed = 14;
    Arm->bUseCameraLagSubstepping = true;
    Arm->PrimaryComponentTick.TickGroup = TG_PostPhysics;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Arm);
    Camera->FieldOfView = 72;
}
void ANCHero::BeginPlay()
{
    Super::BeginPlay();
    Figure = NewObject<USceneComponent>(this, TEXT("Figure"));
    Figure->SetupAttachment(RootComponent);
    Figure->SetRelativeLocation(FVector(0, 0, -90));
    Figure->RegisterComponent();
    AddInstanceComponent(Figure);
    NCPrimitives::Part(this, Figure, TEXT("Shirt"), TEXT("Cube"), FVector(0, 0, 119), FVector(.30,.48,.57), FLinearColor(.03,.51,.48));
    NCPrimitives::Part(this, Figure, TEXT("Head"), TEXT("Sphere"), FVector(0, 0, 164), FVector(.33,.31,.40), FLinearColor(.60,.35,.22));
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        auto* Leg = NewObject<USceneComponent>(this);
        Leg->SetupAttachment(Figure);
        Leg->SetRelativeLocation(FVector(0, Side * 13, 83));
        Leg->RegisterComponent(); AddInstanceComponent(Leg); Legs.Add(Leg);
        NCPrimitives::Part(this, Leg, *FString::Printf(TEXT("Leg%d"),Side), TEXT("Cube"), FVector(0,0,-34), FVector(.23,.19,.66), FLinearColor(.045,.065,.10));
        NCPrimitives::Part(this, Leg, *FString::Printf(TEXT("Shoe%d"),Side), TEXT("Cube"), FVector(7,0,-76), FVector(.33,.21,.14), FLinearColor(.8,.77,.65));
        NCPrimitives::Part(this, Figure, *FString::Printf(TEXT("Arm%d"),Side), TEXT("Cylinder"), FVector(0,Side*31,109), FVector(.13,.13,.60), FLinearColor(.60,.35,.22));
    }
}
void ANCHero::Tick(float Dt)
{
    Super::Tick(Dt);
    RecoveryTime = FMath::Max(0.f, RecoveryTime - Dt);
    const float Speed = GetVelocity().Size2D();
    Gait += Dt * Speed * .018f;
    for (int32 I=0; I<Legs.Num(); ++I)
        Legs[I]->SetRelativeRotation(FRotator(FMath::Sin(Gait + I*PI)*FMath::Min(Speed/510.f,1.f)*32.f,0,0));
    if (GetActorLocation().Z < -1500) Recover();
}
void ANCHero::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis("Forward",this,&ANCHero::Forward);
    Input->BindAxis("Right",this,&ANCHero::Right);
    Input->BindAxis("LookYaw",this,&ANCHero::LookYaw);
    Input->BindAxis("LookPitch",this,&ANCHero::LookPitch);
    Input->BindAxis("PadYaw",this,&ANCHero::PadYaw);
    Input->BindAxis("PadPitch",this,&ANCHero::PadPitch);
    Input->BindAction("Interact",IE_Pressed,this,&ANCHero::TryEnterCar);
    Input->BindAction("Jump",IE_Pressed,this,&ANCHero::TryJump);
    Input->BindAction("Jump",IE_Released,this,&ACharacter::StopJumping);
    Input->BindAction("Sprint",IE_Pressed,this,&ANCHero::BeginSprint);
    Input->BindAction("Sprint",IE_Released,this,&ANCHero::EndSprint);
    Input->BindAction("Greet",IE_Pressed,this,&ANCHero::Greet);
    Input->BindAction("Recover",IE_Pressed,this,&ANCHero::Recover);
}
void ANCHero::Forward(float V) { if (Controller && !IsRecovering()) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),V); }
void ANCHero::Right(float V) { if (Controller && !IsRecovering()) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V); }
void ANCHero::LookYaw(float V) { AddControllerYawInput(V*.8f); }
void ANCHero::LookPitch(float V) { AddControllerPitchInput(V*.7f); }
void ANCHero::PadYaw(float V) { AddControllerYawInput(V*100.f*GetWorld()->GetDeltaSeconds()); }
void ANCHero::PadPitch(float V) { AddControllerPitchInput(V*75.f*GetWorld()->GetDeltaSeconds()); }
void ANCHero::TryJump() { if(!IsRecovering()) Jump(); }
void ANCHero::BeginSprint() { GetCharacterMovement()->MaxWalkSpeed=830; }
void ANCHero::EndSprint() { GetCharacterMovement()->MaxWalkSpeed=510; }
void ANCHero::TryEnterCar()
{
    if (IsRecovering()) return;
    ANCCar* Closest=nullptr; float Best=460;
    for(TActorIterator<ANCCar> It(GetWorld()); It; ++It)
    {
        const float Distance=FVector::Dist(GetActorLocation(),It->GetActorLocation());
        if(Distance<Best && It->CanEnter()) { Closest=*It; Best=Distance; }
    }
    if(Closest) EnterCar(Closest);
}
void ANCHero::EnterCar(ANCCar* Car)
{
    auto* PC=Cast<APlayerController>(Controller);
    if(!PC || !Car || !Car->CanEnter()) return;
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    SetActorEnableCollision(false); SetActorHiddenInGame(true);
    Car->SetDriver(this);
    PC->Possess(Car);
    PC->SetControlRotation(FRotator(-12,Car->GetActorRotation().Yaw,0));
}
void ANCHero::LeaveCar(FVector Position, FVector Momentum, float SpeedKmh)
{
    SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
    SetActorEnableCollision(true); SetActorHiddenInGame(false);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    EndSprint();
    if(SpeedKmh>18)
    {
        Health=FMath::Max(15.f,Health-FMath::Clamp((SpeedKmh-18)*.32f,0.f,45.f));
        RecoveryTime=.85f;
        LaunchCharacter(Momentum.GetClampedToMaxSize(1400)+FVector(0,0,360),true,true);
    }
}
void ANCHero::Recover()
{
    SetActorLocation(FVector(21900,3150,110),false,nullptr,ETeleportType::TeleportPhysics);
    GetCharacterMovement()->StopMovementImmediately();
    Health=100; RecoveryTime=0;
}
void ANCHero::Greet()
{
    ANCPedestrian* Closest=nullptr; float Best=350;
    for(TActorIterator<ANCPedestrian> It(GetWorld()); It; ++It)
    {
        const float D=FVector::Dist(GetActorLocation(),It->GetActorLocation());
        if(D<Best) { Best=D; Closest=*It; }
    }
    if(Closest) Closest->Greet(this);
}
