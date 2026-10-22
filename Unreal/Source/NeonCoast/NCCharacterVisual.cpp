#include "NCCharacterVisual.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "ReferenceSkeleton.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
bool ReferencePosition(const FReferenceSkeleton& Skeleton, const TCHAR* BoneName, FVector& Position)
{
    int32 Index = Skeleton.FindBoneIndex(FName(BoneName));
    if (Index == INDEX_NONE) return false;
    FTransform Transform = Skeleton.GetRefBonePose()[Index];
    while ((Index = Skeleton.GetParentIndex(Index)) != INDEX_NONE)
        Transform *= Skeleton.GetRefBonePose()[Index];
    Position = Transform.GetTranslation();
    return !Position.ContainsNaN();
}

bool ReferenceFacingYaw(const USkeletalMesh* Mesh, float& Yaw)
{
    // Imported foot-to-toe vectors resolve FBX axis conversion without rotating
    // animation bones. Averaging both feet cancels the reference pose's toe-out.
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    FVector LeftFoot, LeftToe, RightFoot, RightToe;
    if (!ReferencePosition(Skeleton, TEXT("foot_l"), LeftFoot) ||
        !ReferencePosition(Skeleton, TEXT("ball_l"), LeftToe) ||
        !ReferencePosition(Skeleton, TEXT("foot_r"), RightFoot) ||
        !ReferencePosition(Skeleton, TEXT("ball_r"), RightToe)) return false;
    const FVector Forward = (LeftToe - LeftFoot) + (RightToe - RightFoot);
    if (Forward.SizeSquared2D() < 1.f) return false;
    Yaw = -FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
    return FMath::IsFinite(Yaw);
}

bool IsInPlaceClip(const UAnimSequence* Clip)
{
    return Clip && !Clip->bEnableRootMotion && Clip->bForceRootLock &&
        Clip->RootMotionRootLock == ERootMotionRootLock::AnimFirstFrame;
}

bool AuthoredCycleSpeed(const UAnimSequence* Clip, float& Speed)
{
    const double Duration = Clip->GetPlayLength();
    if (Duration <= 0.1) return false;
    // ExtractRootMotionFromRange reads the retained root track independently of
    // force-root-lock. It calibrates cadence; CharacterMovement owns translation.
    const FAnimExtractContext Context(0.0, false);
    const FTransform Travel = Clip->ExtractRootMotionFromRange(0.0, Duration, Context);
    Speed = Travel.GetTranslation().Size2D() / Duration;
    return FMath::IsFinite(Speed) && Speed > 25.f && Speed < 1200.f;
}
}

UNCCharacterVisual::UNCCharacterVisual()
{
    PrimaryComponentTick.bCanEverTick = false;
    // Hard references retain both complete casts in cooked builds. Optional loads
    // allow an unprepared local source checkout to log its primitive fallback.
    static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> EricMeshAsset(TEXT("/Game/RealisticCharacters/Eric/SK_Eric.SK_Eric"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> CarlaMeshAsset(TEXT("/Game/RealisticCharacters/Carla/SK_Carla.SK_Carla"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> EricIdleAsset(TEXT("/Game/RealisticCharacters/Eric/Animations/A_Eric_Idle.A_Eric_Idle"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> EricWalkAsset(TEXT("/Game/RealisticCharacters/Eric/Animations/A_Eric_Walk.A_Eric_Walk"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> EricRunAsset(TEXT("/Game/RealisticCharacters/Eric/Animations/A_Eric_Run.A_Eric_Run"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> EricJumpAsset(TEXT("/Game/RealisticCharacters/Eric/Animations/A_Eric_Jump.A_Eric_Jump"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> CarlaIdleAsset(TEXT("/Game/RealisticCharacters/Carla/Animations/A_Carla_Idle.A_Carla_Idle"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> CarlaWalkAsset(TEXT("/Game/RealisticCharacters/Carla/Animations/A_Carla_Walk.A_Carla_Walk"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> CarlaRunAsset(TEXT("/Game/RealisticCharacters/Carla/Animations/A_Carla_Run.A_Carla_Run"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> CarlaJumpAsset(TEXT("/Game/RealisticCharacters/Carla/Animations/A_Carla_Jump.A_Carla_Jump"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> EricSkin(TEXT("/Game/RealisticCharacters/Materials/M_Eric.M_Eric"), LOAD_NoWarn | LOAD_Quiet);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> CarlaSkin(TEXT("/Game/RealisticCharacters/Materials/M_Carla.M_Carla"), LOAD_NoWarn | LOAD_Quiet);
    EricMesh = EricMeshAsset.Get();
    CarlaMesh = CarlaMeshAsset.Get();
    EricIdle = EricIdleAsset.Get();
    EricWalk = EricWalkAsset.Get();
    EricRun = EricRunAsset.Get();
    EricJump = EricJumpAsset.Get();
    CarlaIdle = CarlaIdleAsset.Get();
    CarlaWalk = CarlaWalkAsset.Get();
    CarlaRun = CarlaRunAsset.Get();
    CarlaJump = CarlaJumpAsset.Get();
    EricMaterial = EricSkin.Get();
    CarlaMaterial = CarlaSkin.Get();
}

bool UNCCharacterVisual::Configure(bool bHero, int32 AppearanceSeed)
{
    if (bConfigured) return true;
    Character = Cast<ACharacter>(GetOwner());
    const bool bCarla = !bHero && (uint32(AppearanceSeed) & 1u) != 0;
    HumanMesh = bCarla ? CarlaMesh : EricMesh;
    IdleAnimation = bCarla ? CarlaIdle : EricIdle;
    WalkAnimation = bCarla ? CarlaWalk : EricWalk;
    RunAnimation = bCarla ? CarlaRun : EricRun;
    JumpAnimation = bCarla ? CarlaJump : EricJump;
    UMaterialInterface* ExpectedMaterial = bCarla ? CarlaMaterial.Get() : EricMaterial.Get();
    if (!Character || !Character->GetMesh() || !Character->GetCapsuleComponent() || !HumanMesh || !ExpectedMaterial ||
        !IdleAnimation || !WalkAnimation || !RunAnimation || !JumpAnimation ||
        IdleAnimation->GetSkeleton() != HumanMesh->GetSkeleton() ||
        WalkAnimation->GetSkeleton() != HumanMesh->GetSkeleton() ||
        RunAnimation->GetSkeleton() != HumanMesh->GetSkeleton() ||
        JumpAnimation->GetSkeleton() != HumanMesh->GetSkeleton() ||
        !IsInPlaceClip(IdleAnimation) || !IsInPlaceClip(WalkAnimation) ||
        !IsInPlaceClip(RunAnimation) || !IsInPlaceClip(JumpAnimation))
    {
        UE_LOG(LogTemp, Warning, TEXT("NEON_COAST_CHARACTER_FALLBACK actor=%s: character assets unavailable or incompatible"), *GetNameSafe(GetOwner()));
        return false;
    }

    bool bHasSourceMaterial = false;
    for (const FSkeletalMaterial& Slot : HumanMesh->GetMaterials())
        bHasSourceMaterial |= Slot.MaterialInterface == ExpectedMaterial;
    float FacingYaw = 0.f;
    if (!bHasSourceMaterial || !ReferenceFacingYaw(HumanMesh, FacingYaw) ||
        !AuthoredCycleSpeed(WalkAnimation, WalkCycleSpeed) || !AuthoredCycleSpeed(RunAnimation, RunCycleSpeed))
    {
        UE_LOG(LogTemp, Warning, TEXT("NEON_COAST_CHARACTER_FALLBACK actor=%s: scanned material, reference facing or authored stride unavailable"), *GetNameSafe(GetOwner()));
        return false;
    }

    const FBoxSphereBounds Bounds = HumanMesh->GetImportedBounds();
    const float SourceHeight = Bounds.BoxExtent.Z * 2.f;
    if (!FMath::IsFinite(SourceHeight) || SourceHeight < 150.f || SourceHeight > 200.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("NEON_COAST_CHARACTER_FALLBACK actor=%s: scan unit/orientation invalid height=%.2f"), *GetNameSafe(GetOwner()), SourceHeight);
        return false;
    }
    FRandomStream Appearance(AppearanceSeed);
    VisualScale = bHero ? 1.f : Appearance.FRandRange(.97f, 1.03f);
    const float TargetHeight = SourceHeight * VisualScale;
    const float FeetZ = -Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
        - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * VisualScale;

    USkeletalMeshComponent* Mesh = Character->GetMesh();
    Mesh->SetSkeletalMesh(HumanMesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(true);
    Mesh->SetRelativeScale3D(FVector(VisualScale));
    Mesh->SetRelativeRotation(FRotator(0.f, FacingYaw, 0.f));
    Mesh->SetRelativeLocation(FVector(0.f, 0.f, FeetZ));
    Character->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
    // Keep the scan's original UVs and the per-slot materials assigned at import.
    Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    bConfigured = true;
    UpdateAnimation(0.f, false);
    UE_LOG(LogTemp, Display, TEXT("NEON_COAST_CHARACTER_VISUAL actor=%s mesh=%s skin=%s height=%.2f sourceHeight=%.2f scale=%.4f yaw=%.2f materials=%d walk=%s walkCycleSpeed=%.2f runCycleSpeed=%.2f"),
        *Character->GetName(), *HumanMesh->GetName(), *ExpectedMaterial->GetName(), TargetHeight, SourceHeight,
        VisualScale, FacingYaw, Mesh->GetNumMaterials(), *WalkAnimation->GetName(), WalkCycleSpeed, RunCycleSpeed);
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
    const float MovingThreshold = (Pose == EPose::Walk || Pose == EPose::Run) ? 8.f : 15.f;
    // Hysteresis avoids switching clips every frame around either speed boundary.
    const float RunThreshold = Pose == EPose::Run ? 280.f : 320.f;
    const EPose Next = bFalling ? EPose::Jump : (Speed <= MovingThreshold ? EPose::Idle :
        (Speed > RunThreshold ? EPose::Run : EPose::Walk));
    if (Next != Pose)
    {
        Pose = Next;
        UAnimSequence* Clip = Pose == EPose::Jump ? JumpAnimation.Get() : (Pose == EPose::Run ? RunAnimation.Get() :
            (Pose == EPose::Walk ? WalkAnimation.Get() : IdleAnimation.Get()));
        Mesh->PlayAnimation(Clip, Pose != EPose::Jump);
    }
    if (UAnimSingleNodeInstance* Player = Mesh->GetSingleNodeInstance())
    {
        // Separate authored gaits; uniform height variation also scales stride.
        const float Rate = Pose == EPose::Walk ? FMath::Clamp(Speed / (WalkCycleSpeed * VisualScale), .1f, 2.f) :
            (Pose == EPose::Run ? FMath::Clamp(Speed / (RunCycleSpeed * VisualScale), .1f, 2.f) : 1.f);
        Player->SetPlayRate(Rate);
        Player->SetPlaying(true);
    }
}
