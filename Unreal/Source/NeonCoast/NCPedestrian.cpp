#include "NCPedestrian.h"
#include "NCCar.h"
#include "NCPrimitives.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"

ANCPedestrian::ANCPedestrian()
{
    PrimaryActorTick.bCanEverTick=true;
    GetCapsuleComponent()->InitCapsuleSize(30,88);
    GetCharacterMovement()->MaxWalkSpeed=115;
    GetCharacterMovement()->bRunPhysicsWithNoController=true;
    GetCharacterMovement()->bOrientRotationToMovement=true;
    GetCharacterMovement()->RotationRate=FRotator(0,300,0);
    bUseControllerRotationYaw=false;
}
void ANCPedestrian::BeginPlay()
{
    Super::BeginPlay();
    Origin=GetActorLocation();
    Direction=AppearanceSeed%2?1.f:-1.f;
    GetCharacterMovement()->MaxWalkSpeed=95+AppearanceSeed%55;
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
    Speech=NewObject<UTextRenderComponent>(this,TEXT("Speech"));
    Speech->SetupAttachment(RootComponent); Speech->SetRelativeLocation(FVector(0,0,130));
    Speech->SetHorizontalAlignment(EHTA_Center); Speech->SetWorldSize(19);
    Speech->SetTextRenderColor(FColor(255,236,204)); Speech->SetVisibility(false);
    Speech->RegisterComponent(); AddInstanceComponent(Speech);
}
void ANCPedestrian::NoticeVehicles()
{
    for(TActorIterator<ANCCar> It(GetWorld());It;++It)
    {
        if(It->GetSpeedKmh()<18 || FVector::Dist2D(GetActorLocation(),It->GetActorLocation())>1450) continue;
        Direction=FVector::DotProduct(GetActorLocation()-It->GetActorLocation(),WalkAxis)>=0?1.f:-1.f;
        PanicTime=3; PauseTime=ConversationTime=0;
        Speech->SetVisibility(false); break;
    }
}
void ANCPedestrian::Tick(float Dt)
{
    Super::Tick(Dt);
    SenseClock-=Dt; if(SenseClock<=0) { SenseClock=.25f; NoticeVehicles(); }
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
        if(Along*Direction>RouteHalfLength) { Direction*=-1; PauseTime=PanicTime>0?0.f:.5f; }
        GetCharacterMovement()->MaxWalkSpeed=PanicTime>0?390.f:95.f+AppearanceSeed%55;
        if(PauseTime<=0) AddMovementInput(WalkAxis,Direction,true);
    }
    Gait+=Dt*GetVelocity().Size2D()*.035f;
    for(int32 I=0;I<Limbs.Num();++I)
    {
        const float Sign=(I==0 || I==3)?1.f:-1.f;
        float Angle=FMath::Sin(Gait)*Sign*FMath::Min(GetVelocity().Size2D()/110.f,1.5f)*24.f;
        if(ConversationTime>0 && I==3) Angle=-110+FMath::Sin(ConversationTime*8)*10;
        Limbs[I]->SetRelativeRotation(FRotator(Angle,0,0));
    }
}
void ANCPedestrian::Greet(AActor* Speaker)
{
    if(PanicTime>0) return;
    static const TCHAR* Lines[]={TEXT("Beautiful evening, right?"),TEXT("The boardwalk is just over there."),TEXT("Take it easy on these roads!"),TEXT("Welcome to Neon Coast."),TEXT("Best sunset in the city.")};
    ConversationPartner=Speaker; ConversationTime=4;
    Speech->SetText(FText::FromString(Lines[AppearanceSeed%UE_ARRAY_COUNT(Lines)]));
    Speech->SetVisibility(true);
}
