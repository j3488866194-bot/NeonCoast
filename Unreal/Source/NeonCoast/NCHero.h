#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NCHero.generated.h"
class USpringArmComponent;
class UCameraComponent;
class ANCCar;
class UNCCharacterVisual;

UCLASS()
class NEONCOAST_API ANCHero : public ACharacter
{
    GENERATED_BODY()
    friend class ANCValidationRunner;
public:
    ANCHero();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
    float GetHealth() const { return Health; }
    bool IsRecovering() const { return RecoveryTime > 0.f; }
    bool IsAiming() const { return bAiming; }
    bool IsReloading() const { return ReloadTime > 0.f; }
    bool IsDefeated() const { return Health <= 0.f; }
    int32 GetAmmo() const { return Ammo; }
    int32 GetReserveAmmo() const { return ReserveAmmo; }
    float GetReloadProgress() const { return ReloadTime > 0.f ? 1.f - ReloadTime / 1.4f : 0.f; }
    void RestoreHealth();
    void EnterCar(ANCCar* Car);
    void LeaveCar(FVector Position, FVector Momentum, float SpeedKmh);
    void Recover();
    void Greet();
protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UNCCharacterVisual> CharacterVisual;
    UPROPERTY() TObjectPtr<USceneComponent> Figure;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Legs;
    float Health = 100.f;
    float RecoveryTime = 0.f;
    float Gait = 0.f;
    float FireCooldown = 0.f;
    float ReloadTime = 0.f;
    float DeathRecoveryTime = 0.f;
    float DamageGraceTime = 0.f;
    int32 Ammo = 12;
    int32 ReserveAmmo = 72;
    bool bAiming = false;
    bool bFireHeld = false;
    bool bSprinting = false;
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
    void BeginAim();
    void EndAim();
    void BeginFire();
    void EndFire();
    void FireShot();
    void Reload();
    void UpdateMovementSpeed();
};
