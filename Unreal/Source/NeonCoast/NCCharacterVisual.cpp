#include "NCCharacterVisual.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UNCCharacterVisual::UNCCharacterVisual()
{
    PrimaryComponentTick.bCanEverTick = false;
    // Hard references keep the shared mesh, clips and all three skins in packaged builds.
    // Optional loading lets source-only checkouts use the primitive fallback without an asset-error dialog.
    static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> MeshAsset(TEXT("/Game/Characters/SK_CoastHuman.SK_CoastHuman"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> IdleAsset(TEXT("/Game/Characters/A_Idle.A_Idle"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> RunAsset(TEXT("/Game/Characters/A_Run.A_Run"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> JumpAsset(TEXT("/Game/Characters/A_Jump.A_Jump"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> HeroSkin(TEXT("/Game/Characters/M_CriminalMaleA.M_CriminalMaleA"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> MaleSkin(TEXT("/Game/Characters/M_SkaterMaleA.M_SkaterMaleA"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> FemaleSkin(TEXT("/Game/Characters/M_SkaterFemaleA.M_SkaterFemaleA"), LOAD_NoWarn | LOAD_Quiet);
    HumanMesh = MeshAsset.Get();
    IdleAnimation = IdleAsset.Get();
    RunAnimation = RunAsset.Get();
    JumpAnimation = JumpAsset.Get();
    HeroMaterial = HeroSkin.Get();
    MaleMaterial = MaleSkin.Get();
    FemaleMaterial = FemaleSkin.Get();
}

bool UNCCharacterVisual::Configure(bool bHero, int32 AppearanceSeed)
{
    if (bConfigured) return true;
    Character = Cast<ACharacter>(GetOwner());
    UMaterialInterface* Skin = bHero ? HeroMaterial.Get() : ((uint32(AppearanceSeed) & 1u) ? FemaleMaterial.Get() : MaleMaterial.Get());
    if (!Character || !Character->GetMesh() || !HumanMesh || !Skin ||
        !IdleAnimation || !RunAnimation || !JumpAnimation ||
        IdleAnimation->GetSkeleton() != HumanMesh->GetSkeleton() ||
        RunAnimation->GetSkeleton() != HumanMesh->GetSkeleton() ||
        JumpAnimation->GetSkeleton() != HumanMesh->GetSkeleton())
    {
        UE_LOG(LogTemp, Warning, TEXT("NEON_COAST_CHARACTER_FALLBACK actor=%s: character assets unavailable or incompatible"), *GetNameSafe(GetOwner()));
        return false;
    }

    const FBoxSphereBounds Bounds = HumanMesh->GetBounds();
    const float SourceHeight = Bounds.BoxExtent.Z * 2.f;
    if (!FMath::IsFinite(SourceHeight) || SourceHeight <= KINDA_SMALL_NUMBER) return false;
    FRandomStream Appearance(AppearanceSeed);
    const float TargetHeight = 178.f * (bHero ? 1.f : Appearance.FRandRange(.96f, 1.03f));
    const float Scale = TargetHeight / SourceHeight;
    const float FeetZ = -Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
        - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;

    USkeletalMeshComponent* Mesh = Character->GetMesh();
    Mesh->SetSkeletalMesh(HumanMesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(true);
    Mesh->SetRelativeScale3D(FVector(Scale));
    // Opus's suggested import alignment; the integration pass must verify facing in a rendered frame.
    Mesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
    Mesh->SetRelativeLocation(FVector(0.f, 0.f, FeetZ));
    Character->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
    for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) Mesh->SetMaterial(Slot, Skin);
    Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    bConfigured = true;
    UpdateAnimation(0.f, false);
    UE_LOG(LogTemp, Display, TEXT("NEON_COAST_CHARACTER_VISUAL actor=%s skin=%s height=%.2f sourceHeight=%.2f scale=%.4f"),
        *Character->GetName(), *Skin->GetName(), TargetHeight, SourceHeight, Scale);
    return true;
}

void UNCCharacterVisual::UpdateAnimation(float Speed, bool bFalling, bool bIncapacitated)
{
    if (!bConfigured || !IsValid(Character)) return;
    USkeletalMeshComponent* Mesh = Character->GetMesh();
    if (bIncapacitated)
    {
        // Preserve the actor's existing knockdown rotation and freeze its current pose.
        if (UAnimSingleNodeInstance* Player = Mesh->GetSingleNodeInstance()) Player->SetPlaying(false);
        return;
    }
    const float MovingThreshold = Pose == EPose::Run ? 8.f : 15.f;
    const EPose Next = bFalling ? EPose::Jump : (Speed > MovingThreshold ? EPose::Run : EPose::Idle);
    if (Next != Pose)
    {
        Pose = Next;
        UAnimSequence* Clip = Pose == EPose::Jump ? JumpAnimation.Get() : (Pose == EPose::Run ? RunAnimation.Get() : IdleAnimation.Get());
        Mesh->PlayAnimation(Clip, Pose != EPose::Jump);
    }
    if (UAnimSingleNodeInstance* Player = Mesh->GetSingleNodeInstance())
    {
        // This source pack has no walk clip; slow the run cycle for strolling civilians.
        Player->SetPlayRate(Pose == EPose::Run ? FMath::Clamp(Speed / 390.f, .24f, 2.2f) : 1.f);
        Player->SetPlaying(true);
    }
}
