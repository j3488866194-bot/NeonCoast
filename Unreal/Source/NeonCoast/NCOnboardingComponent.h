#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NCOnboardingComponent.generated.h"

class ANCPlayerController;
class APawn;
class ANCCar;
class ANCHero;

UENUM()
enum class ENCOnboardingStep : uint8
{
    Inactive, Welcome, Look, Walk, EnterCar, Drive, ExitCar, Complete
};

DECLARE_MULTICAST_DELEGATE(FNCOnboardingChanged);

/** First-session guidance, observed from real gameplay without touching progress saves. */
UCLASS(ClassGroup=(NeonCoast), NotBlueprintable)
class NEONCOAST_API UNCOnboardingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UNCOnboardingComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void StartIfNeeded();
    void Restart();
    void AdvanceModal();
    void Skip();
    bool IsActive() const { return Step != ENCOnboardingStep::Inactive; }
    bool IsModal() const { return Step == ENCOnboardingStep::Welcome || Step == ENCOnboardingStep::Complete; }
    ENCOnboardingStep GetStep() const { return Step; }
    int32 GetStepIndex() const { return static_cast<int32>(Step); }
    int32 GetStepCount() const { return 7; }
    float GetProgress() const;
    FString GetTitle() const;
    FString GetBody() const;
    FString GetKeys() const;
    FString GetExtraHint() const;

    FNCOnboardingChanged OnStepChanged;

private:
    ENCOnboardingStep Step = ENCOnboardingStep::Inactive;
    TWeakObjectPtr<APawn> ObservedPawn;
    TWeakObjectPtr<ANCCar> LessonCar;
    TWeakObjectPtr<ANCHero> LessonDriver;
    FVector PreviousLocation = FVector::ZeroVector;
    FRotator PreviousRotation = FRotator::ZeroRotator;
    float LookDegrees = 0.f;
    float WalkCentimeters = 0.f;
    float DriveCentimeters = 0.f;
    float StoppedSeconds = 0.f;
    bool bHasObservation = false;
    bool bDriveInputSeen = false;

    ANCPlayerController* GetPlayer() const;
    void SetStep(ENCOnboardingStep NewStep);
    void ResetObservation();
    void RememberCompleted();
    bool HasLookInput(const ANCPlayerController& Player) const;
    bool HasWalkInput(const ANCPlayerController& Player) const;
    bool HasDriveInput(const ANCPlayerController& Player) const;
};
