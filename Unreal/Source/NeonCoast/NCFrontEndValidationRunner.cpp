#include "NCFrontEndValidationRunner.h"
#include "NCPlayerController.h"
#include "NCGameUserSettings.h"
#include "SNCFrontEnd.h"
#include "NCHero.h"
#include "NCCar.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Layout/Children.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWidget.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
FString SettingsFilePath()
{
    // UE 5.8's known-config globals are cache keys, not physical filenames.
    if (GConfig)
        if (const FConfigBranch* Branch = GConfig->FindBranch(TEXT("GameUserSettings"), GGameUserSettingsIni))
            return FPaths::ConvertRelativePathToFull(Branch->IniPath);
    return FPaths::ConvertRelativePathToFull(FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"), nullptr, *FPaths::GeneratedConfigDir()));
}
FString JsonString(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return TEXT("\"") + Value + TEXT("\"");
}
}

ANCFrontEndValidationRunner::ANCFrontEndValidationRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void ANCFrontEndValidationRunner::Check(bool bPassed, const FString& Name)
{
    ++Checks;
    if (!bPassed) ++Failures;
    Results.Add(FString::Printf(TEXT("{\"name\":%s,\"passed\":%s}"), *JsonString(Name), bPassed ? TEXT("true") : TEXT("false")));
    UE_LOG(LogTemp, Display, TEXT("NC_MENU_CHECK %s %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), *Name);
}

bool ANCFrontEndValidationRunner::Require(bool bPassed, const FString& Name)
{
    Check(bPassed, Name);
    if (!bPassed) Finish();
    return bPassed;
}

void ANCFrontEndValidationRunner::Advance(int32 Next)
{
    Phase = Next;
    PhaseStarted = FPlatformTime::Seconds();
}

TSharedPtr<SNCFrontEnd> ANCFrontEndValidationRunner::FrontEnd() const
{
    return Controller ? Controller->GetFrontEndWidget() : nullptr;
}

TSharedPtr<SWidget> ANCFrontEndValidationRunner::FindTagged(const TSharedPtr<SWidget>& Root, FName Tag) const
{
    if (!Root || !Root->GetVisibility().IsVisible()) return nullptr;
    if (Root->GetTag() == Tag) return Root;
    FChildren* WidgetChildren = Root->GetChildren();
    if (WidgetChildren)
        for (int32 Index = 0; Index < WidgetChildren->Num(); ++Index)
            if (TSharedPtr<SWidget> Match = FindTagged(WidgetChildren->GetChildAt(Index), Tag)) return Match;
    return nullptr;
}

bool ANCFrontEndValidationRunner::FocusTag(FName Tag)
{
    TSharedPtr<SWidget> Target = FindTagged(FrontEnd(), Tag);
    if (!Require(Target.IsValid() && Target->IsEnabled(), FString::Printf(TEXT("visible enabled widget %s"), *Tag.ToString()))) return false;
    FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
    return Require(Target->HasKeyboardFocus(), FString::Printf(TEXT("real Slate focus on %s"), *Tag.ToString()));
}

bool ANCFrontEndValidationRunner::Press(FKey Key)
{
    const FKeyEvent Event(Key, FModifierKeysState(), 0u, false, 0, 0);
    const bool bDown = FSlateApplication::Get().ProcessKeyDownEvent(Event);
    const bool bUp = FSlateApplication::Get().ProcessKeyUpEvent(Event);
    return bDown || bUp;
}

bool ANCFrontEndValidationRunner::Activate(FName Tag, FKey Key)
{
    if (!FocusTag(Tag)) return false;
    return Require(Press(Key), FString::Printf(TEXT("Slate %s activates %s"), *Key.ToString(), *Tag.ToString()));
}

bool ANCFrontEndValidationRunner::SendRightStick(float Value)
{
    FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
    if (!Viewport) return false;
    // Go through the real scene-viewport boundary, including UE's physical
    // Gamepad_RightY sign conversion, then PlayerInput mappings and pawn bindings.
    FSceneViewport* Scene = static_cast<FSceneViewport*>(Viewport);
    const FAnalogInputEvent Event(EKeys::Gamepad_RightY, FModifierKeysState(), 0u, false, 0, 0, Value);
    Scene->OnAnalogValueChanged(Scene->GetCachedGeometry(), Event);
    return true;
}

void ANCFrontEndValidationRunner::Capture(const FString& Path)
{
    // bShowUI includes the actual Slate front end in the native screenshot.
    FScreenshotRequest::RequestScreenshot(Path, true, false);
    UE_LOG(LogTemp, Display, TEXT("NC_MENU_SCREENSHOT %s"), *Path);
}

void ANCFrontEndValidationRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    if (bFinished)
    {
        // A successful UI-confirm action must actually terminate the process.
        if (Failures == 0 && Now - PhaseStarted > 8.0)
        {
            bFinished = false;
            Check(false, TEXT("confirmed QuitToDesktop exits within eight seconds"));
            Finish();
        }
        return;
    }
    if (Started == 0.0)
    {
        Started = PhaseStarted = Now;
        FParse::Value(FCommandLine::Get(), TEXT("NeonCoastMenuReport="), ReportPath);
        if (ReportPath.IsEmpty()) ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FrontEndVerification/report.json"));
        ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
        TitleScreenshot = FPaths::Combine(FPaths::GetPath(ReportPath), TEXT("title.png"));
        SettingsScreenshot = FPaths::Combine(FPaths::GetPath(ReportPath), TEXT("settings.png"));
        FParse::Value(FCommandLine::Get(), TEXT("UserDir="), IsolatedUserDir);
        FString SettingsPath = SettingsFilePath();
        FString UserPath = FPaths::ConvertRelativePathToFull(IsolatedUserDir);
        FPaths::NormalizeDirectoryName(UserPath);
        if (!Require(!IsolatedUserDir.IsEmpty() && SettingsPath.StartsWith(UserPath + TEXT("/"), ESearchCase::IgnoreCase),
            TEXT("settings and progress use an explicit isolated UserDir"))) return;
        if (!Require(FSlateApplication::IsInitialized() && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport,
            TEXT("real Slate application and rendered game viewport exist"))) return;
    }
    if (Now - Started > 150.0)
    {
        Check(false, FString::Printf(TEXT("front-end suite completed before 150s wall timeout at phase %d"), Phase));
        Finish(); return;
    }
    Controller = Cast<ANCPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
    UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
    if (Phase == 0 && (!Controller || !FrontEnd() || Now - Started < 2.0)) return;
    if (!Controller || !Settings) { Check(false, TEXT("player controller and custom settings remain valid")); Finish(); return; }
    const double Age = Now - PhaseStarted;
    if (Age < .35) return;
    const auto PageIs = [this](const TCHAR* Name) { return FrontEnd() && FrontEnd()->GetPageId() == Name; };
    switch (Phase)
    {
    case 0:
        if (!Require(PageIs(TEXT("title")) && Controller->IsFrontEndOpen(), TEXT("startup automatically opens title menu"))) return;
        Check(UGameplayStatics::IsGamePaused(this), TEXT("title pauses the actual world"));
        Hero = Cast<ANCHero>(Controller->GetPawn());
        if (!Require(IsValid(Hero), TEXT("paused title keeps the actual player pawn"))) return;
        PausedLocation = Hero->GetActorLocation();
        bOriginalInvert = Settings->GetInvertGamepadY();
        Check(!bOriginalInvert, TEXT("fresh UserDir defaults to non-inverted gamepad Y"));
        OriginalResolution = Settings->GetScreenResolution();
        OriginalWindowMode = int32(Settings->GetFullscreenMode());
        Capture(TitleScreenshot);
        Advance(1); break;
    case 1:
        Check(Hero->GetActorLocation().Equals(PausedLocation, .1), TEXT("world position stays still while title is open"));
        if (!FocusTag(TEXT("Menu.Primary"))) return;
        Press(EKeys::Gamepad_DPad_Down);
        Advance(2); break;
    case 2:
        Check(FSlateApplication::Get().GetKeyboardFocusedWidget().IsValid()
            && FSlateApplication::Get().GetKeyboardFocusedWidget() != FindTagged(FrontEnd(), TEXT("Menu.Primary")), TEXT("D-pad navigates away from title primary action"));
        if (!Activate(TEXT("Menu.Settings"))) return;
        Advance(3); break;
    case 3:
        if (!Require(PageIs(TEXT("settings")), TEXT("title settings button opens settings"))) return;
        if (!Activate(TEXT("Settings.Tab.Controls"))) return;
        Advance(4); break;
    case 4:
        if (!Activate(TEXT("Settings.InvertGamepadY"), EKeys::SpaceBar)) return;
        Check(Settings->GetInvertGamepadY() == bOriginalInvert, TEXT("changing invert checkbox only edits the draft"));
        Capture(SettingsScreenshot);
        Advance(5); break;
    case 5:
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(6); break;
    case 6:
    {
        Check(Settings->GetInvertGamepadY() != bOriginalInvert, TEXT("Apply updates live independent gamepad inversion"));
        FConfigFile Disk;
        const bool bRead = IFileManager::Get().FileSize(*SettingsFilePath()) > 0;
        Disk.Read(SettingsFilePath());
        bool bPersistedInvert = bOriginalInvert;
        const bool bFound = Disk.GetBool(TEXT("/Script/NeonCoast.NCGameUserSettings"), TEXT("bInvertGamepadY"), bPersistedInvert);
        Check(bRead && bFound && bPersistedInvert != bOriginalInvert, TEXT("Apply writes gamepad inversion to isolated physical config file"));
        if (!Activate(TEXT("Settings.InvertGamepadY"), EKeys::SpaceBar)) return;
        Press(EKeys::Escape);
        Advance(7); break;
    }
    case 7:
        Check(PageIs(TEXT("title")), TEXT("Escape returns settings to title"));
        Check(Settings->GetInvertGamepadY() != bOriginalInvert, TEXT("Back discards the un-applied inversion draft"));
        if (!Activate(TEXT("Menu.Controls"))) return;
        Advance(8); break;
    case 8:
        Check(PageIs(TEXT("controls")), TEXT("controls guide opens from title"));
        Press(EKeys::Gamepad_FaceButton_Right);
        Advance(9); break;
    case 9:
        Check(PageIs(TEXT("title")), TEXT("gamepad B backs out of controls guide"));
        if (!Activate(TEXT("Menu.Settings"))) return;
        Advance(10); break;
    case 10:
        if (!Activate(TEXT("Settings.Tab.Audio"), EKeys::Gamepad_FaceButton_Bottom)) return;
        Advance(11); break;
    case 11:
        Check(PageIs(TEXT("settings")), TEXT("gamepad A activates audio settings tab"));
        if (!Activate(TEXT("Settings.Tab.Video"))) return;
        Advance(12); break;
    case 12:
        if (!Activate(TEXT("Settings.Resolution"))) return;
        Check(Settings->GetScreenResolution() == OriginalResolution, TEXT("resolution choice remains a draft before Apply"));
        Advance(13); break;
    case 13:
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(14); break;
    case 14:
        if (!Require(PageIs(TEXT("video_confirm")) && FrontEnd()->IsAwaitingVideoConfirmation(), TEXT("display mode Apply opens keep/revert confirmation"))) return;
        Check(Settings->IsVideoChangePending(), TEXT("unconfirmed display trial is marked for crash recovery"));
        if (!Activate(TEXT("Dialog.Revert"))) return;
        Advance(15); break;
    case 15:
        Check(PageIs(TEXT("settings")) && Settings->GetScreenResolution() == OriginalResolution && int32(Settings->GetFullscreenMode()) == OriginalWindowMode,
            TEXT("Revert restores the original resolution and window mode"));
        Check(!Settings->IsVideoChangePending(), TEXT("manual revert clears pending display trial"));
        if (!Activate(TEXT("Settings.Resolution"))) return;
        Advance(16); break;
    case 16:
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(17); break;
    case 17:
        if (!Require(FrontEnd()->IsAwaitingVideoConfirmation(), TEXT("second display trial is awaiting confirmation"))) return;
        Advance(18); break;
    case 18:
        if (Age < 16.0) return;
        Check(PageIs(TEXT("settings")) && !FrontEnd()->IsAwaitingVideoConfirmation(), TEXT("video countdown expires while the game world is paused"));
        Check(Settings->GetScreenResolution() == OriginalResolution && int32(Settings->GetFullscreenMode()) == OriginalWindowMode && !Settings->IsVideoChangePending(),
            TEXT("timeout restores and confirms the original display mode"));
        if (!Activate(TEXT("Settings.Resolution"))) return;
        Advance(19); break;
    case 19:
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(20); break;
    case 20:
        if (!Require(FrontEnd()->IsAwaitingVideoConfirmation(), TEXT("third display trial opens confirmation"))) return;
        if (!Activate(TEXT("Dialog.Keep"))) return;
        Advance(21); break;
    case 21:
        Check(PageIs(TEXT("settings")) && !Settings->IsVideoChangePending()
            && Settings->GetScreenResolution() == Settings->GetLastConfirmedScreenResolution()
            && Settings->GetScreenResolution() != OriginalResolution,
            TEXT("Keep commits the changed display mode"));
        if (!Activate(TEXT("Settings.WindowMode"))) return;
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(40); break;
    case 40:
        if (!Require(FrontEnd()->IsAwaitingVideoConfirmation(), TEXT("borderless mode opens confirmation"))) return;
        if (!Activate(TEXT("Dialog.Keep"))) return;
        Advance(41); break;
    case 41:
        if (Age < 1.0) return;
        Check(Settings->GetFullscreenMode() == EWindowMode::WindowedFullscreen
            && GEngine->GameViewport->Viewport->GetWindowMode() == EWindowMode::WindowedFullscreen,
            TEXT("borderless mode is applied to the actual native game viewport"));
        if (!Activate(TEXT("Settings.WindowMode"))) return;
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(42); break;
    case 42:
        if (!Require(FrontEnd()->IsAwaitingVideoConfirmation(), TEXT("fullscreen mode opens confirmation"))) return;
        if (!Activate(TEXT("Dialog.Keep"))) return;
        Advance(43); break;
    case 43:
        if (Age < 1.0) return;
        Check(Settings->GetFullscreenMode() == EWindowMode::Fullscreen
            && GEngine->GameViewport->Viewport->GetWindowMode() == EWindowMode::Fullscreen,
            TEXT("fullscreen mode is applied to the actual native game viewport"));
        Check(Settings->GetLastConfirmedFullscreenMode() == EWindowMode::Fullscreen && !Settings->IsVideoChangePending(),
            TEXT("fullscreen selection is confirmed and retained"));
        if (!Activate(TEXT("Settings.WindowMode"))) return;
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(44); break;
    case 44:
        if (!Require(FrontEnd()->IsAwaitingVideoConfirmation(), TEXT("returning to windowed mode opens confirmation"))) return;
        if (!Activate(TEXT("Dialog.Keep"))) return;
        Advance(45); break;
    case 45:
        if (Age < 1.0) return;
        Check(Settings->GetFullscreenMode() == EWindowMode::Windowed
            && GEngine->GameViewport->Viewport->GetWindowMode() == EWindowMode::Windowed,
            TEXT("windowed mode restores the actual native game viewport"));
        if (!Activate(TEXT("Settings.Tab.Controls"))) return;
        Advance(22); break;
    case 22:
        if (!Activate(TEXT("Settings.InvertGamepadY"), EKeys::SpaceBar)) return;
        if (!Activate(TEXT("Settings.Apply"))) return;
        Advance(23); break;
    case 23:
        Check(Settings->GetInvertGamepadY() == bOriginalInvert, TEXT("a second applied UI change restores normal gamepad Y"));
        if (!Activate(TEXT("Settings.Back"))) return;
        Advance(24); break;
    case 24:
        if (!Activate(TEXT("Menu.Quit"))) return;
        Advance(25); break;
    case 25:
        Check(PageIs(TEXT("quit_confirm")), TEXT("quit first opens confirmation"));
        if (!Activate(TEXT("Dialog.Cancel"))) return;
        Advance(26); break;
    case 26:
        Check(PageIs(TEXT("title")), TEXT("quit Cancel keeps title session alive"));
        if (!Activate(TEXT("Menu.Primary"), EKeys::Gamepad_FaceButton_Bottom)) return;
        Advance(27); break;
    case 27:
        if (!Require(!Controller->IsFrontEndOpen() && !UGameplayStatics::IsGamePaused(this), TEXT("gamepad A starts gameplay and unpauses"))) return;
        Check(Controller->CanContinueGame(), TEXT("started session is available to continue"));
        Controller->SetControlRotation(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f));
        PitchBefore = Controller->GetControlRotation().Pitch;
        Advance(28); break;
    case 28:
        if (Age < .8) { if (!SendRightStick(1.f)) { Check(false, TEXT("scene viewport accepts physical right-stick input")); Finish(); } return; }
        SendRightStick(0.f);
    {
        const float Delta = FMath::FindDeltaAngleDegrees(PitchBefore, Controller->GetControlRotation().Pitch);
        Metrics.Add(FString::Printf(TEXT("\"walking_normal_pitch_delta\":%.4f"), Delta));
        Check(Delta > 1.f, TEXT("physical stick up increases walking camera pitch by actual viewport input"));
        Settings->SetInvertGamepadY(true); Settings->ApplyCustomSettings();
        Controller->SetControlRotation(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f));
        PitchBefore = Controller->GetControlRotation().Pitch;
        Advance(29); break;
    }
    case 29:
        if (Age < .8) { SendRightStick(1.f); return; }
        SendRightStick(0.f);
    {
        const float Delta = FMath::FindDeltaAngleDegrees(PitchBefore, Controller->GetControlRotation().Pitch);
        Metrics.Add(FString::Printf(TEXT("\"walking_inverted_pitch_delta\":%.4f"), Delta));
        Check(Delta < -1.f, TEXT("inversion reverses physical stick-up walking camera pitch"));
        Settings->SetInvertGamepadY(false); Settings->ApplyCustomSettings();
        float Nearest = MAX_flt;
        for (TActorIterator<ANCCar> It(GetWorld()); It; ++It)
            if (!It->IsPoliceVehicle() && It->CanEnter() && FVector::DistSquared(Hero->GetActorLocation(), It->GetActorLocation()) < Nearest)
            { Car = *It; Nearest = FVector::DistSquared(Hero->GetActorLocation(), It->GetActorLocation()); }
        if (!Require(IsValid(Car), TEXT("actual enterable vehicle exists for camera validation"))) return;
        Hero->EnterCar(Car);
        Advance(30); break;
    }
    case 30:
        if (!Require(Controller->GetPawn() == Car, TEXT("vehicle camera test uses the possessed real car"))) return;
        Controller->SetControlRotation(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f));
        PitchBefore = Controller->GetControlRotation().Pitch;
        Advance(31); break;
    case 31:
        if (Age < .8) { SendRightStick(1.f); return; }
        SendRightStick(0.f);
    {
        const float Delta = FMath::FindDeltaAngleDegrees(PitchBefore, Controller->GetControlRotation().Pitch);
        Metrics.Add(FString::Printf(TEXT("\"vehicle_normal_pitch_delta\":%.4f"), Delta));
        Check(Delta > 1.f, TEXT("physical stick up increases vehicle camera pitch through viewport mappings"));
        Settings->SetInvertGamepadY(true); Settings->ApplyCustomSettings();
        Controller->SetControlRotation(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f));
        PitchBefore = Controller->GetControlRotation().Pitch;
        Advance(32); break;
    }
    case 32:
        if (Age < .8) { SendRightStick(1.f); return; }
        SendRightStick(0.f);
    {
        const float Delta = FMath::FindDeltaAngleDegrees(PitchBefore, Controller->GetControlRotation().Pitch);
        Metrics.Add(FString::Printf(TEXT("\"vehicle_inverted_pitch_delta\":%.4f"), Delta));
        Check(Delta < -1.f, TEXT("inversion reverses physical stick-up vehicle camera pitch"));
        Settings->SetInvertGamepadY(bOriginalInvert); Settings->ApplyCustomSettings();
        Press(EKeys::Escape);
        Advance(33); break;
    }
    case 33:
        if (!Require(PageIs(TEXT("pause")) && UGameplayStatics::IsGamePaused(this), TEXT("Escape from real gameplay opens pause menu"))) return;
        if (!Activate(TEXT("Menu.Primary"))) return;
        Advance(34); break;
    case 34:
        Check(!Controller->IsFrontEndOpen() && !UGameplayStatics::IsGamePaused(this), TEXT("pause Continue resumes real gameplay"));
        Press(EKeys::Escape);
        Advance(35); break;
    case 35:
        if (!Activate(TEXT("Menu.ReturnTitle"))) return;
        Advance(36); break;
    case 36:
        Check(PageIs(TEXT("title")) && UGameplayStatics::IsGamePaused(this), TEXT("Return to main menu pauses and opens title"));
        Check(Controller->CanContinueGame() && Controller->GetPawn() == Car, TEXT("returning to title preserves the possessed session"));
        Check(IFileManager::Get().FileSize(*TitleScreenshot) > 10000 && IFileManager::Get().FileSize(*SettingsScreenshot) > 10000,
            TEXT("native title and settings screenshots were written"));
        if (!Activate(TEXT("Menu.Quit"))) return;
        Advance(37); break;
    case 37:
        if (!Require(PageIs(TEXT("quit_confirm")), TEXT("final quit uses the real confirmation dialog"))) return;
        if (!FocusTag(TEXT("Dialog.Confirm"))) return;
        Finish(); break;
    default:
        Check(false, TEXT("front-end validation phase is known")); Finish(); break;
    }
}

void ANCFrontEndValidationRunner::Finish()
{
    if (bFinished) return;
    SendRightStick(0.f);
    bFinished = true;
    PhaseStarted = FPlatformTime::Seconds();
    const FString Report = FString::Printf(
        TEXT("{\n\"status\":%s,\"checks\":%d,\"failures\":%d,\"duration_wall_seconds\":%.3f,\n")
        TEXT("\"title_screenshot\":%s,\"settings_screenshot\":%s,\"settings_file\":%s,\"user_dir\":%s,\n")
        TEXT("\"input_method\":\"real Slate tagged-widget focus and key events; physical right-stick analog through FSceneViewport\",\n")
        TEXT("\"exit_method\":\"real Slate quit confirmation; caller must additionally verify process exit zero\",\n")
        TEXT("\"metrics\":{%s},\"results\":[%s]\n}\n"),
        Failures ? TEXT("\"failed\"") : TEXT("\"passed\""), Checks, Failures,
        FPlatformTime::Seconds() - Started, *JsonString(TitleScreenshot), *JsonString(SettingsScreenshot),
        *JsonString(SettingsFilePath()), *JsonString(IsolatedUserDir), *FString::Join(Metrics, TEXT(",")), *FString::Join(Results, TEXT(",\n")));
    if (!FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        ++Failures;
        UE_LOG(LogTemp, Error, TEXT("NC_MENU_REPORT_WRITE_FAILED %s"), *ReportPath);
    }
    UE_LOG(LogTemp, Display, TEXT("NEON_COAST_MENU_%s checks=%d failures=%d report=%s"), Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures, *ReportPath);
    if (Failures) FPlatformMisc::RequestExitWithStatus(false, 1);
    else if (!Press(EKeys::Enter))
    {
        bFinished = false;
        Check(false, TEXT("final quit confirmation accepts Enter"));
        Finish();
    }
}
