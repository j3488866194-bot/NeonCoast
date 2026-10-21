#include "NCPlayerController.h"
#include "NCHUD.h"
#include "NCAudioSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void ANCPlayerController::BeginPlay()
{
    Super::BeginPlay();
    UpdateInputMode();
    if(PlayerCameraManager) { PlayerCameraManager->ViewPitchMin=-65; PlayerCameraManager->ViewPitchMax=25; }
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
    const bool Menu=bMenuPaused || bMapOpen || bHelpOpen;
    SetPause(Menu);
    bShowMouseCursor=Menu;
    if(Menu)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
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
    if(bMapOpen || bHelpOpen) { bMapOpen=bHelpOpen=false; }
    else bMenuPaused=!bMenuPaused;
    UpdateInputMode();
}
void ANCPlayerController::ToggleMap() { bMapOpen=!bMapOpen; bHelpOpen=false; UpdateInputMode(); }
void ANCPlayerController::ToggleHelp() { bHelpOpen=!bHelpOpen; bMapOpen=false; UpdateInputMode(); }
void ANCPlayerController::TogglePhoto() { bPhotoMode=!bPhotoMode; }
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
    if(!IsPaused() || !GEngine) return;
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
    if(GEngine) if(auto* Settings=GEngine->GetGameUserSettings())
    {
        Settings->SetFullscreenMode(Settings->GetFullscreenMode()==EWindowMode::Windowed?EWindowMode::WindowedFullscreen:EWindowMode::Windowed);
        Settings->ApplySettings(false);
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
