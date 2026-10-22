#include "NCOnboardingComponent.h"
#include "NCPlayerController.h"
#include "NCGameUserSettings.h"
#include "NCHero.h"
#include "NCCar.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

namespace
{
constexpr float RequiredLookDegrees = 25.f;
constexpr float RequiredWalkCentimeters = 400.f;
constexpr float RequiredDriveCentimeters = 2000.f;
constexpr float RequiredStopSeconds = .6f;
}

UNCOnboardingComponent::UNCOnboardingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    // Menus pause the world; observation reset prevents their input from counting
    // when the player returns. Modal state changes are driven by explicit actions.
    PrimaryComponentTick.bTickEvenWhenPaused = true;
}

ANCPlayerController* UNCOnboardingComponent::GetPlayer() const
{
    return Cast<ANCPlayerController>(GetOwner());
}

void UNCOnboardingComponent::StartIfNeeded()
{
    if (IsActive()) return;
    const auto* Settings = UNCGameUserSettings::Get();
    if (Settings && Settings->GetHasCompletedOnboarding()) return;
    Restart();
}

void UNCOnboardingComponent::Restart()
{
    LookDegrees = WalkCentimeters = DriveCentimeters = StoppedSeconds = 0.f;
    bDriveInputSeen = false;
    LessonCar.Reset();
    LessonDriver.Reset();
    SetStep(ENCOnboardingStep::Welcome);
}

void UNCOnboardingComponent::AdvanceModal()
{
    if (Step == ENCOnboardingStep::Welcome) SetStep(ENCOnboardingStep::Look);
    else if (Step == ENCOnboardingStep::Complete)
    {
        RememberCompleted();
        SetStep(ENCOnboardingStep::Inactive);
    }
}

void UNCOnboardingComponent::Skip()
{
    if (!IsActive()) return;
    RememberCompleted();
    SetStep(ENCOnboardingStep::Inactive);
}

void UNCOnboardingComponent::RememberCompleted()
{
    if (auto* Settings = UNCGameUserSettings::Get())
    {
        Settings->SetHasCompletedOnboarding(true);
        Settings->SaveSettings();
    }
}

void UNCOnboardingComponent::ResetObservation()
{
    bHasObservation = false;
    ObservedPawn.Reset();
    StoppedSeconds = 0.f;
}

void UNCOnboardingComponent::SetStep(ENCOnboardingStep NewStep)
{
    Step = NewStep;
    ResetObservation();
    if (Step == ENCOnboardingStep::Drive)
    {
        DriveCentimeters = 0.f;
        bDriveInputSeen = false;
        LessonCar = GetPlayer() ? Cast<ANCCar>(GetPlayer()->GetPawn()) : nullptr;
        LessonDriver = LessonCar.IsValid() ? LessonCar->GetDriverHero() : nullptr;
    }
    UE_LOG(LogTemp, Display, TEXT("NC_ONBOARDING_STEP index=%d name=%s"), GetStepIndex(), *GetTitle());
    OnStepChanged.Broadcast();
}

bool UNCOnboardingComponent::HasLookInput(const ANCPlayerController& Player) const
{
    float MouseX = 0.f, MouseY = 0.f;
    Player.GetInputMouseDelta(MouseX, MouseY);
    const auto* Settings = UNCGameUserSettings::Get();
    const float DeadZone = Settings ? Settings->GetGamepadDeadZone() : .15f;
    return FMath::Abs(MouseX) + FMath::Abs(MouseY) > .001f
        || FMath::Abs(Player.GetInputAnalogKeyState(EKeys::Gamepad_RightX)) > DeadZone
        || FMath::Abs(Player.GetInputAnalogKeyState(EKeys::Gamepad_RightY)) > DeadZone;
}

bool UNCOnboardingComponent::HasWalkInput(const ANCPlayerController& Player) const
{
    return Player.IsInputKeyDown(EKeys::W) || Player.IsInputKeyDown(EKeys::A)
        || Player.IsInputKeyDown(EKeys::S) || Player.IsInputKeyDown(EKeys::D)
        || FMath::Abs(Player.GetInputAnalogKeyState(EKeys::Gamepad_LeftX)) > .15f
        || FMath::Abs(Player.GetInputAnalogKeyState(EKeys::Gamepad_LeftY)) > .15f;
}

bool UNCOnboardingComponent::HasDriveInput(const ANCPlayerController& Player) const
{
    return Player.IsInputKeyDown(EKeys::W) || Player.IsInputKeyDown(EKeys::S)
        || Player.GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > .1f
        || Player.GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis) > .1f;
}

void UNCOnboardingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ANCPlayerController* Player = GetPlayer();
    if (!IsActive() || IsModal() || !Player) return;
    if (Player->IsFrontEndOpen() || Player->IsMapOpen() || Player->IsHelpOpen() || Player->IsPhotoMode()
        || UGameplayStatics::IsGamePaused(this) || Player->IsMoveInputIgnored() || Player->IsLookInputIgnored())
    {
        ResetObservation();
        return;
    }

    APawn* Pawn = Player->GetPawn();
    ANCHero* Hero = Cast<ANCHero>(Pawn);
    ANCCar* Car = Cast<ANCCar>(Pawn);
    if (!IsValid(Pawn) || (!Hero && !Car) || (Hero && (Hero->IsDefeated() || Hero->IsRecovering()))
        || (Car && (Car->GetCondition() <= 0.f || Car->GetDriverHealth() <= 0.f)))
    {
        // Death and high-speed recovery are not successful safe exits.
        if (Step == ENCOnboardingStep::ExitCar) SetStep(ENCOnboardingStep::EnterCar);
        ResetObservation();
        return;
    }
    const FVector Location = Pawn->GetActorLocation();
    const FRotator Rotation = Player->GetControlRotation();
    const bool bSamePawn = bHasObservation && ObservedPawn.Get() == Pawn;
    const float Distance = bSamePawn ? FVector::Dist2D(Location, PreviousLocation) : 0.f;
    const float RotationDelta = bSamePawn
        ? FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousRotation.Yaw, Rotation.Yaw))
          + FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousRotation.Pitch, Rotation.Pitch)) : 0.f;
    PreviousLocation = Location;
    PreviousRotation = Rotation;
    ObservedPawn = Pawn;
    bHasObservation = true;

    switch (Step)
    {
    case ENCOnboardingStep::Look:
        if (HasLookInput(*Player) && RotationDelta < 90.f)
            LookDegrees += RotationDelta;
        if (LookDegrees >= RequiredLookDegrees) SetStep(ENCOnboardingStep::Walk);
        break;
    case ENCOnboardingStep::Walk:
        // A physical displacement is required; idle animation, blocked movement,
        // falling, knockback and recovery teleports cannot complete this lesson.
        if (Hero && Hero->GetCharacterMovement()->IsMovingOnGround() && HasWalkInput(*Player)
            && Distance <= 2200.f * DeltaTime + 30.f)
            WalkCentimeters += Distance;
        if (WalkCentimeters >= RequiredWalkCentimeters) SetStep(ENCOnboardingStep::EnterCar);
        break;
    case ENCOnboardingStep::EnterCar:
        if (Car && IsValid(Car->GetDriverHero())) SetStep(ENCOnboardingStep::Drive);
        break;
    case ENCOnboardingStep::Drive:
        if (!Car)
        {
            SetStep(ENCOnboardingStep::EnterCar);
            break;
        }
        if (Car != LessonCar.Get())
        {
            SetStep(ENCOnboardingStep::Drive);
            break;
        }
        bDriveInputSeen |= HasDriveInput(*Player);
        if (bDriveInputSeen && Distance <= 12000.f * DeltaTime + 50.f)
            DriveCentimeters += Distance;
        if (DriveCentimeters >= RequiredDriveCentimeters && FMath::Abs(Car->GetSpeedKmh()) < 5.f)
            StoppedSeconds += DeltaTime;
        else StoppedSeconds = 0.f;
        if (StoppedSeconds >= RequiredStopSeconds) SetStep(ENCOnboardingStep::ExitCar);
        break;
    case ENCOnboardingStep::ExitCar:
        if (Hero && Hero == LessonDriver.Get() && LessonCar.IsValid() && !Hero->IsHidden()
            && !LessonCar->GetDriverHero() && FVector::Dist2D(Hero->GetActorLocation(), LessonCar->GetActorLocation()) < 600.f)
            SetStep(ENCOnboardingStep::Complete);
        else if (Car && Car != LessonCar.Get()) SetStep(ENCOnboardingStep::Drive);
        else if (!Car) SetStep(ENCOnboardingStep::EnterCar);
        break;
    default:
        break;
    }
}

float UNCOnboardingComponent::GetProgress() const
{
    switch (Step)
    {
    case ENCOnboardingStep::Look: return FMath::Clamp(LookDegrees / RequiredLookDegrees, 0.f, 1.f);
    case ENCOnboardingStep::Walk: return FMath::Clamp(WalkCentimeters / RequiredWalkCentimeters, 0.f, 1.f);
    case ENCOnboardingStep::Drive: return FMath::Clamp(DriveCentimeters / RequiredDriveCentimeters, 0.f, 1.f);
    case ENCOnboardingStep::Complete: return 1.f;
    default: return 0.f;
    }
}

FString UNCOnboardingComponent::GetTitle() const
{
    switch (Step)
    {
    case ENCOnboardingStep::Welcome: return TEXT("欢迎来到霓虹海岸");
    case ENCOnboardingStep::Look: return TEXT("先看看周围");
    case ENCOnboardingStep::Walk: return TEXT("向前走几步");
    case ENCOnboardingStep::EnterCar: return TEXT("坐上你的第一辆车");
    case ENCOnboardingStep::Drive: return TEXT("开一小段，再停稳");
    case ENCOnboardingStep::ExitCar: return TEXT("安全下车");
    case ENCOnboardingStep::Complete: return TEXT("城市交给你了");
    default: return FString();
    }
}

FString UNCOnboardingComponent::GetBody() const
{
    switch (Step)
    {
    case ENCOnboardingStep::Welcome: return TEXT("花一分钟熟悉视角、步行和驾驶。跟着屏幕角落的小提示操作，随时可以暂停或跳过。");
    case ENCOnboardingStep::Look: return TEXT("移动鼠标或右摇杆，让镜头转向你想看的方向。");
    case ENCOnboardingStep::Walk: return TEXT("使用移动按键走出 4 米。按住冲刺键可以跑快一点。");
    case ENCOnboardingStep::EnterCar: return TEXT("靠近停着的车辆，按上车键坐进驾驶位。");
    case ENCOnboardingStep::Drive: return DriveCentimeters >= RequiredDriveCentimeters
        ? TEXT("距离够了。松开油门并刹车，等车辆停稳。") : TEXT("沿道路驾驶至少 20 米，然后松开油门并刹车停稳。");
    case ENCOnboardingStep::ExitCar: return TEXT("车辆停稳后按下车键，回到街道。");
    case ENCOnboardingStep::Complete: return TEXT("前往快递标记接一单，或打开地图自由探索。暂停菜单里可以查看操作，也能重看新手引导。");
    default: return FString();
    }
}

FString UNCOnboardingComponent::GetKeys() const
{
    switch (Step)
    {
    case ENCOnboardingStep::Welcome: return TEXT("Enter / 手柄 A 开始    F2 跳过");
    case ENCOnboardingStep::Look: return TEXT("鼠标 / 右摇杆：视角");
    case ENCOnboardingStep::Walk: return TEXT("WASD / 左摇杆：移动    Shift / L3：冲刺");
    case ENCOnboardingStep::EnterCar: return TEXT("F / 手柄 Y：上车");
    case ENCOnboardingStep::Drive: return TEXT("W / RT：油门    S / LT：倒车    空格 / B：刹车");
    case ENCOnboardingStep::ExitCar: return TEXT("F / 手柄 Y：下车");
    case ENCOnboardingStep::Complete: return TEXT("E / 十字键右：接单互动    M / View：地图");
    default: return FString();
    }
}

FString UNCOnboardingComponent::GetExtraHint() const
{
    const auto* Player = GetPlayer();
    const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
    if (Step == ENCOnboardingStep::Walk && Cast<ANCCar>(Pawn))
        return TEXT("你正在车内。先按 F / 手柄 Y 下车，再练习步行。");
    if (Step == ENCOnboardingStep::Walk)
        return FString::Printf(TEXT("已走 %.1f / 4 米"), FMath::Min(WalkCentimeters / 100.f, 4.f));
    if (Step == ENCOnboardingStep::Drive)
        return DriveCentimeters >= RequiredDriveCentimeters ? TEXT("按住空格 / 手柄 B，车速降到 5 km/h 以下。")
            : FString::Printf(TEXT("已驾驶 %.0f / 20 米"), FMath::Min(DriveCentimeters / 100.f, 20.f));
    if (Step == ENCOnboardingStep::EnterCar && Pawn)
    {
        float Nearest = MAX_flt;
        for (TActorIterator<ANCCar> It(GetWorld()); It; ++It)
            if (It->CanEnter()) Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist(Pawn->GetActorLocation(), It->GetActorLocation())));
        if (Nearest < 460.f) return TEXT("这辆车就在身旁，可以上车。");
        if (Nearest < MAX_flt) return FString::Printf(TEXT("最近可驾驶车辆约 %.0f 米，走近后上车。"), Nearest / 100.f);
        return TEXT("沿街寻找停稳的车辆；按 T / 十字键下可回到安全道路。");
    }
    return TEXT("Esc / Menu 暂停    F2 跳过引导");
}
