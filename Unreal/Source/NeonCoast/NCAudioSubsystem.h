#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NCAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundAttenuation;
class APawn;
class AActor;

/** Original cached audio; synthesis and asset import happen before the game starts. */
UCLASS()
class NEONCOAST_API UNCAudioSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual TStatId GetStatId() const override;

    void CycleRadio();
    void SoundHorn();
    void PlayGunshot(FVector Location);
    void PlayReload(FVector Location);
    FString GetRadioLabel() const;
    // Also callable while paused, so applying a menu volume immediately updates loops.
    void ApplyUserVolumeSettings();
    // Menus have their own pause-independent music and coast ambience.
    void SetFrontEndActive(bool bActive);
    bool PlaySettingsPreview();

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    UPROPERTY() TArray<TObjectPtr<USoundBase>> RadioSounds;
    UPROPERTY() TObjectPtr<USoundBase> EngineSound;
    UPROPERTY() TObjectPtr<USoundBase> AmbienceSound;
    UPROPERTY() TObjectPtr<USoundBase> SirenSound;
    UPROPERTY() TObjectPtr<USoundBase> HornSound;
    UPROPERTY() TObjectPtr<USoundBase> GunshotSound;
    UPROPERTY() TObjectPtr<USoundBase> ReloadSound;
    UPROPERTY() TObjectPtr<USoundBase> FootstepSound;
    UPROPERTY() TObjectPtr<UAudioComponent> RadioLoop;
    UPROPERTY() TObjectPtr<UAudioComponent> EngineLoop;
    UPROPERTY() TObjectPtr<UAudioComponent> AmbienceLoop;
    UPROPERTY() TObjectPtr<UAudioComponent> SirenLoop;
    UPROPERTY() TObjectPtr<UAudioComponent> MenuMusicLoop;
    UPROPERTY() TObjectPtr<UAudioComponent> MenuAmbienceLoop;
    UPROPERTY() TObjectPtr<UAudioComponent> PreviewSound;
    UPROPERTY() TObjectPtr<USoundAttenuation> EffectAttenuation;
    UPROPERTY() TObjectPtr<USoundAttenuation> SirenAttenuation;

    bool bEnabled = false;
    bool bStarted = false;
    bool bAssetsLoaded = false;
    bool bFrontEndActive = false;
    int32 Station = 0;
    float Clock = 0.f;
    double RetryAt = 0.0;
    float HornReadyAt = 0.f;
    float StepReadyAt = 0.f;
    float NextSirenScan = 0.f;
    float RadioGain = 0.f;
    float EngineGain = 0.f;
    float EnginePitch = .75f;
    float SirenGain = 0.f;
    TWeakObjectPtr<AActor> NearestPolice;

    bool EnsureStarted();
    UAudioComponent* StartLoop(USoundBase* Sound, float Volume, bool bUI = false);
    void UpdateMenuAudio();
    void StopLoop(TObjectPtr<UAudioComponent>& Component);
    USoundBase* LoadSound(const TCHAR* Name);
    void UpdateSiren(float DeltaTime, APawn* Pawn);
    void UpdateFootsteps(APawn* Pawn);
};
