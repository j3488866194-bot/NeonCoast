#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NCHero.generated.h"
class USpringArmComponent;
class UCameraComponent;
class ANCCar;

UCLASS()
class NEONCOAST_API ANCHero : public ACharacter
{
    GENERATED_BODY()
public:
    ANCHero();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    float GetHealth() const { return Health; }
    bool IsRecovering() const { return RecoveryTime > 0.f; }
    void EnterCar(ANCCar* Car);
    void LeaveCar(FVector Position, FVector Momentum, float SpeedKmh);
    void Recover();
    void Greet();
protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<USceneComponent> Figure;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Legs;
    float Health = 100.f;
    float RecoveryTime = 0.f;
    float Gait = 0.f;
    void Forward(float Value);
    void Right(float Value);
    void LookYaw(float Value);
    void LookPitch(float Value);
    void PadYaw(float Value);
    void PadPitch(float Value);
    void TryEnterCar();
    void BeginSprint();
    void EndSprint();
    void TryJump();
};
