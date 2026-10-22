#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCStartupDisplayValidationRunner.generated.h"

/** Real startup viewport checks; invoked only with an isolated -UserDir. */
UCLASS(NotBlueprintable, Transient)
class NEONCOAST_API ANCStartupDisplayValidationRunner : public AActor
{
    GENERATED_BODY()
public:
    ANCStartupDisplayValidationRunner();
    virtual void Tick(float DeltaSeconds) override;
private:
    double Started = 0.0, CapturedAt = 0.0;
    int32 Checks = 0, Failures = 0;
    bool bCaptured = false, bFinished = false;
    FString CaseName, ReportPath, ScreenshotPath, UserDir, SettingsPath;
    TArray<FString> Results, Metrics;
    void Check(bool bPassed, const FString& Name);
    void Finish();
};
