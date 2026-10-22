#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "NCOnboardingValidationRunner.generated.h"

class ANCPlayerController;
class UNCOnboardingComponent;
class ANCHero;
class ANCCar;
class SWidget;

/** Rendered, opt-in first-run/replay verification. Requires a dedicated UserDir. */
UCLASS(NotBlueprintable, Transient)
class NEONCOAST_API ANCOnboardingValidationRunner : public AActor
{
    GENERATED_BODY()
public:
    ANCOnboardingValidationRunner();
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<ANCPlayerController> Controller;
    UPROPERTY() TObjectPtr<UNCOnboardingComponent> Guide;
    UPROPERTY() TObjectPtr<ANCHero> Hero;
    UPROPERTY() TObjectPtr<ANCCar> Car;
    int32 Phase = 0, Checks = 0, Failures = 0;
    double Started = 0., PhaseStarted = 0.;
    bool bFinished = false, bResumeRun = false;
    FString ReportPath, IsolatedUserDir, SettingsPath;
    TArray<FString> Results;
    FVector WalkStart = FVector::ZeroVector;
    FVector DrivePrevious = FVector::ZeroVector;
    float WalkedCentimeters = 0.f, DrivenCentimeters = 0.f, PeakSpeed = 0.f;
    TArray<FKey> HeldKeys;

    void Check(bool bPassed, const FString& Name);
    bool Require(bool bPassed, const FString& Name);
    void Advance(int32 NextPhase);
    TSharedPtr<SWidget> FindTagged(const TSharedPtr<SWidget>& Root, FName WidgetTag) const;
    bool Activate(FName WidgetTag);
    bool Key(FKey InputKey, bool bDown);
    bool Press(FKey InputKey);
    bool Analog(FKey InputKey, float Value);
    bool CompletedFlagOnDisk() const;
    void Capture(const TCHAR* Filename);
    void Finish();
};
