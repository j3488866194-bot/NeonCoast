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
    enum class EPose : uint8 { None, Idle, Run, Jump };
    UPROPERTY() TObjectPtr<ACharacter> Character;
    UPROPERTY() TObjectPtr<USkeletalMesh> HumanMesh;
    UPROPERTY() TObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> RunAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> JumpAnimation;
    UPROPERTY() TObjectPtr<UMaterialInterface> HeroMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> MaleMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> FemaleMaterial;
    EPose Pose = EPose::None;
    bool bConfigured = false;
};
