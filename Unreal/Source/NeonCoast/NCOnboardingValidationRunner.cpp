#include "NCOnboardingValidationRunner.h"
#include "NCOnboardingComponent.h"
#include "NCPlayerController.h"
#include "NCGameUserSettings.h"
#include "NCHero.h"
#include "NCCar.h"
#include "SNCFrontEnd.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Kismet/GameplayStatics.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
#include "UnrealClient.h"

namespace
{
FString QuoteOnboardingJson(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return TEXT("\"") + Value + TEXT("\"");
}
}

ANCOnboardingValidationRunner::ANCOnboardingValidationRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void ANCOnboardingValidationRunner::Check(bool bPassed, const FString& Name)
{
    ++Checks;
    if (!bPassed) ++Failures;
    Results.Add(FString::Printf(TEXT("{\"name\":%s,\"passed\":%s}"), *QuoteOnboardingJson(Name), bPassed ? TEXT("true") : TEXT("false")));
    UE_LOG(LogTemp, Display, TEXT("NC_ONBOARDING_CHECK %s %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), *Name);
}

bool ANCOnboardingValidationRunner::Require(bool bPassed, const FString& Name)
{
    Check(bPassed, Name);
    if (!bPassed) Finish();
    return bPassed;
}

void ANCOnboardingValidationRunner::Advance(int32 NextPhase)
{
    Phase = NextPhase;
    PhaseStarted = FPlatformTime::Seconds();
}

TSharedPtr<SWidget> ANCOnboardingValidationRunner::FindTagged(const TSharedPtr<SWidget>& Root, FName WidgetTag) const
{
    if (!Root || !Root->GetVisibility().IsVisible()) return nullptr;
    if (Root->GetTag() == WidgetTag) return Root;
    FChildren* WidgetChildren = Root->GetChildren();
    if (WidgetChildren)
        for (int32 Index = 0; Index < WidgetChildren->Num(); ++Index)
            if (auto Match = FindTagged(WidgetChildren->GetChildAt(Index), WidgetTag)) return Match;
    return nullptr;
}

bool ANCOnboardingValidationRunner::Activate(FName WidgetTag)
{
    TSharedPtr<SWidget> Target;
    for (const auto& Window : FSlateApplication::Get().GetTopLevelWindows())
    {
        Target = FindTagged(Window, WidgetTag);
        if (Target) break;
    }
    if (!Require(Target && Target->IsEnabled(), FString::Printf(TEXT("visible enabled widget %s"), *WidgetTag.ToString()))) return false;
    FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
    if (!Require(Target->HasKeyboardFocus(), FString::Printf(TEXT("native focus on %s"), *WidgetTag.ToString()))) return false;
    return Require(Press(EKeys::Enter), FString::Printf(TEXT("real Slate Enter activates %s"), *WidgetTag.ToString()));
}

bool ANCOnboardingValidationRunner::Key(FKey InputKey, bool bDown)
{
    const FKeyEvent Event(InputKey, FModifierKeysState(), 0u, false, 0, 0);
    if (bDown) HeldKeys.AddUnique(InputKey); else HeldKeys.Remove(InputKey);
    return bDown ? FSlateApplication::Get().ProcessKeyDownEvent(Event) : FSlateApplication::Get().ProcessKeyUpEvent(Event);
}

bool ANCOnboardingValidationRunner::Press(FKey InputKey)
{
    const bool bDown = Key(InputKey, true);
    const bool bUp = Key(InputKey, false);
    return bDown || bUp;
}

bool ANCOnboardingValidationRunner::Analog(FKey InputKey, float Value)
{
    FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
    if (!Viewport) return false;
    FSceneViewport* Scene = static_cast<FSceneViewport*>(Viewport);
    const FAnalogInputEvent Event(InputKey, FModifierKeysState(), 0u, false, 0, 0, Value);
    Scene->OnAnalogValueChanged(Scene->GetCachedGeometry(), Event);
    return true;
}

bool ANCOnboardingValidationRunner::CompletedFlagOnDisk() const
{
    FConfigFile File;
    bool bCompleted = false;
    File.Read(SettingsPath);
    return File.GetBool(TEXT("/Script/NeonCoast.NCGameUserSettings"), TEXT("bHasCompletedOnboarding"), bCompleted)
        && bCompleted;
}

void ANCOnboardingValidationRunner::Capture(const TCHAR* Filename)
{
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(ReportPath), Filename), true, false);
}

void ANCOnboardingValidationRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    if (bFinished)
    {
        if (Failures == 0 && Now - PhaseStarted > 8.)
        {
            bFinished = false;
            Check(false, TEXT("confirmed quit exits within eight seconds"));
            Finish();
        }
        return;
    }
    if (Started == 0.)
    {
        Started = PhaseStarted = Now;
        bResumeRun = FParse::Param(FCommandLine::Get(), TEXT("NeonCoastOnboardingResumeTest"));
        FParse::Value(FCommandLine::Get(), TEXT("NeonCoastOnboardingReport="), ReportPath);
        FParse::Value(FCommandLine::Get(), TEXT("UserDir="), IsolatedUserDir);
        if (ReportPath.IsEmpty()) ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnboardingVerification/report.json"));
        ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
        if (GConfig)
            if (const FConfigBranch* Branch = GConfig->FindBranch(TEXT("GameUserSettings"), GGameUserSettingsIni))
                SettingsPath = FPaths::ConvertRelativePathToFull(Branch->IniPath);
        FString UserPath = FPaths::ConvertRelativePathToFull(IsolatedUserDir);
        FPaths::NormalizeDirectoryName(UserPath);
        if (!Require(!IsolatedUserDir.IsEmpty() && SettingsPath.StartsWith(UserPath + TEXT("/"), ESearchCase::IgnoreCase),
            TEXT("onboarding settings and progress use an explicit isolated UserDir"))) return;
        if (!Require(FSlateApplication::IsInitialized(), TEXT("real Slate application is running"))) return;
    }
    if (Now - Started > 180.)
    {
        Check(false, FString::Printf(TEXT("onboarding suite finishes before 180s at phase %d"), Phase));
        Finish(); return;
    }
    Controller = Cast<ANCPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
    Guide = Controller ? Controller->FindComponentByClass<UNCOnboardingComponent>() : nullptr;
    auto* Settings = UNCGameUserSettings::Get();
    if (Phase == 0 && (!Controller || !Guide || !Controller->IsFrontEndOpen() || Now - Started < 2.)) return;
    if (!Controller || !Guide || !Settings)
    {
        Check(false, TEXT("onboarding controller, component and settings remain valid"));
        Finish(); return;
    }
    const double Age = Now - PhaseStarted;
    if (Age < .3) return;
    switch (Phase)
    {
    case 0:
        Check(true, TEXT("onboarding controller, component and settings exist"));
        Check(!Guide->IsActive(), TEXT("title screen does not begin gameplay tutorial"));
        Check(Settings->GetHasCompletedOnboarding() == bResumeRun, TEXT("completion preference matches fresh or returning launch"));
        if (!Activate(TEXT("Menu.Primary"))) return;
        Advance(bResumeRun ? 200 : 1); break;
    case 1:
        if (!Require(Guide->GetStep() == ENCOnboardingStep::Welcome && Guide->IsModal(), TEXT("first gameplay entry opens welcome modal"))) return;
        Check(UGameplayStatics::IsGamePaused(this), TEXT("welcome modal pauses the live world"));
        Check(Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored(), TEXT("welcome blocks gameplay movement and view input"));
        Check(!Settings->GetHasCompletedOnboarding(), TEXT("opening welcome does not mark the tutorial complete"));
        Capture(TEXT("welcome.png"));
        Advance(2); break;
    case 2:
        if (!Activate(TEXT("Onboarding.Primary"))) return;
        Advance(3); break;
    case 3:
        if (!Require(Guide->GetStep() == ENCOnboardingStep::Look && !UGameplayStatics::IsGamePaused(this), TEXT("welcome primary starts a non-blocking look lesson"))) return;
        Controller->SetControlRotation(Controller->GetControlRotation() + FRotator(0, 60, 0));
        Advance(4); break;
    case 4:
        Check(Guide->GetStep() == ENCOnboardingStep::Look && FMath::IsNearlyZero(Guide->GetProgress()), TEXT("programmatic camera rotation without player input cannot complete look"));
        Press(EKeys::M);
        Advance(5); break;
    case 5:
        if (!Require(Controller->IsMapOpen(), TEXT("map opens while the non-blocking guide is active"))) return;
        Controller->SetControlRotation(Controller->GetControlRotation() + FRotator(0, 60, 0));
        Analog(EKeys::Gamepad_RightX, .7f);
        Advance(6); break;
    case 6:
        Check(Guide->GetStep() == ENCOnboardingStep::Look && FMath::IsNearlyZero(Guide->GetProgress()), TEXT("map interaction and camera changes do not advance the tutorial"));
        Analog(EKeys::Gamepad_RightX, 0.f);
        Press(EKeys::M);
        Advance(7); break;
    case 7:
        if (Guide->GetStep() == ENCOnboardingStep::Look)
        {
            Analog(EKeys::Gamepad_RightX, .7f);
            if (Age > 6.) { Check(false, TEXT("real viewport right stick advances look lesson")); Finish(); }
            return;
        }
        Analog(EKeys::Gamepad_RightX, 0.f);
        if (!Require(Guide->GetStep() == ENCOnboardingStep::Walk, TEXT("actual right-stick rotation completes look"))) return;
        Hero = Cast<ANCHero>(Controller->GetPawn());
        if (!Require(IsValid(Hero), TEXT("walk lesson uses the actual possessed character"))) return;
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(FVector(21600, -18000, 100), false, nullptr, ETeleportType::TeleportPhysics);
        Controller->SetControlRotation(FRotator(-12, 90, 0));
        Advance(8); break;
    case 8:
        if (Age < 1.) return;
        Check(Guide->GetStep() == ENCOnboardingStep::Walk && FMath::IsNearlyZero(Guide->GetProgress()), TEXT("fixture teleport and idle settling do not count as walking"));
        Capture(TEXT("walking.png"));
        WalkStart = Hero->GetActorLocation();
        Key(EKeys::W, true);
        Advance(9); break;
    case 9:
        WalkedCentimeters = FVector::Dist2D(WalkStart, Hero->GetActorLocation());
        if (Guide->GetStep() == ENCOnboardingStep::Walk)
        {
            if (Age > 8.) { Check(false, TEXT("real keyboard movement completes the four-meter walk")); Finish(); }
            return;
        }
        Key(EKeys::W, false);
        if (!Require(Guide->GetStep() == ENCOnboardingStep::EnterCar, TEXT("four-meter walk reaches the enter-car lesson"))) return;
        Check(WalkedCentimeters >= 390.f, TEXT("independently measured character displacement reaches approximately four meters"));
        for (TActorIterator<ANCCar> It(GetWorld()); It; ++It)
            if (!It->IsPoliceVehicle() && It->CanEnter()) { Car = *It; break; }
        if (!Require(IsValid(Car), TEXT("a real drivable vehicle is available"))) return;
        Car->SetActorLocationAndRotation(FVector(21600, -17100, 85), FRotator(0, 90, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Car->GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Car->GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        if (auto* Vehicle = Cast<UChaosWheeledVehicleMovementComponent>(Car->GetVehicleMovementComponent())) { Vehicle->ResetVehicle(); Vehicle->SetSleeping(false); }
        Car->ResetHeldInput();
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->SetActorLocation(FVector(21300, -17100, 100), false, nullptr, ETeleportType::TeleportPhysics);
        Advance(10); break;
    case 10:
        if (Age < 1.) return;
        Check(Guide->GetStep() == ENCOnboardingStep::EnterCar, TEXT("standing near a car does not substitute for entering it"));
        Press(EKeys::F);
        Advance(11); break;
    case 11:
        if (!Require(Controller->GetPawn() == Car && Guide->GetStep() == ENCOnboardingStep::Drive, TEXT("real F interaction possesses the car and advances the guide"))) return;
        DrivePrevious = Car->GetActorLocation();
        Analog(EKeys::Gamepad_RightTriggerAxis, .7f);
        Advance(12); break;
    case 12:
        DrivenCentimeters += FVector::Dist2D(DrivePrevious, Car->GetActorLocation());
        DrivePrevious = Car->GetActorLocation();
        PeakSpeed = FMath::Max(PeakSpeed, Car->GetSpeedKmh());
        if (Guide->GetProgress() < 1.f)
        {
            Analog(EKeys::Gamepad_RightTriggerAxis, .7f);
            if (Age > 25.) { Check(false, TEXT("real trigger input drives twenty meters")); Finish(); }
            return;
        }
        Check(Guide->GetStep() == ENCOnboardingStep::Drive && PeakSpeed > 5.f, TEXT("driving distance alone does not bypass the stop requirement"));
        Check(DrivenCentimeters >= 1950.f, TEXT("independently measured vehicle travel reaches approximately twenty meters"));
        Analog(EKeys::Gamepad_RightTriggerAxis, 0.f);
        Key(EKeys::Gamepad_FaceButton_Right, true);
        Advance(13); break;
    case 13:
        if (Guide->GetStep() == ENCOnboardingStep::Drive)
        {
            if (Age > 15.) { Check(false, TEXT("real brake input brings vehicle to a safe stop")); Finish(); }
            return;
        }
        Key(EKeys::Gamepad_FaceButton_Right, false);
        if (!Require(Guide->GetStep() == ENCOnboardingStep::ExitCar && Car->GetSpeedKmh() < 5.f, TEXT("stopping the actual car advances to the exit lesson"))) return;
        Press(EKeys::F);
        Advance(14); break;
    case 14:
        if (!Require(Guide->GetStep() == ENCOnboardingStep::Complete && Controller->GetPawn() == Hero, TEXT("actual F exit returns to the hero and opens completion"))) return;
        Check(Guide->IsModal() && UGameplayStatics::IsGamePaused(this), TEXT("completion pauses safely for the final directions"));
        Check(Guide->GetKeys().Contains(TEXT("E")) && Guide->GetKeys().Contains(TEXT("M")), TEXT("completion advertises courier interaction and the map"));
        Capture(TEXT("complete.png"));
        Advance(15); break;
    case 15:
        if (!Activate(TEXT("Onboarding.Primary"))) return;
        Advance(16); break;
    case 16:
        Check(!Guide->IsActive() && !UGameplayStatics::IsGamePaused(this), TEXT("completion primary returns full gameplay control"));
        Check(Settings->GetHasCompletedOnboarding() && CompletedFlagOnDisk(), TEXT("completion is persisted in the isolated settings file"));
        Guide->StartIfNeeded();
        Check(!Guide->IsActive(), TEXT("completed players do not restart guidance on another start request"));
        Press(EKeys::Escape);
        Advance(17); break;
    case 17:
        if (!Activate(TEXT("Menu.Controls"))) return;
        Advance(18); break;
    case 18:
        if (!Activate(TEXT("Controls.RestartOnboarding"))) return;
        Advance(19); break;
    case 19:
        if (!Require(Guide->GetStep() == ENCOnboardingStep::Welcome && !Controller->IsFrontEndOpen(), TEXT("pause controls replay opens the welcome modal"))) return;
        if (!Activate(TEXT("Onboarding.Skip"))) return;
        Advance(20); break;
    case 20:
        Check(!Guide->IsActive() && !UGameplayStatics::IsGamePaused(this) && CompletedFlagOnDisk(), TEXT("welcome Skip resumes gameplay and retains completion"));
        Press(EKeys::Escape);
        Advance(21); break;
    case 21:
        if (!Activate(TEXT("Menu.ReturnTitle"))) return;
        Advance(22); break;
    case 22:
        if (!Activate(TEXT("Menu.Controls"))) return;
        Advance(23); break;
    case 23:
        if (!Activate(TEXT("Controls.RestartOnboarding"))) return;
        Advance(24); break;
    case 24:
        if (!Require(Guide->GetStep() == ENCOnboardingStep::Welcome && !Controller->IsFrontEndOpen(), TEXT("title controls replay enters the playable session and opens welcome"))) return;
        if (!Activate(TEXT("Onboarding.Primary"))) return;
        Advance(25); break;
    case 25:
        if (!Require(Guide->GetStep() == ENCOnboardingStep::Look, TEXT("replayed welcome starts a fresh look lesson"))) return;
        Press(EKeys::F2);
        Advance(26); break;
    case 26:
        Check(!Guide->IsActive() && CompletedFlagOnDisk(), TEXT("F2 skips a non-modal lesson and persists completion"));
        Guide->Restart();
        Advance(27); break;
    case 27:
        if (!Activate(TEXT("Onboarding.Primary"))) return;
        Advance(28); break;
    case 28:
        Press(EKeys::Escape);
        Advance(29); break;
    case 29:
        if (!Activate(TEXT("Menu.Controls"))) return;
        Advance(30); break;
    case 30:
        if (!Activate(TEXT("Controls.SkipOnboarding"))) return;
        Advance(31); break;
    case 31:
        Check(!Guide->IsActive() && CompletedFlagOnDisk(), TEXT("pause controls Skip dismisses and persists the active lesson"));
        if (!Activate(TEXT("Controls.Back"))) return;
        Advance(90); break;
    case 200:
        if (Age < 1.) return;
        Check(!Guide->IsActive() && !Controller->IsFrontEndOpen() && !UGameplayStatics::IsGamePaused(this), TEXT("a separate returning process skips completed onboarding"));
        Check(CompletedFlagOnDisk(), TEXT("returning process reads persisted completion"));
        Press(EKeys::Escape);
        Advance(90); break;
    case 90:
        if (!Activate(TEXT("Menu.Quit"))) return;
        Advance(91); break;
    case 91:
        if (!Require(Controller->GetFrontEndWidget() && Controller->GetFrontEndWidget()->GetPageId() == TEXT("quit_confirm"), TEXT("test exits using the actual quit confirmation"))) return;
        {
            auto Target = FindTagged(Controller->GetFrontEndWidget(), TEXT("Dialog.Confirm"));
            if (!Require(Target && Target->IsEnabled(), TEXT("final quit confirmation is visible and enabled"))) return;
            FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
            if (!Require(Target->HasKeyboardFocus(), TEXT("final quit confirmation has native focus"))) return;
        }
        Finish(); break;
    default:
        Check(false, TEXT("known onboarding validation phase")); Finish(); break;
    }
}

void ANCOnboardingValidationRunner::Finish()
{
    if (bFinished) return;
    Analog(EKeys::Gamepad_RightX, 0.f);
    Analog(EKeys::Gamepad_RightTriggerAxis, 0.f);
    const TArray<FKey> KeysToRelease = HeldKeys;
    for (const FKey Held : KeysToRelease) Key(Held, false);
    bFinished = true;
    PhaseStarted = FPlatformTime::Seconds();
    const FString Report = FString::Printf(
        TEXT("{\n\"status\":%s,\"checks\":%d,\"failures\":%d,\"resume_run\":%s,\"duration_wall_seconds\":%.3f,\n")
        TEXT("\"settings_file\":%s,\"user_dir\":%s,\"walk_displacement_cm\":%.3f,\"drive_travel_cm\":%.3f,\"peak_speed_kmh\":%.3f,\n")
        TEXT("\"input_method\":\"real Slate focus/key events and scene-viewport analog input; fixture teleports only between lessons\",\n")
        TEXT("\"results\":[%s]\n}\n"),
        Failures ? TEXT("\"failed\"") : TEXT("\"passed\""), Checks, Failures, bResumeRun ? TEXT("true") : TEXT("false"),
        FPlatformTime::Seconds() - Started, *QuoteOnboardingJson(SettingsPath), *QuoteOnboardingJson(IsolatedUserDir),
        WalkedCentimeters, DrivenCentimeters, PeakSpeed, *FString::Join(Results, TEXT(",\n")));
    if (!FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        ++Failures;
        UE_LOG(LogTemp, Error, TEXT("NC_ONBOARDING_REPORT_WRITE_FAILED %s"), *ReportPath);
    }
    UE_LOG(LogTemp, Display, TEXT("NEON_COAST_ONBOARDING_%s checks=%d failures=%d report=%s"), Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures, *ReportPath);
    if (Failures) FPlatformMisc::RequestExitWithStatus(false, 1);
    else
    {
        // Finish first so a synchronous Slate quit cannot lose the report.
        if (!Press(EKeys::Enter))
        {
            bFinished = false;
            Check(false, TEXT("final quit confirmation accepts Enter"));
            Finish();
        }
    }
}
