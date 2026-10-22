#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "GameFramework/Actor.h"
#include "NCFrontEndValidationRunner.generated.h"

class ANCPlayerController;
class ANCHero;
class ANCCar;
class SNCFrontEnd;
class SWidget;

/** Opt-in real Slate/viewport verification. Requires an isolated -UserDir. */
UCLASS(NotBlueprintable, Transient)
class NEONCOAST_API ANCFrontEndValidationRunner : public AActor
{
    GENERATED_BODY()
public:
    ANCFrontEndValidationRunner();
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<ANCPlayerController> Controller;
    UPROPERTY() TObjectPtr<ANCHero> Hero;
    UPROPERTY() TObjectPtr<ANCCar> Car;
    int32 Phase = 0, Checks = 0, Failures = 0;
    double Started = 0.0, PhaseStarted = 0.0;
    bool bFinished = false, bOriginalInvert = false;
    FIntPoint OriginalResolution = FIntPoint::ZeroValue;
    int32 OriginalWindowMode = 0;
    float PitchBefore = 0.f;
    FVector PausedLocation = FVector::ZeroVector;
    FString ReportPath, TitleScreenshot, SettingsScreenshot, IsolatedUserDir;
    TArray<FString> Results;
    TArray<FString> Metrics;

    void Check(bool bPassed, const FString& Name);
    bool Require(bool bPassed, const FString& Name);
    void Advance(int32 Next);
    TSharedPtr<SNCFrontEnd> FrontEnd() const;
    TSharedPtr<SWidget> FindTagged(const TSharedPtr<SWidget>& Root, FName Tag) const;
    bool FocusTag(FName Tag);
    bool Activate(FName Tag, FKey Key = EKeys::Enter);
    bool Press(FKey Key);
    bool SendRightStick(float Value);
    void Capture(const FString& Path);
    void Finish();
};

