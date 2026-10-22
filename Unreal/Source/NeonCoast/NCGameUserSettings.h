#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "NCGameUserSettings.generated.h"

class UPlayerInput;
class FConfigFile;

/** Per-user options. Camera inversion is independent for mouse and controller. */
UCLASS(Config=GameUserSettings)
class NEONCOAST_API UNCGameUserSettings : public UGameUserSettings
{
    GENERATED_BODY()
public:
    static UNCGameUserSettings* Get();
    virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
    virtual void LoadSettings(bool bForceReload = false) override;
    virtual void ValidateSettings() override;
    virtual void SetToDefaults() override;
    virtual void ConfirmVideoMode() override;
    // Explicit menu confirmation. Engine resize notifications must not accept a trial.
    void AcceptVideoMode();
    void ResetCustomDefaults();
    void ApplyCustomSettings();
    void ApplyInputSettings(UPlayerInput& Input) const;
    bool IsVideoChangePending() const { return bVideoChangePending; }
    bool WasVideoModeRecovered() const { return bVideoModeRecovered; }
    bool WasLegacySettingsMigrated() const { return bLegacySettingsMigrated; }
    bool WasStartupDisplayModeApplied() const { return bStartupDisplayModeApplied; }
    bool ImportLegacySettingsIfNeeded(FConfigFile& ConfigFile);
    // Prepare once per process, preserving video recovery. No viewport or
    // command-line changes occur here; the controller applies with overrides.
    bool PrepareStartupDisplayMode(const FIntPoint& DesktopResolution);
    // State-only recovery is also used by the isolated config test. LoadSettings
    // persists the result; the controller applies it once a viewport exists.
    bool RestorePendingVideoMode();

    float GetMouseSensitivity() const { return MouseSensitivity; }
    float GetGamepadSensitivity() const { return GamepadSensitivity; }
    bool GetInvertMouseY() const { return bInvertMouseY; }
    bool GetInvertGamepadY() const { return bInvertGamepadY; }
    float GetGamepadDeadZone() const { return GamepadDeadZone; }
    float GetMasterVolume() const { return MasterVolume; }
    float GetMusicVolume() const { return MusicVolume; }
    float GetEffectsVolume() const { return EffectsVolume; }
    bool GetHasCompletedOnboarding() const { return bHasCompletedOnboarding; }
    bool GetStartInBorderless() const { return bStartInBorderless; }

    void SetMouseSensitivity(float Value);
    void SetGamepadSensitivity(float Value);
    void SetInvertMouseY(bool bValue) { bInvertMouseY = bValue; }
    void SetInvertGamepadY(bool bValue) { bInvertGamepadY = bValue; }
    void SetGamepadDeadZone(float Value);
    void SetMasterVolume(float Value);
    void SetMusicVolume(float Value);
    void SetEffectsVolume(float Value);
    void SetHasCompletedOnboarding(bool bValue) { bHasCompletedOnboarding = bValue; }
    void SetStartInBorderless(bool bValue) { bStartInBorderless = bValue; }

    // Inputs have already passed the DefaultInput.ini mappings: positive means
    // right/up. Gamepad_RightY's viewport inversion is cancelled by that mapping.
    float TransformMouseYaw(float Value) const;
    float TransformMousePitch(float Value) const;
    float TransformGamepadYaw(float Value) const;
    float TransformGamepadPitch(float Value) const;
    float GetMusicGain() const { return MasterVolume * MusicVolume; }
    float GetEffectsGain() const { return MasterVolume * EffectsVolume; }

private:
    UPROPERTY(Config) float MouseSensitivity = 1.f;
    UPROPERTY(Config) float GamepadSensitivity = 1.f;
    UPROPERTY(Config) bool bInvertMouseY = false;
    UPROPERTY(Config) bool bInvertGamepadY = false;
    UPROPERTY(Config) float GamepadDeadZone = .15f;
    UPROPERTY(Config) float MasterVolume = 1.f;
    UPROPERTY(Config) float MusicVolume = .6f;
    UPROPERTY(Config) float EffectsVolume = 1.f;
    UPROPERTY(Config) bool bVideoChangePending = false;
    UPROPERTY(Config) bool bStartInBorderless = true;
    UPROPERTY(Config) bool bHasCompletedOnboarding = false;
    bool bLoadingSettings = false;
    bool bVideoModeRecovered = false;
    bool bLegacySettingsMigrated = false;
    bool bStartupDisplayPrepared = false;
    bool bStartupDisplayModeApplied = false;

    float FilterGamepadAxis(float Value) const;
    void SanitizeCustomSettings();
    void EnsureConfirmedVideoMode();
};
