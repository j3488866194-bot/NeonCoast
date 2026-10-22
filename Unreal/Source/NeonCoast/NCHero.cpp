#include "NCHero.h"
#include "NCCar.h"
#include "NCPedestrian.h"
#include "NCGameplaySubsystem.h"
#include "NCAudioSubsystem.h"
#include "NCPrimitives.h"
#include "NCCharacterVisual.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "DrawDebugHelpers.h"

ANCHero::ANCHero()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    GetCharacterMovement()->MaxWalkSpeed = 510.f;
    GetCharacterMovement()->JumpZVelocity = 560.f;
    GetCharacterMovement()->AirControl = .32f;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0, 560, 0);
    bUseControllerRotationYaw = false;
    CharacterVisual = CreateDefaultSubobject<UNCCharacterVisual>(TEXT("CharacterVisual"));
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
    if (!CharacterVisual->Configure(true))
    {
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
}
void ANCHero::Tick(float Dt)
{
    Super::Tick(Dt);
    DamageGraceTime = FMath::Max(0.f, DamageGraceTime - Dt);
    RecoveryTime = FMath::Max(0.f, RecoveryTime - Dt);
    FireCooldown = FMath::Max(0.f, FireCooldown - Dt);
    if (DeathRecoveryTime > 0.f)
    {
        CharacterVisual->UpdateAnimation(0.f, false, true);
        DeathRecoveryTime = FMath::Max(0.f, DeathRecoveryTime - Dt);
        if (DeathRecoveryTime <= 0.f) Recover();
        return;
    }
    if (IsHidden()) return;
    if (ReloadTime > 0.f)
    {
        ReloadTime = FMath::Max(0.f, ReloadTime - Dt);
        if (ReloadTime <= 0.f)
        {
            const int32 Transfer = FMath::Min(12 - Ammo, ReserveAmmo);
            Ammo += Transfer;
            ReserveAmmo -= Transfer;
        }
    }
    if (bFireHeld && FireCooldown <= 0.f && !IsReloading()) FireShot();
    Arm->TargetArmLength = FMath::FInterpTo(Arm->TargetArmLength, bAiming ? 240.f : 450.f, Dt, 12.f);
    Arm->SocketOffset = FMath::VInterpTo(Arm->SocketOffset, FVector(0, bAiming ? 65.f : 35.f, 65), Dt, 12.f);
    Camera->FieldOfView = FMath::FInterpTo(Camera->FieldOfView, bAiming ? 58.f : 72.f, Dt, 10.f);
    const float Speed = GetVelocity().Size2D();
    if (CharacterVisual->IsConfigured())
        CharacterVisual->UpdateAnimation(Speed, GetCharacterMovement()->IsFalling());
    else
    {
        Gait += Dt * Speed * .018f;
        for (int32 I=0; I<Legs.Num(); ++I)
            Legs[I]->SetRelativeRotation(FRotator(FMath::Sin(Gait + I*PI)*FMath::Min(Speed/510.f,1.f)*32.f,0,0));
    }
    if (GetActorLocation().Z < -1500 || GetActorLocation().Size2D() > 60000) Recover();
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
    // Releases must clear held actions even when a menu has paused gameplay.
    Input->BindAction("Jump",IE_Released,this,&ACharacter::StopJumping).bExecuteWhenPaused=true;
    Input->BindAction("Sprint",IE_Pressed,this,&ANCHero::BeginSprint);
    Input->BindAction("Sprint",IE_Released,this,&ANCHero::EndSprint).bExecuteWhenPaused=true;
    Input->BindAction("OnFootInteract",IE_Pressed,this,&ANCHero::Greet);
    Input->BindAction("Aim",IE_Pressed,this,&ANCHero::BeginAim);
    Input->BindAction("Aim",IE_Released,this,&ANCHero::EndAim).bExecuteWhenPaused=true;
    Input->BindAction("Fire",IE_Pressed,this,&ANCHero::BeginFire);
    Input->BindAction("Fire",IE_Released,this,&ANCHero::EndFire).bExecuteWhenPaused=true;
    Input->BindAction("Reload",IE_Pressed,this,&ANCHero::Reload);
    Input->BindAction("Recover",IE_Pressed,this,&ANCHero::Recover);
}
void ANCHero::Forward(float V) { if (Controller && !IsRecovering()) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),V); }
void ANCHero::Right(float V) { if (Controller && !IsRecovering()) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V); }
void ANCHero::LookYaw(float V) { if (!IsDefeated()) AddControllerYawInput(V*(bAiming?.45f:.8f)); }
void ANCHero::LookPitch(float V) { if (!IsDefeated()) AddControllerPitchInput(V*(bAiming?.4f:.7f)); }
void ANCHero::PadYaw(float V) { if (!IsDefeated()) AddControllerYawInput(V*(bAiming?60.f:100.f)*GetWorld()->GetDeltaSeconds()); }
void ANCHero::PadPitch(float V) { if (!IsDefeated()) AddControllerPitchInput(V*(bAiming?45.f:75.f)*GetWorld()->GetDeltaSeconds()); }
void ANCHero::TryJump() { if(!IsRecovering()) Jump(); }
void ANCHero::UpdateMovementSpeed() { GetCharacterMovement()->MaxWalkSpeed=bAiming?280.f:(bSprinting?830.f:510.f); }
void ANCHero::BeginSprint() { bSprinting=true; UpdateMovementSpeed(); }
void ANCHero::EndSprint() { bSprinting=false; UpdateMovementSpeed(); }
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
    EndFire(); EndAim(); EndSprint(); ReloadTime=0;
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
    if (IsHidden())
    {
        for(TActorIterator<ANCCar> It(GetWorld());It;++It)
        {
            if(It->GetDriverHero()!=this) continue;
            It->Recover();
            It->ExitCar();
            break;
        }
        if(IsHidden()) { DeathRecoveryTime=.5f; return; }
    }
    EndFire(); EndAim(); EndSprint();
    ReloadTime=0;
    SetActorLocation(FVector(21900,3150,110),false,nullptr,ETeleportType::TeleportPhysics);
    SetActorEnableCollision(true);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCharacterMovement()->StopMovementImmediately();
    RestoreHealth(); DamageGraceTime=3.f;
    // Recovery always supplies a usable magazine; ammunition is otherwise finite.
    Ammo=FMath::Max(Ammo,12); ReserveAmmo=FMath::Max(ReserveAmmo,24);
}
void ANCHero::RestoreHealth()
{
    Health=100; RecoveryTime=0; DeathRecoveryTime=0;
}
void ANCHero::Greet()
{
    if(IsRecovering() || IsDefeated()) return;
    if(auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>())
        if(Gameplay->TryInteract(this)) return;
    ANCPedestrian* Closest=nullptr; float Best=350;
    for(TActorIterator<ANCPedestrian> It(GetWorld()); It; ++It)
    {
        const float D=FVector::Dist(GetActorLocation(),It->GetActorLocation());
        if(D<Best && !It->IsIncapacitated()) { Best=D; Closest=*It; }
    }
    if(Closest) Closest->Greet(this);
}

void ANCHero::BeginAim()
{
    if(IsRecovering() || IsHidden()) return;
    bAiming=true;
    bUseControllerRotationYaw=true;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    UpdateMovementSpeed();
}
void ANCHero::EndAim()
{
    bAiming=false;
    bUseControllerRotationYaw=false;
    GetCharacterMovement()->bOrientRotationToMovement=true;
    UpdateMovementSpeed();
}
void ANCHero::BeginFire()
{
    if(IsRecovering() || IsHidden()) return;
    bFireHeld=true;
    FireShot();
}
void ANCHero::EndFire() { bFireHeld=false; }
void ANCHero::Reload()
{
    if(IsRecovering() || IsHidden() || IsReloading() || Ammo>=12 || ReserveAmmo<=0) return;
    ReloadTime=1.4f;
    if (auto* Audio = GetWorld()->GetSubsystem<UNCAudioSubsystem>()) Audio->PlayReload(GetActorLocation());
}
void ANCHero::FireShot()
{
    if(IsRecovering() || IsHidden() || IsReloading() || FireCooldown>0.f) return;
    auto* PC=Cast<APlayerController>(Controller);
    if(!PC) return;
    if(Ammo<=0) { Reload(); return; }
    --Ammo; FireCooldown=.24f;

    FVector ViewLocation; FRotator ViewRotation;
    PC->GetPlayerViewPoint(ViewLocation,ViewRotation);
    const FVector ViewDirection=ViewRotation.Vector();
    const FVector ShotDirection=FMath::VRandCone(ViewDirection,FMath::DegreesToRadians(bAiming?.15f:1.1f));
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NeonCoastAim),true,this);
    FVector AimPoint=ViewLocation+ShotDirection*18000.f;
    FHitResult CameraHit;
    if(GetWorld()->LineTraceSingleByChannel(CameraHit,ViewLocation,AimPoint,ECC_Visibility,Params))
        AimPoint=CameraHit.ImpactPoint;
    const FVector Muzzle=GetActorLocation()+FVector(0,0,48)+ViewDirection*55.f+ViewRotation.RotateVector(FVector(0,18,0));
    if (auto* Audio = GetWorld()->GetSubsystem<UNCAudioSubsystem>()) Audio->PlayGunshot(Muzzle);
    // An obstruction behind the shoulder cannot turn a shot back through the hero.
    if(FVector::DotProduct(AimPoint-Muzzle,ViewDirection)<10.f)
        AimPoint=Muzzle+ShotDirection*100.f;
    // The camera hit is on a surface. Trace slightly through it so rounding
    // from the different muzzle origin cannot stop the ray just outside it.
    // The muzzle query still returns the nearest obstruction, including cover.
    const FVector TraceEnd=CameraHit.bBlockingHit ? AimPoint+(AimPoint-Muzzle).GetSafeNormal()*2.f : AimPoint;
    FHitResult Hit;
    const bool bHit=GetWorld()->LineTraceSingleByChannel(Hit,Muzzle,TraceEnd,ECC_Visibility,Params);
    if(bHit && Hit.GetActor())
        UGameplayStatics::ApplyPointDamage(Hit.GetActor(),35.f,(AimPoint-Muzzle).GetSafeNormal(),Hit,Controller,this,UDamageType::StaticClass());
    const FVector End=bHit?Hit.ImpactPoint:AimPoint;
    DrawDebugLine(GetWorld(),Muzzle,End,FColor(255,213,128),false,.07f,0,.8f);
    if(bHit) DrawDebugPoint(GetWorld(),End,5.f,FColor(255,226,173),false,.12f);
    for(TActorIterator<ANCPedestrian> It(GetWorld());It;++It)
        if(FVector::DistSquared(It->GetActorLocation(),Muzzle)<FMath::Square(2500.f)) It->PanicFrom(Muzzle,5.f);
    if(auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>()) Gameplay->ReportCrime(.45f,GetActorLocation());
    UE_LOG(LogTemp,Verbose,TEXT("NEON_COAST_SHOT ammo=%d cameraActor=%s actor=%s impact=%s muzzle=%s"),
        Ammo,*GetNameSafe(CameraHit.GetActor()),*GetNameSafe(Hit.GetActor()),*AimPoint.ToString(),*Muzzle.ToString());
}
float ANCHero::TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser)
{
    if(DamageAmount<=0.f || IsDefeated() || DamageGraceTime>0.f) return 0.f;
    const float Applied=FMath::Min(Health,DamageAmount);
    Health=FMath::Max(0.f,Health-Applied);
    if(IsDefeated())
    {
        EndFire(); EndAim(); EndSprint(); ReloadTime=0;
        RecoveryTime=DeathRecoveryTime=3.f;
        GetCharacterMovement()->StopMovementImmediately();
        if(auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>()) Gameplay->OnPlayerDefeated();
        UE_LOG(LogTemp,Display,TEXT("NEON_COAST_HERO_DEFEATED"));
    }
    return Applied;
}
