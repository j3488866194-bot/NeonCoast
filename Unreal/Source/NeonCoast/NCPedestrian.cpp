#include "NCPedestrian.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCGameplaySubsystem.h"
#include "NCPrimitives.h"
#include "NCCharacterVisual.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"

ANCPedestrian::ANCPedestrian()
{
    PrimaryActorTick.bCanEverTick=true;
    GetCapsuleComponent()->InitCapsuleSize(30,88);
    // Pedestrians may pass one another, but vehicles, architecture and bullets still hit them.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Vehicle,ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    GetCharacterMovement()->MaxWalkSpeed=115;
    GetCharacterMovement()->bRunPhysicsWithNoController=true;
    GetCharacterMovement()->bOrientRotationToMovement=true;
    GetCharacterMovement()->RotationRate=FRotator(0,300,0);
    bUseControllerRotationYaw=false;
    CharacterVisual=CreateDefaultSubobject<UNCCharacterVisual>(TEXT("CharacterVisual"));
}
void ANCPedestrian::BeginPlay()
{
    Super::BeginPlay();
    Origin=GetActorLocation();
    LastLocation=Origin;
    WalkAxis.Z=0;
    WalkAxis=WalkAxis.GetSafeNormal();
    if(WalkAxis.IsNearlyZero()) WalkAxis=FVector::YAxisVector;
    Direction=AppearanceSeed%2?1.f:-1.f;
    GetCharacterMovement()->MaxWalkSpeed=95+AppearanceSeed%55;
    if(!CharacterVisual->Configure(false,AppearanceSeed))
    {
        const FLinearColor Shirts[]={FLinearColor(.86,.48,.23),FLinearColor(.06,.47,.49),FLinearColor(.70,.22,.36),FLinearColor(.75,.74,.65),FLinearColor(.3,.25,.48),FLinearColor(.17,.23,.3)};
        const FLinearColor Skins[]={FLinearColor(.73,.46,.29),FLinearColor(.32,.16,.09),FLinearColor(.88,.64,.43),FLinearColor(.50,.28,.16)};
        const FLinearColor Shirt=Shirts[AppearanceSeed%UE_ARRAY_COUNT(Shirts)];
        const FLinearColor Skin=Skins[(AppearanceSeed/3)%UE_ARRAY_COUNT(Skins)];
        NCPrimitives::Part(this,RootComponent,TEXT("Torso"),TEXT("Cube"),FVector(0,0,27),FVector(.28,.44,.54),Shirt);
        NCPrimitives::Part(this,RootComponent,TEXT("Head"),TEXT("Sphere"),FVector(0,0,74),FVector(.32,.30,.37),Skin);
        NCPrimitives::Part(this,RootComponent,TEXT("Hair"),TEXT("Sphere"),FVector(-3,0,87),FVector(.31,.31,.15),FLinearColor(.08,.05,.035));
        if(AppearanceSeed%3==0) NCPrimitives::Part(this,RootComponent,TEXT("Cap"),TEXT("Cylinder"),FVector(4,0,92),FVector(.44,.38,.05),Shirt);
        for(int32 Side=-1;Side<=1;Side+=2)
        {
            auto* Leg=NewObject<USceneComponent>(this);
            Leg->SetupAttachment(RootComponent); Leg->SetRelativeLocation(FVector(0,Side*12,-8));
            Leg->RegisterComponent(); AddInstanceComponent(Leg); Limbs.Add(Leg);
            NCPrimitives::Part(this,Leg,*FString::Printf(TEXT("Trousers%d"),Side),TEXT("Cube"),FVector(0,0,-31),FVector(.21,.18,.60),FLinearColor(.06,.085,.13));
            NCPrimitives::Part(this,Leg,*FString::Printf(TEXT("Shoe%d"),Side),TEXT("Cube"),FVector(6,0,-72),FVector(.30,.19,.13),FLinearColor(.8,.78,.70));
            auto* Arm=NewObject<USceneComponent>(this);
            Arm->SetupAttachment(RootComponent); Arm->SetRelativeLocation(FVector(0,Side*28,48));
            Arm->RegisterComponent(); AddInstanceComponent(Arm); Limbs.Add(Arm);
            NCPrimitives::Part(this,Arm,*FString::Printf(TEXT("Sleeve%d"),Side),TEXT("Cube"),FVector(0,0,-10),FVector(.18,.17,.23),Shirt);
            NCPrimitives::Part(this,Arm,*FString::Printf(TEXT("Forearm%d"),Side),TEXT("Cylinder"),FVector(0,0,-35),FVector(.12,.12,.32),Skin);
        }
    }
    Speech=NewObject<UTextRenderComponent>(this,TEXT("Speech"));
    Speech->SetupAttachment(RootComponent); Speech->SetRelativeLocation(FVector(0,0,130));
    Speech->SetHorizontalAlignment(EHTA_Center); Speech->SetWorldSize(19);
    Speech->SetTextRenderColor(FColor(255,236,204)); Speech->SetVisibility(false);
    Speech->RegisterComponent(); AddInstanceComponent(Speech);
}
void ANCPedestrian::NoticeVehicles()
{
    if(IsIncapacitated()) return;
    for(TActorIterator<ANCCar> It(GetWorld());It;++It)
    {
        const FVector Away=GetActorLocation()-It->GetActorLocation();
        if(It->GetSpeedKmh()<18 || Away.SizeSquared2D()>FMath::Square(1100.f)) continue;
        if(Away.SizeSquared2D()<FMath::Square(450.f) || FVector::DotProduct(Away.GetSafeNormal2D(),It->GetActorForwardVector())>.2f)
        {
            PanicFrom(It->GetActorLocation(),3.f);
            break;
        }
    }
}
void ANCPedestrian::Tick(float Dt)
{
    Super::Tick(Dt);
    if(IsIncapacitated())
    {
        CharacterVisual->UpdateAnimation(0.f,false,true);
        IncapacitatedTime=FMath::Max(0.f,IncapacitatedTime-Dt);
        if(IncapacitatedTime<=0.f) ReturnToRoute();
        return;
    }
    if(GetActorLocation().Z<-1200 || FVector::Dist2D(GetActorLocation(),Origin)>FMath::Max(4500.f,RouteHalfLength*3.f))
    {
        ReturnToRoute();
        return;
    }
    SenseClock-=Dt; if(SenseClock<=0) { SenseClock=.25f; NoticeVehicles(); }
    ObstacleClock-=Dt;
    PanicTime=FMath::Max(0.f,PanicTime-Dt); PauseTime=FMath::Max(0.f,PauseTime-Dt);
    ConversationTime=FMath::Max(0.f,ConversationTime-Dt);
    if(ConversationTime>0 && IsValid(ConversationPartner))
    {
        const FVector To=ConversationPartner->GetActorLocation()-GetActorLocation();
        SetActorRotation(FRotator(0,To.Rotation().Yaw,0));
        GetCharacterMovement()->StopMovementImmediately();
        if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
            Speech->SetWorldRotation((Camera->GetCameraLocation()-Speech->GetComponentLocation()).Rotation());
    }
    else
    {
        Speech->SetVisibility(false);
        const float Along=FVector::DotProduct(GetActorLocation()-Origin,WalkAxis);
        const float TurnDistance=RouteHalfLength+(PanicTime>0.f?1400.f:0.f);
        if(Along*Direction>TurnDistance) { Direction*=-1; PauseTime=PanicTime>0?0.f:.5f; }
        GetCharacterMovement()->MaxWalkSpeed=PanicTime>0?390.f:95.f+AppearanceSeed%55;
        if(PauseTime<=0)
        {
            if(ObstacleClock<=0.f)
            {
                ObstacleClock=.18f;
                AvoidanceDirection=FindWalkDirection();
            }
            AddMovementInput(AvoidanceDirection,1.f,true);
        }
        if(PauseTime<=0.f && FVector::DistSquared2D(GetActorLocation(),LastLocation)<FMath::Square(Dt*12.f))
        {
            StuckTime+=Dt;
            if(StuckTime>1.2f)
            {
                Direction*=-1.f; ObstacleClock=0.f; PauseTime=.2f; StuckTime=0.f;
            }
        }
        else StuckTime=0.f;
    }
    LastLocation=GetActorLocation();
    if(CharacterVisual->IsConfigured())
        CharacterVisual->UpdateAnimation(GetVelocity().Size2D(),GetCharacterMovement()->IsFalling());
    else
    {
        Gait+=Dt*GetVelocity().Size2D()*.035f;
        for(int32 I=0;I<Limbs.Num();++I)
        {
            const float Sign=(I==0 || I==3)?1.f:-1.f;
            float Angle=FMath::Sin(Gait)*Sign*FMath::Min(GetVelocity().Size2D()/110.f,1.5f)*24.f;
            if(ConversationTime>0 && I==3) Angle=-110+FMath::Sin(ConversationTime*8)*10;
            Limbs[I]->SetRelativeRotation(FRotator(Angle,0,0));
        }
    }
}
void ANCPedestrian::Greet(AActor* Speaker)
{
    if(PanicTime>0 || IsIncapacitated() || !IsValid(Speaker)) return;
    static const TCHAR* Lines[]={TEXT("Beautiful evening, right?"),TEXT("The boardwalk is just over there."),TEXT("Take it easy on these roads!"),TEXT("Welcome to Neon Coast."),TEXT("Best sunset in the city.")};
    ConversationPartner=Speaker; ConversationTime=4;
    Speech->SetText(FText::FromString(Lines[AppearanceSeed%UE_ARRAY_COUNT(Lines)]));
    Speech->SetVisibility(true);
}
void ANCPedestrian::PanicFrom(const FVector& Threat,float Duration)
{
    if(IsIncapacitated()) return;
    Direction=FVector::DotProduct(GetActorLocation()-Threat,WalkAxis)>=0.f?1.f:-1.f;
    PanicTime=FMath::Max(PanicTime,Duration);
    PauseTime=ConversationTime=0.f;
    ConversationPartner=nullptr;
    ObstacleClock=0.f;
    if(Speech) Speech->SetVisibility(false);
}
FVector ANCPedestrian::FindWalkDirection()
{
    const FVector Offset=GetActorLocation()-Origin;
    const FVector SideOffset=Offset-WalkAxis*FVector::DotProduct(Offset,WalkAxis);
    FVector Desired=(WalkAxis*Direction-SideOffset.GetClampedToMaxSize(400.f)*.0018f).GetSafeNormal2D();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NeonCoastPedestrianAvoid),false,this);
    const FVector Start=GetActorLocation();
    const auto Clear=[&](const FVector& Travel)
    {
        FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Start,Start+Travel*130.f,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(27.f),Params)) return false;
        // Do not step over the promenade edge or a substantial drop.
        FHitResult Floor;
        return GetWorld()->LineTraceSingleByChannel(Floor,Start+Travel*140.f,Start+Travel*140.f-FVector(0,0,180),ECC_WorldStatic,Params);
    };
    if(Clear(Desired)) return Desired;
    const float PreferredSide=AppearanceSeed%2?1.f:-1.f;
    for(const float Angle:{65.f,-65.f,110.f,-110.f})
    {
        const FVector Alternative=Desired.RotateAngleAxis(Angle*PreferredSide,FVector::UpVector);
        if(Clear(Alternative)) return Alternative;
    }
    Direction*=-1.f;
    return FVector::ZeroVector;
}
void ANCPedestrian::ReturnToRoute()
{
    SetActorEnableCollision(false);
    SetActorRotation(FRotator(0,WalkAxis.Rotation().Yaw,0));
    SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);
    SetActorEnableCollision(true);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCharacterMovement()->StopMovementImmediately();
    Health=60.f; IncapacitatedTime=PanicTime=ConversationTime=StuckTime=0.f;
    ConversationPartner=nullptr; PauseTime=.7f; ObstacleClock=0.f;
    LastLocation=Origin;
    if(Speech) Speech->SetVisibility(false);
}
float ANCPedestrian::TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser)
{
    if(DamageAmount<=0.f || IsIncapacitated()) return 0.f;
    const float Applied=FMath::Min(Health,DamageAmount);
    Health=FMath::Max(0.f,Health-Applied);
    const FVector Threat=IsValid(DamageCauser)?DamageCauser->GetActorLocation():GetActorLocation()-WalkAxis*100.f;
    PanicFrom(Threat,8.f);
    if(Cast<ANCHero>(DamageCauser) || (EventInstigator && EventInstigator->IsPlayerController()))
        if(auto* Gameplay=GetWorld()->GetSubsystem<UNCGameplaySubsystem>()) Gameplay->ReportCrime(1.5f,GetActorLocation());
    if(Health<=0.f)
    {
        IncapacitatedTime=18.f;
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        SetActorEnableCollision(false);
        SetActorRotation(FRotator(0,GetActorRotation().Yaw,75.f));
        Speech->SetVisibility(false);
        UE_LOG(LogTemp,Display,TEXT("NEON_COAST_PEDESTRIAN_DOWN actor=%s"),*GetName());
    }
    return Applied;
}
