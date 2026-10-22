#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/NavigationReply.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "NCOnboardingComponent.h"

class ANCPlayerController;
class SBox;
class SWidget;

/**
 * First-play tutorial overlay. Welcome and Complete are centred modal cards with two actions;
 * the hands-on steps (Look .. ExitCar) are a small non-interactive card on the right edge.
 * The controller owns lifetime, visibility, input mode and pause, and calls RefreshStep when
 * the component changes. The widget only calls AdvanceOnboarding / SkipOnboarding.
 */
class NEONCOAST_API SNCOnboarding : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNCOnboarding)
		: _OwnerController(nullptr)
		{}
		SLATE_ARGUMENT(ANCPlayerController*, OwnerController)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the layout when the component's step (or its key list) changed; live text updates itself. */
	void RefreshStep();
	/** Modal steps: puts keyboard/gamepad focus on the primary action. HUD steps: no-op. */
	void FocusDefault();
	bool IsShowingModal() const { return bModal; }

	// SWidget
	virtual bool SupportsKeyboardFocus() const override { return bModal; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;

private:
	// Palette and type shared with SNCFrontEnd / ANCHUD. Members rather than file statics, so a unity build
	// that also compiles SNCFrontEnd.cpp never sees two unqualified Ink() or Font() helpers.
	static FLinearColor Hex(uint8 R, uint8 G, uint8 B, float A);
	static FLinearColor Ink(float A = 1.f);
	static FLinearColor DeepInk(float A = 1.f);
	static FLinearColor Cream(float A = 1.f);
	static FLinearColor Sand(float A = 1.f);
	static FLinearColor Mist(float A = 1.f);
	static FLinearColor Teal(float A = 1.f);
	static FLinearColor Amber(float A = 1.f);
	static FSlateFontInfo Font(const ANSICHAR* Face, int32 Size, int32 LetterSpacing = 0);

	const UNCOnboardingComponent* GetComponent() const;
	TSharedRef<SWidget> BuildModal(const UNCOnboardingComponent& Onboarding);
	TSharedRef<SWidget> BuildCard(const UNCOnboardingComponent& Onboarding);
	TSharedRef<SWidget> MakeProgress(float SegmentWidth, float Height);
	TSharedRef<SWidget> MakeKeyList(const FString& Keys, int32 KeySize, int32 ActionSize);
	TSharedRef<SWidget> MakeKeyCap(const FText& Key, bool bGamepad, int32 Size);
	TSharedRef<SWidget> MakeHint(const FText& Keyboard, const FText& Gamepad, const FText& Action);
	TSharedRef<SWidget> MakeButton(FName WidgetTag, const FText& Label, FOnClicked OnClicked, bool bPrimary, TSharedPtr<SWidget>& OutWidget);

	FReply OnPrimary();
	FReply OnSkip();
	void RequestFocus(TSharedPtr<SWidget> Widget);
	void GatherButtons(TArray<TSharedPtr<SWidget>>& Out) const;

	TWeakObjectPtr<ANCPlayerController> Owner;
	TSharedPtr<SBox> Host;

	bool bBuilt = false;
	bool bModal = false;
	ENCOnboardingStep ShownStep = ENCOnboardingStep::Inactive;
	FString ShownKeys;
	FString LiveExtra;					// GetExtraHint scans the world, so it is sampled a few times a second
	double NextExtraRefresh = 0.0;

	TWeakPtr<SWidget> PrimaryButton;
	TWeakPtr<SWidget> SkipButton;
	TWeakPtr<SWidget> PendingFocus;		// re-applied on tick until the new widgets accept it
	int32 PendingFocusTries = 0;

	// Styles and brushes are owned here so widgets never point at freed style data.
	FButtonStyle ButtonStyle;
	FSlateNoResource NoBrush;
	FSlateColorBrush WhiteBrush = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush PanelBrush = FSlateRoundedBoxBrush(FLinearColor::White, 6.f);
	FSlateRoundedBoxBrush SoftBrush = FSlateRoundedBoxBrush(FLinearColor::White, 3.f);
	FSlateRoundedBoxBrush OutlineBrush = FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.f, FLinearColor::White, 1.5f);
	FSlateRoundedBoxBrush PillBrush = FSlateRoundedBoxBrush(FLinearColor::White, 12.f);
	FSlateRoundedBoxBrush BarBrush = FSlateRoundedBoxBrush(FLinearColor::White, 2.f);
};
