#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NCPedestrian.generated.h"
class UTextRenderComponent;
class UNCCharacterVisual;
UCLASS()
class NEONCOAST_API ANCPedestrian : public ACharacter
{
    GENERATED_BODY()
public:
    ANCPedestrian();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
    void Greet(AActor* Speaker);
    void PanicFrom(const FVector& Threat, float Duration = 4.f);
    bool IsIncapacitated() const { return IncapacitatedTime > 0.f; }
    bool IsPanicking() const { return PanicTime > 0.f; }
    float GetHealth() const { return Health; }
    int32 AppearanceSeed=0;
    FVector WalkAxis=FVector(0,1,0);
    float RouteHalfLength=750;
protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UNCCharacterVisual> CharacterVisual;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Limbs;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Speech;
    UPROPERTY() TObjectPtr<AActor> ConversationPartner;
    FVector Origin;
    float Direction=1, PauseTime=0, PanicTime=0, ConversationTime=0, Gait=0, SenseClock=0;
    float Health=60.f, IncapacitatedTime=0.f, ObstacleClock=0.f, StuckTime=0.f;
    FVector AvoidanceDirection=FVector::ZeroVector;
    FVector LastLocation=FVector::ZeroVector;
    void NoticeVehicles();
    FVector FindWalkDirection();
    void ReturnToRoute();
};
