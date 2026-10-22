#include "NCGameUserSettings.h"
#include "NCAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
    float SafeClamp(float Value, float Min, float Max, float Fallback)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Fallback;
    }
}

UNCGameUserSettings* UNCGameUserSettings::Get()
{
    return GEngine ? Cast<UNCGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UNCGameUserSettings::SetMouseSensitivity(float V) { MouseSensitivity = SafeClamp(V, .25f, 3.f, 1.f); }
void UNCGameUserSettings::SetGamepadSensitivity(float V) { GamepadSensitivity = SafeClamp(V, .25f, 3.f, 1.f); }
void UNCGameUserSettings::SetGamepadDeadZone(float V) { GamepadDeadZone = SafeClamp(V, .05f, .35f, .15f); }
void UNCGameUserSettings::SetMasterVolume(float V) { MasterVolume = SafeClamp(V, 0.f, 1.f, 1.f); }
void UNCGameUserSettings::SetMusicVolume(float V) { MusicVolume = SafeClamp(V, 0.f, 1.f, .6f); }
void UNCGameUserSettings::SetEffectsVolume(float V) { EffectsVolume = SafeClamp(V, 0.f, 1.f, 1.f); }

void UNCGameUserSettings::ResetCustomDefaults()
{
    MouseSensitivity = GamepadSensitivity = 1.f;
    bInvertMouseY = bInvertGamepadY = false;
    GamepadDeadZone = .15f;
    MasterVolume = EffectsVolume = 1.f;
    MusicVolume = .6f;
}

void UNCGameUserSettings::SetToDefaults()
{
    Super::SetToDefaults();
    ResetCustomDefaults();
    bVideoChangePending = false;
    SetFullscreenMode(EWindowMode::WindowedFullscreen);
    const FIntPoint Desktop = GetDesktopResolution();
    SetScreenResolution(Desktop.X > 0 && Desktop.Y > 0 ? Desktop : FIntPoint(1600, 900));
    Super::ConfirmVideoMode();
    bStartInBorderless = true;
    // Onboarding is a lifetime preference. Resetting settings does not replay
    // the tutorial or clear the current process's startup preparation guard.
}

void UNCGameUserSettings::SanitizeCustomSettings()
{
    SetMouseSensitivity(MouseSensitivity);
    SetGamepadSensitivity(GamepadSensitivity);
    SetGamepadDeadZone(GamepadDeadZone);
    SetMasterVolume(MasterVolume);
    SetMusicVolume(MusicVolume);
    SetEffectsVolume(EffectsVolume);
}

void UNCGameUserSettings::LoadSettings(bool bForceReload)
{
    {
        TGuardValue<bool> LoadingGuard(bLoadingSettings, true);
        Super::LoadSettings(bForceReload);
    }
    bool bMigratedThisLoad = false;
    if (GConfig)
        if (FConfigFile* File = GConfig->FindConfigFile(GGameUserSettingsIni))
            bMigratedThisLoad = ImportLegacySettingsIfNeeded(*File);
    SanitizeCustomSettings();
    EnsureConfirmedVideoMode();
    const bool bRecoveredThisLoad = RestorePendingVideoMode();
    const bool bStartupDisplayChanged = PrepareStartupDisplayMode(GetDesktopResolution());
    if (bRecoveredThisLoad || bMigratedThisLoad || bStartupDisplayChanged)
    {
        // Avoid resolution changes, scalability application, or audio work while
        // the engine is still constructing its settings/viewport during startup.
        SaveConfig(CPF_Config, *GGameUserSettingsIni);
    }
    if (bRecoveredThisLoad)
    {
        UE_LOG(LogTemp, Display, TEXT("NC_SETTINGS_VIDEO_RECOVERED resolution=%dx%d mode=%d"),
            GetScreenResolution().X, GetScreenResolution().Y, int32(GetFullscreenMode()));
    }
    if (bMigratedThisLoad)
        UE_LOG(LogTemp, Display, TEXT("NC_SETTINGS_LEGACY_MIGRATED resolution=%dx%d mode=%d frameLimit=%.0f"),
            GetScreenResolution().X, GetScreenResolution().Y, int32(GetFullscreenMode()), GetFrameRateLimit());
    if (bStartupDisplayChanged)
        UE_LOG(LogTemp, Display, TEXT("NC_SETTINGS_STARTUP_BORDERLESS resolution=%dx%d mode=%d"),
            GetScreenResolution().X, GetScreenResolution().Y, int32(GetFullscreenMode()));
}

bool UNCGameUserSettings::PrepareStartupDisplayMode(const FIntPoint& DesktopResolution)
{
    if (bStartupDisplayPrepared) return false;
    bStartupDisplayPrepared = true;
    // A recovered or pending video trial takes priority for this entire
    // process. Reloading options must not override a safe recovered window.
    if (!bStartInBorderless || bVideoModeRecovered || bVideoChangePending) return false;
    // A headless process has no display; never persist a guessed monitor size.
    if (DesktopResolution.X <= 0 || DesktopResolution.Y <= 0) return false;
    SetFullscreenMode(EWindowMode::WindowedFullscreen);
    SetScreenResolution(DesktopResolution);
    Super::ConfirmVideoMode();
    bStartupDisplayModeApplied = true;
    return true;
}

bool UNCGameUserSettings::ImportLegacySettingsIfNeeded(FConfigFile& ConfigFile)
{
    // The engine loads the concrete class section, not its parent's section.
    // Preserve previous users' display options and the project's old defaults
    // exactly once; an existing custom section always wins, even if incomplete.
    if (ConfigFile.DoesSectionExist(TEXT("/Script/NeonCoast.NCGameUserSettings"))
        || !ConfigFile.DoesSectionExist(TEXT("/Script/Engine.GameUserSettings"))) return false;
    UObject::FLoadConfigParams Params;
    Params.ConfigClass = UGameUserSettings::StaticClass();
    Params.OverrideFile = &ConfigFile;
    LoadConfig(Params);
    // ScalabilityGroups is a separate section, already loaded by the engine and
    // retained by Super::LoadSettings. Do not replace its chosen quality levels.
    bLegacySettingsMigrated = true;
    return true;
}

void UNCGameUserSettings::ValidateSettings()
{
    Super::ValidateSettings();
    SanitizeCustomSettings();
}

void UNCGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
    ValidateSettings();
    EnsureConfirmedVideoMode();
    // Compare configured values, never the actual viewport (which -ResX/-ResY
    // may legitimately override). Only explicit UI applications start a trial.
    if (!bCheckForCommandLineOverrides
        && (GetScreenResolution() != GetLastConfirmedScreenResolution()
            || GetFullscreenMode() != GetLastConfirmedFullscreenMode()))
        bVideoChangePending = true;
    Super::ApplySettings(bCheckForCommandLineOverrides);
    ApplyCustomSettings();
    SaveSettings();
}

void UNCGameUserSettings::EnsureConfirmedVideoMode()
{
    const FIntPoint Confirmed = GetLastConfirmedScreenResolution();
    if (Confirmed.X > 0 && Confirmed.Y > 0 && LastConfirmedFullscreenMode >= 0 && LastConfirmedFullscreenMode <= 2) return;
    const FIntPoint Current = GetScreenResolution();
    if (!bVideoChangePending && Current.X > 0 && Current.Y > 0)
        Super::ConfirmVideoMode();
    else
    {
        // A first-ever or malformed config must still have a recoverable window.
        LastUserConfirmedResolutionSizeX = 1600;
        LastUserConfirmedResolutionSizeY = 900;
        LastConfirmedFullscreenMode = int32(EWindowMode::Windowed);
        LastUserConfirmedDisplayID.Empty();
        LastUserConfirmedDisplayIndex = -1;
    }
}

void UNCGameUserSettings::ConfirmVideoMode()
{
    // UE calls this automatically when a window resize completes. A successful
    // resize is not the user's Keep action, and must retain the recovery mode.
    if (bVideoChangePending) return;
    Super::ConfirmVideoMode();
}

void UNCGameUserSettings::AcceptVideoMode()
{
    Super::ConfirmVideoMode();
    bVideoChangePending = false;
}

bool UNCGameUserSettings::RestorePendingVideoMode()
{
    if (!bVideoChangePending) return false;
    EnsureConfirmedVideoMode();
    RevertVideoMode();
    bVideoChangePending = false;
    bVideoModeRecovered = true;
    return true;
}

void UNCGameUserSettings::ApplyInputSettings(UPlayerInput& Input) const
{
    FInputAxisProperties Props;
    Props.DeadZone = GamepadDeadZone;
    Props.Sensitivity = Props.Exponent = 1.f;
    Props.bInvert = false;
    Input.SetAxisProperties(EKeys::Gamepad_LeftX, Props);
    Input.SetAxisProperties(EKeys::Gamepad_LeftY, Props);
    // The camera helper below owns the right-stick deadzone. Applying the same
    // deadzone here as well would compound it and lose the fine movement range.
    Props.DeadZone = 0.f;
    Input.SetAxisProperties(EKeys::Gamepad_RightX, Props);
    Input.SetAxisProperties(EKeys::Gamepad_RightY, Props);
}

void UNCGameUserSettings::ApplyCustomSettings()
{
    SanitizeCustomSettings();
    if (!GEngine) return;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (!World || !World->IsGameWorld()) continue;
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
            if (APlayerController* PC = It->Get(); PC && PC->PlayerInput) ApplyInputSettings(*PC->PlayerInput);
        if (UNCAudioSubsystem* Audio = World->GetSubsystem<UNCAudioSubsystem>()) Audio->ApplyUserVolumeSettings();
    }
}

float UNCGameUserSettings::FilterGamepadAxis(float Value) const
{
    if (!FMath::IsFinite(Value)) return 0.f;
    const float Magnitude = FMath::Clamp(FMath::Abs(Value), 0.f, 1.f);
    return FMath::Sign(Value) * FMath::Max(0.f, Magnitude - GamepadDeadZone) / (1.f - GamepadDeadZone);
}

float UNCGameUserSettings::TransformMouseYaw(float V) const
{
    return FMath::IsFinite(V) ? V * MouseSensitivity : 0.f;
}
float UNCGameUserSettings::TransformMousePitch(float V) const
{
    return TransformMouseYaw(V) * (bInvertMouseY ? -1.f : 1.f);
}
float UNCGameUserSettings::TransformGamepadYaw(float V) const
{
    return FilterGamepadAxis(V) * GamepadSensitivity;
}
float UNCGameUserSettings::TransformGamepadPitch(float V) const
{
    return TransformGamepadYaw(V) * (bInvertGamepadY ? -1.f : 1.f);
}
