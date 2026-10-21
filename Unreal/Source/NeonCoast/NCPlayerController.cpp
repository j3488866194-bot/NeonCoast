#include "NCPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Components/InputComponent.h"
void ANCPlayerController::BeginPlay()
{
    Super::BeginPlay();
    SetInputMode(FInputModeGameOnly());
    bShowMouseCursor=false;
    if(PlayerCameraManager) { PlayerCameraManager->ViewPitchMin=-65; PlayerCameraManager->ViewPitchMax=25; }
}
void ANCPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    auto& Binding=InputComponent->BindAction("Pause",IE_Pressed,this,&ANCPlayerController::TogglePause);
    Binding.bExecuteWhenPaused=true;
}
void ANCPlayerController::TogglePause()
{
    const bool Paused=!IsPaused();
    SetPause(Paused); bShowMouseCursor=Paused;
    if(Paused) SetInputMode(FInputModeGameAndUI()); else SetInputMode(FInputModeGameOnly());
    if(GEngine) GEngine->AddOnScreenDebugMessage(741,Paused?3600.f:0.f,FColor(230,230,215),TEXT("PAUSED  |  Esc / Start to resume"));
}
