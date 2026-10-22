#include "NCPlayerController.h"
#include "SNCFrontEnd.h"
#include "NCGameUserSettings.h"
#include "NCGameplaySubsystem.h"
#include "NCSaveGame.h"
#include "NCHero.h"
#include "NCCar.h"
#include "NCWorldMarkers.h"
#include "EngineUtils.h"
#include "NCHUD.h"
#include "NCAudioSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerInput.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void ANCPlayerController::BeginPlay()
{
    Super::BeginPlay();
    UpdateInputMode();
    if(PlayerCameraManager) { PlayerCameraManager->ViewPitchMin=-65; PlayerCameraManager->ViewPitchMax=25; }
    if(auto* Settings=UNCGameUserSettings::Get())
    {
        Settings->ApplyCustomSettings();
        if(Settings->WasVideoModeRecovered() || Settings->WasLegacySettingsMigrated()) Settings->ApplyResolutionSettings(true);
    }
    const bool Skip=IsRunningCommandlet() || FParse::Param(FCommandLine::Get(),TEXT("SkipFrontEnd"))
        || FParse::Param(FCommandLine::Get(),TEXT("NeonCoastSmokeTest"))
        || FParse::Param(FCommandLine::Get(),TEXT("NeonCoastPoliceTest"));
    if(!Skip && IsLocalController())
    {
        for(const TCHAR* Slot : { TEXT("NeonCoastProgress_v1"), TEXT("NeonCoastProgress_v1_backup") })
            if(UGameplayStatics::DoesSaveGameExist(Slot,0))
                if(auto* Save=Cast<UNCSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Save && Save->IsSane()) bExistingProgress=true;
        // Let GameMode finish possession before attaching UI and pausing the world.
        GetWorldTimerManager().SetTimerForNextTick(this,&ANCPlayerController::ReturnToMainMenu);
    }
    else bSessionStarted=true;
}
void ANCPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if(bFrontEndOpen) RestoreFrontEndScene();
    if(FrontEnd.IsValid() && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(FrontEnd.ToSharedRef());
    FrontEnd.Reset();
    bFrontEndOpen=false;
    Super::EndPlay(EndPlayReason);
}
void ANCPlayerController::OpenFrontEnd(bool bTitleScreen)
{
    if(!GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized()) return;
    if(!bFrontEndOpen)
    {
        bPreviousCameraMoveableWhenPaused=GetWorld()->bIsCameraMoveableWhenPaused;
        // Continue renderer history updates while simulation is paused. Otherwise
        // the camera's initial spawn velocity is frozen into every menu frame.
        GetWorld()->bIsCameraMoveableWhenPaused=true;
        if(PlayerCameraManager) PlayerCameraManager->SetGameCameraCutThisFrame();
        for(TActorIterator<ANCWorldMarkers> It(GetWorld()); It; ++It)
            if(!It->IsHidden()) { MenuHiddenMarkers.Add(*It); It->SetActorHiddenInGame(true); }
        if(!bSessionStarted && !bStartupSceneWarmed)
        {
            // First allow animation evaluation and suspension to settle under the
            // already interactive menu, rather than freezing a spawn/reference pose.
            bStartupSceneWarmed=true;
            bMenuWarmupPending=true;
            GetWorldTimerManager().SetTimer(StartupWarmupTimer,this,&ANCPlayerController::FinishStartupWarmup,.75f,false);
        }
    }
    bMapOpen=bHelpOpen=bPhotoMode=bMenuPaused=false;
    if(auto* Hero=Cast<ANCHero>(GetPawn())) Hero->ResetHeldInput();
    if(auto* Car=Cast<ANCCar>(GetPawn())) Car->ResetHeldInput();
    if(PlayerInput) PlayerInput->FlushPressedKeys();
    if(!FrontEnd.IsValid()) SAssignNew(FrontEnd,SNCFrontEnd).OwnerController(this).TitleScreen(bTitleScreen);
    else FrontEnd->OpenRoot(bTitleScreen);
    if(!bFrontEndOpen) GEngine->GameViewport->AddViewportWidgetContent(FrontEnd.ToSharedRef(),100);
    bFrontEndOpen=true;
    UpdateInputMode();
    FrontEnd->FocusDefault();
    UE_LOG(LogTemp,Display,TEXT("NC_FRONTEND_READY page=%s"),bTitleScreen?TEXT("title"):TEXT("pause"));
}
void ANCPlayerController::StartOrResumeGame()
{
    if(FrontEnd.IsValid() && FrontEnd->IsAwaitingVideoConfirmation()) return;
    if(FrontEnd.IsValid() && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(FrontEnd.ToSharedRef());
    if(bFrontEndOpen) RestoreFrontEndScene();
    bFrontEndOpen=false;
    if(PlayerCameraManager) PlayerCameraManager->SetGameCameraCutThisFrame();
    bSessionStarted=true;
    FrontEndError.Empty();
    bMapOpen=bHelpOpen=bMenuPaused=false;
    if(PlayerInput) PlayerInput->FlushPressedKeys();
    UpdateInputMode();
    if(FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    UE_LOG(LogTemp,Display,TEXT("NC_FRONTEND_GAME_RESUMED"));
}
void ANCPlayerController::FinishStartupWarmup()
{
    bMenuWarmupPending=false;
    if(bFrontEndOpen)
    {
        SetPause(true);
        if(PlayerCameraManager) PlayerCameraManager->SetGameCameraCutThisFrame();
    }
}
void ANCPlayerController::RestoreFrontEndScene()
{
    GetWorldTimerManager().ClearTimer(StartupWarmupTimer);
    bMenuWarmupPending=false;
    GetWorld()->bIsCameraMoveableWhenPaused=bPreviousCameraMoveableWhenPaused;
    for(const auto& Marker : MenuHiddenMarkers) if(Marker.IsValid()) Marker->SetActorHiddenInGame(false);
    MenuHiddenMarkers.Reset();
}
bool ANCPlayerController::SaveBeforeLeaving()
{
    if(!bSessionStarted) return true;
    auto* Progress=GetWorld()->GetSubsystem<UNCGameplaySubsystem>();
    if(!Progress || !Progress->IsPersistenceEnabled() || Progress->SaveProgress()) return true;
    FrontEndError=TEXT("保存失败，请检查磁盘空间后重试。当前游戏仍然保留。");
    UE_LOG(LogTemp,Warning,TEXT("NC_FRONTEND_SAVE_FAILED"));
    return false;
}
void ANCPlayerController::ReturnToMainMenu()
{
    if(!SaveBeforeLeaving()) { OpenFrontEnd(false); return; }
    FrontEndError.Empty();
    OpenFrontEnd(true);
}
void ANCPlayerController::QuitToDesktop()
{
    if(!SaveBeforeLeaving()) { OpenFrontEnd(false); return; }
    UKismetSystemLibrary::QuitGame(this,this,EQuitPreference::Quit,false);
}
bool ANCPlayerController::CanContinueGame() const { return bSessionStarted || bExistingProgress; }
FString ANCPlayerController::GetFrontEndProgressText() const
{
    if(!FrontEndError.IsEmpty()) return FrontEndError;
    if(auto* Progress=GetWorld()->GetSubsystem<UNCGameplaySubsystem>())
        return FString::Printf(TEXT("现金 $%d   ·   配送 %d   ·   赛事胜利 %d   ·   发现 %d/8"),
            Progress->GetCash(),Progress->GetCompletedDeliveries(),Progress->GetRaceWins(),Progress->GetCollectibleCount());
    return TEXT("沿海城市，自由探索。");
}
void ANCPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindAction("Pause",IE_Pressed,this,&ANCPlayerController::TogglePause).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::M,IE_Pressed,this,&ANCPlayerController::ToggleMap).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Gamepad_Special_Left,IE_Pressed,this,&ANCPlayerController::ToggleMap).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::F1,IE_Pressed,this,&ANCPlayerController::ToggleHelp).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::F6,IE_Pressed,this,&ANCPlayerController::TogglePhoto).bExecuteWhenPaused=true;
    auto& Click=InputComponent->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ANCPlayerController::ClickMap);
    Click.bExecuteWhenPaused=true; Click.bConsumeInput=false;
    InputComponent->BindKey(EKeys::Delete,IE_Pressed,this,&ANCPlayerController::ClearWaypoint).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::One,IE_Pressed,this,&ANCPlayerController::QualityLow).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Two,IE_Pressed,this,&ANCPlayerController::QualityMedium).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Three,IE_Pressed,this,&ANCPlayerController::QualityHigh).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::F11,IE_Pressed,this,&ANCPlayerController::ToggleFullscreen).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::F12,IE_Pressed,this,&ANCPlayerController::TakePhoto).bExecuteWhenPaused=true;
    InputComponent->BindKey(EKeys::Q,IE_Pressed,this,&ANCPlayerController::CycleRadio);
    InputComponent->BindKey(EKeys::Gamepad_DPad_Left,IE_Pressed,this,&ANCPlayerController::CycleRadio);
    InputComponent->BindKey(EKeys::H,IE_Pressed,this,&ANCPlayerController::SoundHorn);
    InputComponent->BindKey(EKeys::Gamepad_LeftThumbstick,IE_Pressed,this,&ANCPlayerController::SoundHorn).bConsumeInput=false;
}
void ANCPlayerController::UpdateInputMode()
{
    const bool Menu=bFrontEndOpen || bMenuPaused || bMapOpen || bHelpOpen;
    SetPause(Menu && !bMenuWarmupPending);
    ResetIgnoreMoveInput(); ResetIgnoreLookInput();
    SetIgnoreMoveInput(Menu); SetIgnoreLookInput(Menu);
    bShowMouseCursor=Menu;
    if(Menu)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        if(bFrontEndOpen && FrontEnd.IsValid()) Mode.SetWidgetToFocus(FrontEnd);
        SetInputMode(Mode);
    }
    else
    {
        FInputModeGameOnly Mode;
        Mode.SetConsumeCaptureMouseDown(false);
        SetInputMode(Mode);
    }
}
void ANCPlayerController::TogglePause()
{
    if(bFrontEndOpen) { if(FrontEnd.IsValid()) FrontEnd->HandleBack(); return; }
    if(bMapOpen || bHelpOpen) { bMapOpen=bHelpOpen=false; }
    else { OpenFrontEnd(false); return; }
    UpdateInputMode();
}
void ANCPlayerController::ToggleMap() { if(bFrontEndOpen) return; bMapOpen=!bMapOpen; bHelpOpen=false; UpdateInputMode(); }
void ANCPlayerController::ToggleHelp() { if(bFrontEndOpen) return; bHelpOpen=!bHelpOpen; bMapOpen=false; UpdateInputMode(); }
void ANCPlayerController::TogglePhoto() { if(!bFrontEndOpen) bPhotoMode=!bPhotoMode; }
void ANCPlayerController::ClickMap()
{
    if(!bMapOpen) return;
    float X=0,Y=0;
    if(GetMousePosition(X,Y))
        if(auto* HUD=Cast<ANCHUD>(GetHUD()))
            if(HUD->ScreenToMap(FVector2D(X,Y),Waypoint)) bHasWaypoint=true;
}
void ANCPlayerController::ClearWaypoint() { if(bMapOpen) bHasWaypoint=false; }
void ANCPlayerController::SetQuality(int32 Quality)
{
    if(bFrontEndOpen || !IsPaused() || !GEngine) return;
    if(auto* Settings=GEngine->GetGameUserSettings())
    {
        Settings->SetOverallScalabilityLevel(Quality);
        Settings->ApplySettings(false);
        GEngine->AddOnScreenDebugMessage(742,3,FColor::White,FString::Printf(TEXT("Graphics: %s"),Quality==0?TEXT("Low"):Quality==1?TEXT("Medium"):TEXT("High")));
    }
}
void ANCPlayerController::QualityLow() { SetQuality(0); }
void ANCPlayerController::QualityMedium() { SetQuality(1); }
void ANCPlayerController::QualityHigh() { SetQuality(2); }
void ANCPlayerController::ToggleFullscreen()
{
    if(bFrontEndOpen) return;
    if(GEngine) if(auto* Settings=GEngine->GetGameUserSettings())
    {
        Settings->SetFullscreenMode(Settings->GetFullscreenMode()==EWindowMode::Windowed?EWindowMode::WindowedFullscreen:EWindowMode::Windowed);
        Settings->ApplySettings(false);
        if(auto* Custom=Cast<UNCGameUserSettings>(Settings)) Custom->AcceptVideoMode();
        else Settings->ConfirmVideoMode();
        Settings->SaveSettings();
    }
}
void ANCPlayerController::TakePhoto()
{
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,TEXT("NeonCoast.png")),false,true);
}
void ANCPlayerController::CycleRadio() { if(auto* Audio=GetWorld()->GetSubsystem<UNCAudioSubsystem>()) Audio->CycleRadio(); }
void ANCPlayerController::SoundHorn() { if(auto* Audio=GetWorld()->GetSubsystem<UNCAudioSubsystem>()) Audio->SoundHorn(); }
