#if WITH_DEV_AUTOMATION_TESTS

#include "NCGameUserSettings.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCCameraDirectionTest, "NeonCoast.Settings.CameraDirections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCCameraDirectionTest::RunTest(const FString& Parameters)
{
    auto* Settings = NewObject<UNCGameUserSettings>();
    Settings->ResetCustomDefaults();
    const auto* Input = GetDefault<UInputSettings>();
    TestFalse(TEXT("hidden legacy pitch/yaw scales are disabled"), Input->bEnableLegacyInputScales);
    auto MappingScale = [this, Input](FName Axis, FKey Key)
    {
        TArray<FInputAxisKeyMapping> Matches;
        for (const auto& Mapping : Input->GetAxisMappings())
            if (Mapping.AxisName == Axis && Mapping.Key == Key) Matches.Add(Mapping);
        TestEqual(*FString::Printf(TEXT("exactly one %s mapping"), *Axis.ToString()), Matches.Num(), 1);
        return Matches.Num() == 1 ? Matches[0].Scale : 0.f;
    };

    // Installed UE5.8 SceneViewport: MouseDelta.Y -= CursorDelta.Y (line 931),
    // and Gamepad_RightY is negated before InputAxis (line 1352).
    const float PadUp = -1.f * MappingScale(TEXT("PadPitch"), EKeys::Gamepad_RightY);
    const float PadRight = MappingScale(TEXT("PadYaw"), EKeys::Gamepad_RightX);
    const float MouseUp = 1.f * MappingScale(TEXT("LookPitch"), EKeys::MouseY);
    const float MouseRight = MappingScale(TEXT("LookYaw"), EKeys::MouseX);
    TestTrue(TEXT("right stick up produces upward pitch"), Settings->TransformGamepadPitch(PadUp) > 0.f);
    TestTrue(TEXT("right stick down produces downward pitch"), Settings->TransformGamepadPitch(-PadUp) < 0.f);
    TestTrue(TEXT("right stick right produces right yaw"), Settings->TransformGamepadYaw(PadRight) > 0.f);
    TestTrue(TEXT("right stick left produces left yaw"), Settings->TransformGamepadYaw(-PadRight) < 0.f);
    TestTrue(TEXT("mouse up produces upward pitch"), Settings->TransformMousePitch(MouseUp) > 0.f);
    TestTrue(TEXT("mouse right produces right yaw"), Settings->TransformMouseYaw(MouseRight) > 0.f);
    TestTrue(TEXT("positive Unreal pitch actually points upward"), FRotator(10, 0, 0).Vector().Z > 0.f);

    Settings->SetInvertGamepadY(true);
    TestTrue(TEXT("controller inversion reverses controller pitch"), Settings->TransformGamepadPitch(PadUp) < 0.f);
    TestTrue(TEXT("controller inversion does not affect mouse"), Settings->TransformMousePitch(MouseUp) > 0.f);
    Settings->SetInvertMouseY(true);
    Settings->SetInvertGamepadY(false);
    TestTrue(TEXT("mouse inversion reverses only mouse pitch"), Settings->TransformMousePitch(MouseUp) < 0.f);
    TestTrue(TEXT("mouse inversion does not affect controller"), Settings->TransformGamepadPitch(PadUp) > 0.f);
    TestTrue(TEXT("neither inversion changes yaw"), Settings->TransformMouseYaw(MouseRight) > 0.f && Settings->TransformGamepadYaw(PadRight) > 0.f);

    Settings->SetMouseSensitivity(2.f);
    Settings->SetGamepadSensitivity(.5f);
    TestEqual(TEXT("mouse has its own sensitivity"), Settings->TransformMouseYaw(1.f), 2.f);
    TestEqual(TEXT("controller has its own sensitivity"), Settings->TransformGamepadYaw(1.f), .5f);
    Settings->SetGamepadSensitivity(1.f);
    Settings->SetGamepadDeadZone(.2f);
    TestEqual(TEXT("stick drift is suppressed"), Settings->TransformGamepadYaw(.19f), 0.f);
    TestEqual(TEXT("negative drift is suppressed"), Settings->TransformGamepadPitch(-.19f), 0.f);
    TestTrue(TEXT("remaining axis range is rescaled once"), FMath::IsNearlyEqual(Settings->TransformGamepadYaw(.6f), .5f, 1.e-6f));
    TestEqual(TEXT("full stick travel remains available"), Settings->TransformGamepadYaw(1.f), 1.f);
    TestEqual(TEXT("input noise outside normalized range is clamped"), Settings->TransformGamepadYaw(5.f), 1.f);
    auto* PlayerInput = NewObject<UPlayerInput>();
    Settings->ApplyInputSettings(*PlayerInput);
    FInputAxisProperties Props;
    TestTrue(TEXT("right stick properties exist"), PlayerInput->GetAxisProperties(EKeys::Gamepad_RightY, Props));
    TestEqual(TEXT("engine does not apply a second right stick deadzone"), Props.DeadZone, 0.f);
    TestFalse(TEXT("engine does not apply another inversion"), Props.bInvert);
    TestTrue(TEXT("left stick properties exist"), PlayerInput->GetAxisProperties(EKeys::Gamepad_LeftX, Props));
    TestEqual(TEXT("movement uses the configured deadzone"), Props.DeadZone, .2f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCSettingsRoundTripTest, "NeonCoast.Settings.ConfigRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCSettingsRoundTripTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/Settings"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini"));
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*Filename, false, true); };
    auto* Original = NewObject<UNCGameUserSettings>();
    Original->ResetCustomDefaults();
    Original->SetMouseSensitivity(1.75f);
    Original->SetGamepadSensitivity(.85f);
    Original->SetInvertMouseY(false);
    Original->SetInvertGamepadY(true);
    Original->SetGamepadDeadZone(.23f);
    Original->SetMasterVolume(.7f);
    Original->SetMusicVolume(.4f);
    Original->SetEffectsVolume(.9f);

    // Use Unreal's actual config property serializer with a private cache and a
    // unique file. Never call SaveSettings, ApplySettings, or change GConfig/CDO.
    FConfigCacheIni TestConfig(EConfigCacheType::Temporary);
    FConfigFile& File = TestConfig.Add(Filename, FConfigFile());
    File.bCanSaveAllSections = true;
    Original->SaveConfig(CPF_Config, *Filename, &TestConfig, false);
    if (!TestTrue(TEXT("options serialize to an isolated ini"), File.Write(Filename, false))) return false;
    FConfigFile ReadBack;
    ReadBack.Read(Filename);
    auto* Restored = NewObject<UNCGameUserSettings>();
    Restored->ResetCustomDefaults();
    UObject::FLoadConfigParams Params;
    Params.ConfigClass = UNCGameUserSettings::StaticClass();
    Params.OverrideFile = &ReadBack;
    Restored->LoadConfig(Params);
    TestEqual(TEXT("mouse sensitivity persists"), Restored->GetMouseSensitivity(), 1.75f);
    TestEqual(TEXT("controller sensitivity persists"), Restored->GetGamepadSensitivity(), .85f);
    TestFalse(TEXT("normal mouse Y persists"), Restored->GetInvertMouseY());
    TestTrue(TEXT("inverted controller Y persists"), Restored->GetInvertGamepadY());
    TestEqual(TEXT("deadzone persists"), Restored->GetGamepadDeadZone(), .23f);
    TestEqual(TEXT("master volume persists"), Restored->GetMasterVolume(), .7f);
    TestEqual(TEXT("music volume persists"), Restored->GetMusicVolume(), .4f);
    TestEqual(TEXT("effects volume persists"), Restored->GetEffectsVolume(), .9f);
    TestTrue(TEXT("master and music multiply"), FMath::IsNearlyEqual(Restored->GetMusicGain(), .28f));
    TestTrue(TEXT("master and effects multiply"), FMath::IsNearlyEqual(Restored->GetEffectsGain(), .63f));
    Restored->SetMasterVolume(0.f);
    TestEqual(TEXT("master mute silences music completely"), Restored->GetMusicGain(), 0.f);
    TestEqual(TEXT("master mute silences effects completely"), Restored->GetEffectsGain(), 0.f);
    Restored->ResetCustomDefaults();
    TestEqual(TEXT("reset restores sensitivity"), Restored->GetMouseSensitivity(), 1.f);
    TestFalse(TEXT("reset restores normal controller pitch"), Restored->GetInvertGamepadY());
    TestEqual(TEXT("reset restores music volume"), Restored->GetMusicVolume(), .6f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCSettingsLimitsTest, "NeonCoast.Settings.InvalidValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCSettingsLimitsTest::RunTest(const FString& Parameters)
{
    auto* Settings = NewObject<UNCGameUserSettings>();
    Settings->ResetCustomDefaults();
    Settings->SetMouseSensitivity(-100.f);
    Settings->SetGamepadSensitivity(100.f);
    TestEqual(TEXT("mouse lower limit"), Settings->GetMouseSensitivity(), .25f);
    TestEqual(TEXT("controller upper limit"), Settings->GetGamepadSensitivity(), 3.f);
    Settings->SetGamepadDeadZone(1.f);
    TestEqual(TEXT("deadzone never divides by zero"), Settings->GetGamepadDeadZone(), .35f);
    Settings->SetGamepadDeadZone(-1.f);
    TestEqual(TEXT("deadzone lower limit"), Settings->GetGamepadDeadZone(), .05f);
    Settings->SetMusicVolume(5.f);
    Settings->SetEffectsVolume(-3.f);
    TestEqual(TEXT("music cannot amplify above the authored mix"), Settings->GetMusicVolume(), 1.f);
    TestEqual(TEXT("effects cannot be negative"), Settings->GetEffectsVolume(), 0.f);
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    Settings->SetMouseSensitivity(NaN);
    Settings->SetGamepadDeadZone(NaN);
    TestEqual(TEXT("invalid sensitivity falls back safely"), Settings->GetMouseSensitivity(), 1.f);
    TestEqual(TEXT("invalid deadzone falls back safely"), Settings->GetGamepadDeadZone(), .15f);
    TestEqual(TEXT("invalid mouse samples do not corrupt camera rotation"), Settings->TransformMousePitch(NaN), 0.f);
    TestEqual(TEXT("invalid controller samples do not corrupt camera rotation"), Settings->TransformGamepadPitch(NaN), 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCVideoRecoveryTest, "NeonCoast.Settings.VideoRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCVideoRecoveryTest::RunTest(const FString& Parameters)
{
    // Model a process that ended during its confirmation countdown. The fixture
    // uses Unreal's config loader but never modifies the user's ini or display.
    FConfigFile InterruptedTrial;
    const TCHAR* Section = TEXT("/Script/NeonCoast.NCGameUserSettings");
    InterruptedTrial.SetString(Section, TEXT("ResolutionSizeX"), TEXT("3840"));
    InterruptedTrial.SetString(Section, TEXT("ResolutionSizeY"), TEXT("2160"));
    InterruptedTrial.SetString(Section, TEXT("FullscreenMode"), TEXT("0"));
    InterruptedTrial.SetString(Section, TEXT("LastUserConfirmedResolutionSizeX"), TEXT("1600"));
    InterruptedTrial.SetString(Section, TEXT("LastUserConfirmedResolutionSizeY"), TEXT("900"));
    InterruptedTrial.SetString(Section, TEXT("LastConfirmedFullscreenMode"), TEXT("2"));
    InterruptedTrial.SetBool(Section, TEXT("bVideoChangePending"), true);
    UObject::FLoadConfigParams Params;
    Params.ConfigClass = UNCGameUserSettings::StaticClass();
    Params.OverrideFile = &InterruptedTrial;
    auto* Recovered = NewObject<UNCGameUserSettings>();
    Recovered->LoadConfig(Params);
    TestTrue(TEXT("interrupted video trial is persisted"), Recovered->IsVideoChangePending());
    TestTrue(TEXT("interrupted video trial is recovered"), Recovered->RestorePendingVideoMode());
    TestEqual(TEXT("safe resolution is restored"), Recovered->GetScreenResolution(), FIntPoint(1600, 900));
    TestEqual(TEXT("safe window mode is restored"), int32(Recovered->GetFullscreenMode()), int32(EWindowMode::Windowed));
    TestFalse(TEXT("recovery clears the pending marker"), Recovered->IsVideoChangePending());
    TestTrue(TEXT("controller can detect the need to restore its viewport"), Recovered->WasVideoModeRecovered());
    TestFalse(TEXT("recovery is idempotent"), Recovered->RestorePendingVideoMode());

    auto* Kept = NewObject<UNCGameUserSettings>();
    Kept->LoadConfig(Params);
    Kept->ConfirmVideoMode();
    TestTrue(TEXT("engine resize notification cannot accept a display trial"), Kept->IsVideoChangePending());
    TestEqual(TEXT("automatic resize preserves the recovery resolution"), Kept->GetLastConfirmedScreenResolution(), FIntPoint(1600, 900));
    Kept->AcceptVideoMode();
    TestFalse(TEXT("Keep clears the pending marker"), Kept->IsVideoChangePending());
    TestEqual(TEXT("Keep updates the safe resolution"), Kept->GetLastConfirmedScreenResolution(), FIntPoint(3840, 2160));
    TestEqual(TEXT("Keep updates the safe window mode"), int32(Kept->GetLastConfirmedFullscreenMode()), int32(EWindowMode::Fullscreen));
    TestFalse(TEXT("confirmed changes are not reverted next time"), Kept->RestorePendingVideoMode());

    InterruptedTrial.SetString(Section, TEXT("LastUserConfirmedResolutionSizeX"), TEXT("0"));
    InterruptedTrial.SetString(Section, TEXT("LastUserConfirmedResolutionSizeY"), TEXT("0"));
    auto* Malformed = NewObject<UNCGameUserSettings>();
    Malformed->LoadConfig(Params);
    Malformed->RestorePendingVideoMode();
    TestEqual(TEXT("invalid recovery resolution falls back to a usable window"), Malformed->GetScreenResolution(), FIntPoint(1600, 900));
    TestEqual(TEXT("invalid recovery mode uses windowed"), int32(Malformed->GetFullscreenMode()), int32(EWindowMode::Windowed));
    auto* Fresh = NewObject<UNCGameUserSettings>();
    Fresh->SetToDefaults();
    TestTrue(TEXT("first-run defaults have a valid recovery resolution"),
        Fresh->GetLastConfirmedScreenResolution().X > 0 && Fresh->GetLastConfirmedScreenResolution().Y > 0);
    TestFalse(TEXT("first-run defaults do not start a video trial"), Fresh->IsVideoChangePending());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCSettingsMigrationTest, "NeonCoast.Settings.LegacyMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCSettingsMigrationTest::RunTest(const FString& Parameters)
{
    FConfigFile LegacyFile;
    const TCHAR* Legacy = TEXT("/Script/Engine.GameUserSettings");
    const TCHAR* Custom = TEXT("/Script/NeonCoast.NCGameUserSettings");
    LegacyFile.SetString(Legacy, TEXT("ResolutionSizeX"), TEXT("1600"));
    LegacyFile.SetString(Legacy, TEXT("ResolutionSizeY"), TEXT("900"));
    LegacyFile.SetString(Legacy, TEXT("LastUserConfirmedResolutionSizeX"), TEXT("1600"));
    LegacyFile.SetString(Legacy, TEXT("LastUserConfirmedResolutionSizeY"), TEXT("900"));
    LegacyFile.SetString(Legacy, TEXT("FullscreenMode"), TEXT("2"));
    LegacyFile.SetString(Legacy, TEXT("LastConfirmedFullscreenMode"), TEXT("2"));
    LegacyFile.SetString(Legacy, TEXT("FrameRateLimit"), TEXT("60.0"));
    LegacyFile.SetBool(Legacy, TEXT("bUseVSync"), true);
    LegacyFile.SetString(TEXT("ScalabilityGroups"), TEXT("sg.ShadowQuality"), TEXT("2"));
    auto* FirstRun = NewObject<UNCGameUserSettings>();
    FirstRun->ResetCustomDefaults();
    TestTrue(TEXT("legacy-only configuration is imported"), FirstRun->ImportLegacySettingsIfNeeded(LegacyFile));
    TestEqual(TEXT("legacy resolution is retained"), FirstRun->GetScreenResolution(), FIntPoint(1600, 900));
    TestEqual(TEXT("legacy confirmed resolution is retained"), FirstRun->GetLastConfirmedScreenResolution(), FIntPoint(1600, 900));
    TestEqual(TEXT("legacy window mode is retained"), int32(FirstRun->GetFullscreenMode()), int32(EWindowMode::Windowed));
    TestEqual(TEXT("legacy frame cap is retained"), FirstRun->GetFrameRateLimit(), 60.f);
    TestTrue(TEXT("legacy VSync is retained"), FirstRun->IsVSyncEnabled());
    TestTrue(TEXT("startup knows to apply migrated video settings"), FirstRun->WasLegacySettingsMigrated());
    TestFalse(TEXT("legacy import does not introduce controller inversion"), FirstRun->GetInvertGamepadY());
    FString Quality;
    LegacyFile.GetString(TEXT("ScalabilityGroups"), TEXT("sg.ShadowQuality"), Quality);
    TestEqual(TEXT("shared quality section remains unchanged"), Quality, FString(TEXT("2")));

    LegacyFile.SetString(Custom, TEXT("ResolutionSizeX"), TEXT("1920"));
    LegacyFile.SetString(Custom, TEXT("ResolutionSizeY"), TEXT("1080"));
    LegacyFile.SetString(Custom, TEXT("FrameRateLimit"), TEXT("120.0"));
    LegacyFile.SetBool(Custom, TEXT("bInvertGamepadY"), true);
    auto* ReturningUser = NewObject<UNCGameUserSettings>();
    UObject::FLoadConfigParams Params;
    Params.ConfigClass = UNCGameUserSettings::StaticClass();
    Params.OverrideFile = &LegacyFile;
    ReturningUser->LoadConfig(Params);
    TestFalse(TEXT("existing custom section blocks migration"), ReturningUser->ImportLegacySettingsIfNeeded(LegacyFile));
    TestEqual(TEXT("custom resolution is never overwritten"), ReturningUser->GetScreenResolution(), FIntPoint(1920, 1080));
    TestEqual(TEXT("custom frame cap is never overwritten"), ReturningUser->GetFrameRateLimit(), 120.f);
    TestTrue(TEXT("custom inversion is never overwritten"), ReturningUser->GetInvertGamepadY());
    TestFalse(TEXT("returning users are not marked as migrated"), ReturningUser->WasLegacySettingsMigrated());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCStartupDisplayTest, "NeonCoast.Settings.StartupBorderless",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCStartupDisplayTest::RunTest(const FString& Parameters)
{
    auto* Fresh = NewObject<UNCGameUserSettings>();
    Fresh->SetToDefaults();
    TestTrue(TEXT("automatic borderless is enabled by default"), Fresh->GetStartInBorderless());
    TestEqual(TEXT("default video mode is borderless"), int32(Fresh->GetFullscreenMode()), int32(EWindowMode::WindowedFullscreen));
    TestEqual(TEXT("default recovery resolution matches the desktop-sized window"), Fresh->GetLastConfirmedScreenResolution(), Fresh->GetScreenResolution());

    // Model an existing v0.4 user's window while keeping all unrelated options.
    Fresh->SetFullscreenMode(EWindowMode::Windowed);
    Fresh->SetScreenResolution(FIntPoint(1600, 900));
    Fresh->SetMasterVolume(.45f);
    Fresh->SetGamepadSensitivity(1.7f);
    Fresh->SetHasCompletedOnboarding(true);
    TestTrue(TEXT("startup prepares the configured desktop-sized window"), Fresh->PrepareStartupDisplayMode(FIntPoint(2560, 1440)));
    TestEqual(TEXT("startup selects borderless"), int32(Fresh->GetFullscreenMode()), int32(EWindowMode::WindowedFullscreen));
    TestEqual(TEXT("startup covers the desktop"), Fresh->GetScreenResolution(), FIntPoint(2560, 1440));
    TestEqual(TEXT("startup keeps a valid confirmed desktop size"), Fresh->GetLastConfirmedScreenResolution(), FIntPoint(2560, 1440));
    TestTrue(TEXT("controller can apply prepared settings with command-line overrides"), Fresh->WasStartupDisplayModeApplied());
    TestFalse(TEXT("automatic startup is not an unconfirmed display trial"), Fresh->IsVideoChangePending());
    TestEqual(TEXT("startup preserves volume"), Fresh->GetMasterVolume(), .45f);
    TestEqual(TEXT("startup preserves input sensitivity"), Fresh->GetGamepadSensitivity(), 1.7f);
    TestTrue(TEXT("startup preserves onboarding completion"), Fresh->GetHasCompletedOnboarding());
    Fresh->SetFullscreenMode(EWindowMode::Windowed);
    Fresh->SetScreenResolution(FIntPoint(1280, 720));
    Fresh->AcceptVideoMode();
    TestFalse(TEXT("startup preparation does not override later in-session choices"), Fresh->PrepareStartupDisplayMode(FIntPoint(2560, 1440)));
    TestEqual(TEXT("an in-session window remains selected"), int32(Fresh->GetFullscreenMode()), int32(EWindowMode::Windowed));

    FConfigFile Saved;
    const TCHAR* Section = TEXT("/Script/NeonCoast.NCGameUserSettings");
    Saved.SetBool(Section, TEXT("bStartInBorderless"), false);
    Saved.SetString(Section, TEXT("ResolutionSizeX"), TEXT("1280"));
    Saved.SetString(Section, TEXT("ResolutionSizeY"), TEXT("720"));
    Saved.SetString(Section, TEXT("FullscreenMode"), TEXT("2"));
    UObject::FLoadConfigParams Params;
    Params.ConfigClass = UNCGameUserSettings::StaticClass();
    Params.OverrideFile = &Saved;
    auto* OptedOut = NewObject<UNCGameUserSettings>();
    OptedOut->LoadConfig(Params);
    TestFalse(TEXT("automatic startup opt-out loads from config"), OptedOut->GetStartInBorderless());
    TestFalse(TEXT("opt-out preserves the selected mode across launches"), OptedOut->PrepareStartupDisplayMode(FIntPoint(2560, 1440)));
    TestEqual(TEXT("opt-out retains the selected window size"), OptedOut->GetScreenResolution(), FIntPoint(1280, 720));
    TestEqual(TEXT("opt-out retains the selected window mode"), int32(OptedOut->GetFullscreenMode()), int32(EWindowMode::Windowed));

    Saved.SetBool(Section, TEXT("bStartInBorderless"), true);
    Saved.SetBool(Section, TEXT("bVideoChangePending"), true);
    Saved.SetString(Section, TEXT("LastUserConfirmedResolutionSizeX"), TEXT("1600"));
    Saved.SetString(Section, TEXT("LastUserConfirmedResolutionSizeY"), TEXT("900"));
    Saved.SetString(Section, TEXT("LastConfirmedFullscreenMode"), TEXT("2"));
    auto* Recovered = NewObject<UNCGameUserSettings>();
    Recovered->LoadConfig(Params);
    TestTrue(TEXT("interrupted trial is recovered before startup preparation"), Recovered->RestorePendingVideoMode());
    TestFalse(TEXT("safe recovery takes priority over automatic borderless"), Recovered->PrepareStartupDisplayMode(FIntPoint(2560, 1440)));
    TestEqual(TEXT("recovery retains the confirmed safe resolution"), Recovered->GetScreenResolution(), FIntPoint(1600, 900));
    TestEqual(TEXT("recovery retains the confirmed safe window mode"), int32(Recovered->GetFullscreenMode()), int32(EWindowMode::Windowed));
    TestFalse(TEXT("startup application flag remains clear after recovery"), Recovered->WasStartupDisplayModeApplied());

    auto* Headless = NewObject<UNCGameUserSettings>();
    Headless->SetToDefaults();
    TestFalse(TEXT("headless startup never writes an unknown desktop size"), Headless->PrepareStartupDisplayMode(FIntPoint::ZeroValue));
    TestTrue(TEXT("headless recovery size remains valid"), Headless->GetScreenResolution().X > 0 && Headless->GetScreenResolution().Y > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCOnboardingPersistenceTest, "NeonCoast.Settings.OnboardingPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCOnboardingPersistenceTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/Settings"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini"));
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*Filename, false, true); };
    auto* Original = NewObject<UNCGameUserSettings>();
    Original->SetToDefaults();
    Original->SetHasCompletedOnboarding(false);
    TestFalse(TEXT("incomplete onboarding can be shown to new users"), Original->GetHasCompletedOnboarding());
    Original->SetHasCompletedOnboarding(true);
    Original->SetStartInBorderless(false);
    Original->SetMasterVolume(.35f);
    FConfigCacheIni TestConfig(EConfigCacheType::Temporary);
    FConfigFile& File = TestConfig.Add(Filename, FConfigFile());
    File.bCanSaveAllSections = true;
    Original->SaveConfig(CPF_Config, *Filename, &TestConfig, false);
    if (!TestTrue(TEXT("onboarding and launch preferences serialize independently of game progress"), File.Write(Filename, false))) return false;
    FConfigFile ReadBack;
    ReadBack.Read(Filename);
    auto* Restored = NewObject<UNCGameUserSettings>();
    Restored->SetHasCompletedOnboarding(false);
    Restored->SetStartInBorderless(true);
    UObject::FLoadConfigParams Params;
    Params.ConfigClass = UNCGameUserSettings::StaticClass();
    Params.OverrideFile = &ReadBack;
    Restored->LoadConfig(Params);
    TestTrue(TEXT("onboarding completion persists on restart"), Restored->GetHasCompletedOnboarding());
    TestFalse(TEXT("automatic borderless opt-out persists on restart"), Restored->GetStartInBorderless());
    TestEqual(TEXT("onboarding persistence preserves volume"), Restored->GetMasterVolume(), .35f);
    Restored->ResetCustomDefaults();
    TestTrue(TEXT("resetting custom options does not replay completed onboarding"), Restored->GetHasCompletedOnboarding());
    TestFalse(TEXT("resetting input and audio does not change the launch preference"), Restored->GetStartInBorderless());
    Restored->SetToDefaults();
    TestTrue(TEXT("full settings defaults do not replay completed onboarding"), Restored->GetHasCompletedOnboarding());
    TestTrue(TEXT("full settings defaults restore automatic borderless"), Restored->GetStartInBorderless());
    return true;
}

#endif
