#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "NCPlayerController.generated.h"
class SNCFrontEnd;
class SNCOnboarding;
class UNCOnboardingComponent;
UCLASS()
class NEONCOAST_API ANCPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ANCPlayerController();
    void StartOrResumeGame();
    void ReturnToMainMenu();
    void QuitToDesktop();
    bool CanContinueGame() const;
    FString GetFrontEndProgressText() const;
    bool IsFrontEndOpen() const { return bFrontEndOpen; }
    TSharedPtr<SNCFrontEnd> GetFrontEndWidget() const { return FrontEnd; }
    void OpenFrontEnd(bool bTitleScreen);
    UNCOnboardingComponent* GetOnboarding() const { return Onboarding; }
    TSharedPtr<SNCOnboarding> GetOnboardingWidget() const { return OnboardingWidget; }
    void AdvanceOnboarding();
    void SkipOnboarding();
    void RestartOnboarding();
    bool PreviewAudio();
    bool IsMapOpen() const { return bMapOpen; }
    bool IsHelpOpen() const { return bHelpOpen; }
    bool IsPhotoMode() const { return bPhotoMode; }
    bool HasWaypoint() const { return bHasWaypoint; }
    FVector GetWaypoint() const { return Waypoint; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void SetupInputComponent() override;
    void TogglePause();
    void ToggleMap();
    void ToggleHelp();
    void TogglePhoto();
    void ClickMap();
    void ClearWaypoint();
    void UpdateInputMode();
    void QualityLow();
    void QualityMedium();
    void QualityHigh();
    void SetQuality(int32 Quality);
    void ToggleFullscreen();
    void TakePhoto();
    void CycleRadio();
    void SoundHorn();
    bool bMapOpen=false, bHelpOpen=false, bPhotoMode=false, bMenuPaused=false, bHasWaypoint=false;
    FVector Waypoint=FVector::ZeroVector;
private:
    UPROPERTY() TObjectPtr<UNCOnboardingComponent> Onboarding;
    TSharedPtr<SNCOnboarding> OnboardingWidget;
    bool bOnboardingCameraOverride=false;
    bool bPreviousOnboardingCameraMoveable=false;
    void RefreshOnboarding();
    bool ShouldStartOnboarding() const;
    TSharedPtr<SNCFrontEnd> FrontEnd;
    bool bFrontEndOpen=false;
    bool bSessionStarted=false;
    bool bExistingProgress=false;
    bool bPreviousCameraMoveableWhenPaused=false;
    bool bStartupSceneWarmed=false;
    bool bMenuWarmupPending=false;
    FTimerHandle StartupWarmupTimer;
    TArray<TWeakObjectPtr<AActor>> MenuHiddenMarkers;
    FString FrontEndError;
    bool SaveBeforeLeaving();
    void FinishStartupWarmup();
    void RestoreFrontEndScene();
};
