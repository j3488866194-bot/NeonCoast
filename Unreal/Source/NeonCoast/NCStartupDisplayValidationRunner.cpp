#include "NCStartupDisplayValidationRunner.h"
#include "NCGameUserSettings.h"
#include "NCPlayerController.h"
#include "SNCFrontEnd.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
FString StartupDisplayJsonString(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return TEXT("\"") + Value + TEXT("\"");
}
FString StartupDisplaySettingsFilePath()
{
    if (GConfig)
        if (const FConfigBranch* Branch = GConfig->FindBranch(TEXT("GameUserSettings"), GGameUserSettingsIni))
            return FPaths::ConvertRelativePathToFull(Branch->IniPath);
    return FPaths::ConvertRelativePathToFull(FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"), nullptr, *FPaths::GeneratedConfigDir()));
}
}

ANCStartupDisplayValidationRunner::ANCStartupDisplayValidationRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void ANCStartupDisplayValidationRunner::Check(bool bPassed, const FString& Name)
{
    ++Checks;
    if (!bPassed) ++Failures;
    Results.Add(FString::Printf(TEXT("{\"name\":%s,\"passed\":%s}"), *StartupDisplayJsonString(Name), bPassed ? TEXT("true") : TEXT("false")));
    UE_LOG(LogTemp, Display, TEXT("NC_STARTUP_DISPLAY_CHECK %s %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), *Name);
}

void ANCStartupDisplayValidationRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished) return;
    const double Now = FPlatformTime::Seconds();
    if (Started == 0.0)
    {
        Started = Now;
        FParse::Value(FCommandLine::Get(), TEXT("NeonCoastStartupDisplayReport="), ReportPath);
        FParse::Value(FCommandLine::Get(), TEXT("NeonCoastStartupDisplayCase="), CaseName);
        FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDir);
        if (ReportPath.IsEmpty()) ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StartupDisplayVerification/report.json"));
        ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
        ScreenshotPath = FPaths::Combine(FPaths::GetPath(ReportPath), TEXT("startup.png"));
        SettingsPath = StartupDisplaySettingsFilePath();
        FString NormalizedUserDir = FPaths::ConvertRelativePathToFull(UserDir);
        FPaths::NormalizeDirectoryName(NormalizedUserDir);
        Check(!UserDir.IsEmpty() && SettingsPath.StartsWith(NormalizedUserDir + TEXT("/"), ESearchCase::IgnoreCase),
            TEXT("settings and progress are isolated from the actual user's files"));
        Check(CaseName == TEXT("fresh") || CaseName == TEXT("legacy") || CaseName == TEXT("opt_out")
            || CaseName == TEXT("recovery") || CaseName == TEXT("override"), TEXT("startup fixture case is recognized"));
        if (Failures) { Finish(); return; }
    }
    if (Now - Started > 35.0)
    {
        Check(false, TEXT("rendered startup validation finishes within 35 seconds"));
        Finish(); return;
    }
    if (bCaptured)
    {
        if (Now - CapturedAt < 1.0) return;
        if (IFileManager::Get().FileSize(*ScreenshotPath) <= 10000 && Now - CapturedAt < 8.0) return;
        Check(IFileManager::Get().FileSize(*ScreenshotPath) > 10000, TEXT("real startup viewport screenshot was written"));
        Finish(); return;
    }
    ANCPlayerController* Controller = Cast<ANCPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
    UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
    FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
    const TSharedPtr<SNCFrontEnd> FrontEnd = Controller ? Controller->GetFrontEndWidget() : nullptr;
    if (!Settings || !Viewport || !FrontEnd || Now - Started < 3.0) return;

    const bool bWindowed = CaseName == TEXT("opt_out") || CaseName == TEXT("recovery") || CaseName == TEXT("override");
    const bool bOverride = CaseName == TEXT("override");
    const EWindowMode::Type ExpectedMode = bWindowed ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen;
    const FIntPoint DesktopSize = Settings->GetDesktopResolution();
    const FIntPoint ExpectedSize = bWindowed ? FIntPoint(1280, 720) : DesktopSize;
    const FIntPoint ViewportSize = Viewport->GetSizeXY();
    const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
    Check(FSlateApplication::IsInitialized() && Window.IsValid(), TEXT("real Slate and native game window exist"));
    Check(FrontEnd->GetPageId() == TEXT("title") && Controller->IsFrontEndOpen(), TEXT("startup still opens the title menu"));
    Check(UGameplayStatics::IsGamePaused(this), TEXT("startup title pauses simulation after its warmup"));
    Check(DesktopSize.X > 0 && DesktopSize.Y > 0, TEXT("actual desktop dimensions are known"));
    Check(Viewport->GetWindowMode() == ExpectedMode, TEXT("rendered viewport uses the expected startup window mode"));
    Check(Window.IsValid() && Window->GetWindowMode() == ExpectedMode, TEXT("native window uses the expected startup window mode"));
    Check(ViewportSize == ExpectedSize, TEXT("rendered viewport uses the expected desktop or preserved window dimensions"));
    Check(Settings->GetStartInBorderless() == (CaseName != TEXT("opt_out")), TEXT("automatic-borderless preference matches its fixture"));
    Check(Settings->WasVideoModeRecovered() == (CaseName == TEXT("recovery")), TEXT("interrupted video trial is recovered only in its fixture"));
    Check(!Settings->IsVideoChangePending(), TEXT("startup leaves no unconfirmed video trial"));
    Check(Settings->WasStartupDisplayModeApplied() == (!bWindowed || bOverride), TEXT("startup preparation respects opt-out and safe recovery"));
    Check(bOverride == FParse::Param(FCommandLine::Get(), TEXT("windowed")), TEXT("only the override fixture uses a windowed command-line flag"));
    if (!bOverride)
    {
        Check(Settings->GetFullscreenMode() == ExpectedMode, TEXT("configured window mode agrees with the actual window"));
        Check(Settings->GetScreenResolution() == ExpectedSize, TEXT("configured resolution agrees with the actual viewport"));
        Check(Settings->GetLastConfirmedScreenResolution() == ExpectedSize, TEXT("safe confirmed resolution matches the startup mode"));
    }
    if (CaseName != TEXT("fresh"))
    {
        Check(FMath::IsNearlyEqual(Settings->GetMasterVolume(), .73f) && FMath::IsNearlyEqual(Settings->GetMusicVolume(), .41f)
            && FMath::IsNearlyEqual(Settings->GetEffectsVolume(), .62f), TEXT("startup preserves existing audio preferences"));
        Check(FMath::IsNearlyEqual(Settings->GetMouseSensitivity(), 1.2f) && FMath::IsNearlyEqual(Settings->GetGamepadSensitivity(), 1.35f)
            && Settings->GetInvertGamepadY(), TEXT("startup preserves existing input preferences"));
    }
    Metrics.Add(FString::Printf(TEXT("\"viewport_mode\":%d,\"native_window_mode\":%d,\"configured_mode\":%d"),
        int32(Viewport->GetWindowMode()), Window.IsValid() ? int32(Window->GetWindowMode()) : -1, int32(Settings->GetFullscreenMode())));
    Metrics.Add(FString::Printf(TEXT("\"viewport_width\":%d,\"viewport_height\":%d,\"desktop_width\":%d,\"desktop_height\":%d"),
        ViewportSize.X, ViewportSize.Y, DesktopSize.X, DesktopSize.Y));
    Metrics.Add(FString::Printf(TEXT("\"automatic_borderless\":%s,\"video_recovered\":%s"),
        Settings->GetStartInBorderless() ? TEXT("true") : TEXT("false"), Settings->WasVideoModeRecovered() ? TEXT("true") : TEXT("false")));
    FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
    bCaptured = true;
    CapturedAt = Now;
}

void ANCStartupDisplayValidationRunner::Finish()
{
    if (bFinished) return;
    bFinished = true;
    const FString Report = FString::Printf(
        TEXT("{\"status\":%s,\"case\":%s,\"checks\":%d,\"failures\":%d,\"duration_wall_seconds\":%.3f,")
        TEXT("\"screenshot\":%s,\"settings_file\":%s,\"user_dir\":%s,\"metrics\":{%s},\"results\":[%s]}\n"),
        Failures ? TEXT("\"failed\"") : TEXT("\"passed\""), *StartupDisplayJsonString(CaseName), Checks, Failures,
        FPlatformTime::Seconds() - Started, *StartupDisplayJsonString(ScreenshotPath), *StartupDisplayJsonString(SettingsPath), *StartupDisplayJsonString(UserDir),
        *FString::Join(Metrics, TEXT(",")), *FString::Join(Results, TEXT(",\n")));
    if (!FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        ++Failures;
        UE_LOG(LogTemp, Error, TEXT("NC_STARTUP_DISPLAY_REPORT_WRITE_FAILED %s"), *ReportPath);
    }
    UE_LOG(LogTemp, Display, TEXT("NEON_COAST_STARTUP_DISPLAY_%s checks=%d failures=%d case=%s report=%s"),
        Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures, *CaseName, *ReportPath);
    FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
