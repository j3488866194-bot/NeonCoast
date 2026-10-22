#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NCCharacterVisual.generated.h"

class ACharacter;
class USkeletalMesh;
class UAnimSequence;
class UMaterialInterface;

// Shared presentation for the player and civilians; their capsules still own collision.
UCLASS()
class NEONCOAST_API UNCCharacterVisual : public UActorComponent
{
    GENERATED_BODY()
public:
    UNCCharacterVisual();
    bool Configure(bool bHero, int32 AppearanceSeed = 0);
    bool IsConfigured() const { return bConfigured; }
    void UpdateAnimation(float Speed, bool bFalling, bool bIncapacitated = false);

private:
    enum class EPose : uint8 { None, Idle, Walk, Run, Jump };
    UPROPERTY() TObjectPtr<ACharacter> Character;
    UPROPERTY() TObjectPtr<USkeletalMesh> EricMesh;
    UPROPERTY() TObjectPtr<USkeletalMesh> CarlaMesh;
    UPROPERTY() TObjectPtr<UAnimSequence> EricIdle;
    UPROPERTY() TObjectPtr<UAnimSequence> EricWalk;
    UPROPERTY() TObjectPtr<UAnimSequence> EricRun;
    UPROPERTY() TObjectPtr<UAnimSequence> EricJump;
    UPROPERTY() TObjectPtr<UAnimSequence> CarlaIdle;
    UPROPERTY() TObjectPtr<UAnimSequence> CarlaWalk;
    UPROPERTY() TObjectPtr<UAnimSequence> CarlaRun;
    UPROPERTY() TObjectPtr<UAnimSequence> CarlaJump;
    UPROPERTY() TObjectPtr<UMaterialInterface> EricMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> CarlaMaterial;
    UPROPERTY() TObjectPtr<USkeletalMesh> HumanMesh;
    UPROPERTY() TObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> RunAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> JumpAnimation;
    EPose Pose = EPose::None;
    float VisualScale = 1.f;
    float WalkCycleSpeed = 0.f;
    float RunCycleSpeed = 0.f;
    bool bConfigured = false;
};
