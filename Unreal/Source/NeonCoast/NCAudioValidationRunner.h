#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCAudioValidationRunner.generated.h"

class FNCAudioCapture;
class ANCPlayerController;
class UNCAudioSubsystem;

/** Opt-in master-submix PCM verification; always launched with an isolated UserDir. */
UCLASS(NotBlueprintable, Transient)
class NEONCOAST_API ANCAudioValidationRunner : public AActor
{
    GENERATED_BODY()
public:
    ANCAudioValidationRunner();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<ANCPlayerController> Controller;
    UPROPERTY() TObjectPtr<UNCAudioSubsystem> Audio;
    TSharedPtr<FNCAudioCapture, ESPMode::ThreadSafe> Capture;
    int32 Phase = -1, Checks = 0, Failures = 0;
    double Started = 0.0, PhaseStarted = 0.0;
    bool bCapturing = false, bFinished = false;
    FString ReportPath, UserDir;
    float OriginalMaster = 1.f, OriginalMusic = .6f, OriginalEffects = 1.f;
    TArray<FString> Results, Measurements;
    void Check(bool bPassed, const FString& Name);
    void SetVolumes(float Master, float Music, float Effects);
    void BeginPhase(int32 Next);
    void EvaluatePhase();
    void Finish();
};
