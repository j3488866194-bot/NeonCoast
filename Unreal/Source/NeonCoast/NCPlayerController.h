#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "NCPlayerController.generated.h"
UCLASS()
class NEONCOAST_API ANCPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    bool IsMapOpen() const { return bMapOpen; }
    bool IsHelpOpen() const { return bHelpOpen; }
    bool IsPhotoMode() const { return bPhotoMode; }
    bool HasWaypoint() const { return bHasWaypoint; }
    FVector GetWaypoint() const { return Waypoint; }
protected:
    virtual void BeginPlay() override;
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
};
