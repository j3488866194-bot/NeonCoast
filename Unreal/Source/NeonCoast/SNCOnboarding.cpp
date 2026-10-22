#include "SNCOnboarding.h"

#include "NCOnboardingComponent.h"
#include "NCPlayerController.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "NCOnboarding"

// Named (not anonymous) so a unity build that also contains SNCFrontEnd.cpp never sees two helpers with one name.
namespace NCOnb
{
	constexpr float CardWidth = 380.f;			// HUD card, right edge below cash / wanted
	constexpr float CardText = CardWidth - 3.f - 40.f;
	constexpr float ModalWidth = 640.f;
	constexpr float ModalText = ModalWidth - 88.f;

	/** Same focus language as the front end: no dashed rectangle, hover moves focus. */
	class SNCOnboardingButton : public SButton
	{
	public:
		virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SButton::OnMouseEnter(MyGeometry, MouseEvent);
			const auto Delta = MouseEvent.GetCursorDelta();
			if (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) >= KINDA_SMALL_NUMBER && IsEnabled() && !HasAnyUserFocus().IsSet())
			{
				FSlateApplication::Get().SetUserFocus(MouseEvent.GetUserIndex(), SharedThis(this), EFocusCause::Mouse);
			}
		}
	};
}

// ---------------------------------------------------------------------------------------------------------------------
// Palette (sRGB) and type. Same values as SNCFrontEnd and ANCHUD.

FLinearColor SNCOnboarding::Hex(uint8 R, uint8 G, uint8 B, float A)
{
	FLinearColor C(FColor(R, G, B));
	C.A = A;
	return C;
}
FLinearColor SNCOnboarding::Ink(float A) { return Hex(0x10, 0x1D, 0x28, A); }
FLinearColor SNCOnboarding::DeepInk(float A) { return Hex(0x09, 0x12, 0x1A, A); }
FLinearColor SNCOnboarding::Cream(float A) { return Hex(0xFF, 0xF2, 0xD6, A); }
FLinearColor SNCOnboarding::Sand(float A) { return Hex(0xD9, 0xCD, 0xB5, A); }
FLinearColor SNCOnboarding::Mist(float A) { return Hex(0x9C, 0xAE, 0xAC, A); }
FLinearColor SNCOnboarding::Teal(float A) { return Hex(0x5F, 0xD8, 0xC0, A); }
FLinearColor SNCOnboarding::Amber(float A) { return Hex(0xFF, 0xC4, 0x57, A); }

FSlateFontInfo SNCOnboarding::Font(const ANSICHAR* Face, int32 Size, int32 LetterSpacing)
{
	FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Face, Size);
	Info.LetterSpacing = LetterSpacing;
	return Info;
}

// ---------------------------------------------------------------------------------------------------------------------
// Public contract

void SNCOnboarding::Construct(const FArguments& InArgs)
{
	Owner = InArgs._OwnerController;

	ButtonStyle = FButtonStyle()
		.SetNormal(NoBrush)
		.SetHovered(NoBrush)
		.SetPressed(NoBrush)
		.SetDisabled(NoBrush)
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f));

	ChildSlot
	[
		SAssignNew(Host, SBox)
	];

	RefreshStep();
}

const UNCOnboardingComponent* SNCOnboarding::GetComponent() const
{
	ANCPlayerController* PC = Owner.Get();
	return PC ? PC->GetOnboarding() : nullptr;
}

void SNCOnboarding::RefreshStep()
{
	const UNCOnboardingComponent* Onboarding = GetComponent();
	const bool bActive = Onboarding && Onboarding->IsActive();
	const ENCOnboardingStep Step = bActive ? Onboarding->GetStep() : ENCOnboardingStep::Inactive;
	const FString Keys = bActive ? Onboarding->GetKeys() : FString();
	if (bBuilt && Step == ShownStep && Keys == ShownKeys)
	{
		return;
	}

	bBuilt = true;
	ShownStep = Step;
	ShownKeys = Keys;
	bModal = bActive && Onboarding->IsModal();
	PrimaryButton.Reset();
	SkipButton.Reset();
	PendingFocus.Reset();
	LiveExtra = bActive && !bModal ? Onboarding->GetExtraHint() : FString();
	NextExtraRefresh = FPlatformTime::Seconds() + .2;

	if (!bActive)
	{
		Host->SetContent(SNullWidget::NullWidget);
		return;
	}
	Host->SetContent(bModal ? BuildModal(*Onboarding) : BuildCard(*Onboarding));

	// The rebuilt card replaced the focused button. Re-take focus only while nothing else owns the screen;
	// otherwise the controller calls FocusDefault once it shows the modal.
	const ANCPlayerController* PC = Owner.Get();
	if (bModal && PC && !PC->IsFrontEndOpen() && !PC->IsMapOpen())
	{
		FocusDefault();
	}
}

void SNCOnboarding::FocusDefault()
{
	if (bModal)
	{
		RequestFocus(PrimaryButton.Pin());
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Frame update and input (modal steps own input; HUD steps never touch it)

void SNCOnboarding::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const double Now = FPlatformTime::Seconds();
	if (!bModal && ShownStep != ENCOnboardingStep::Inactive && Now >= NextExtraRefresh)
	{
		NextExtraRefresh = Now + .2;
		const UNCOnboardingComponent* Onboarding = GetComponent();
		LiveExtra = Onboarding && Onboarding->IsActive() ? Onboarding->GetExtraHint() : FString();
	}

	if (!bModal || !FSlateApplication::IsInitialized())
	{
		return;
	}
	FSlateApplication& App = FSlateApplication::Get();
	if (const TSharedPtr<SWidget> Wanted = PendingFocus.Pin())
	{
		if (Wanted->HasAnyUserFocus().IsSet() || ++PendingFocusTries > 30)
		{
			PendingFocus.Reset();
		}
		else
		{
			App.SetAllUserFocus(Wanted, EFocusCause::SetDirectly);
		}
		return;
	}

	// Focus fell back to the game viewport (alt-tab, click-through): take it back while the card is up.
	const ANCPlayerController* PC = Owner.Get();
	if (PC && !PC->IsFrontEndOpen() && !PC->IsMapOpen() && App.IsActive())
	{
		static const FName ViewportType(TEXT("SViewport"));
		static const FName WindowType(TEXT("SWindow"));
		const TSharedPtr<SWidget> Focused = App.GetUserFocusedWidget(0);
		if (!Focused.IsValid() || Focused->GetType() == ViewportType || Focused->GetType() == WindowType)
		{
			FocusDefault();
		}
	}
}

FReply SNCOnboarding::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (!bModal)
	{
		return FReply::Unhandled();
	}
	// The controller focuses the root; hand focus straight to the primary action.
	if (const TSharedPtr<SWidget> Target = PrimaryButton.Pin())
	{
		PendingFocus = Target;
		PendingFocusTries = 0;
		return FReply::Handled().SetUserFocus(Target.ToSharedRef(), EFocusCause::SetDirectly);
	}
	return FReply::Handled();
}

FReply SNCOnboarding::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bModal)
	{
		return FReply::Unhandled();
	}
	// Reached only by keys the focused button did not use: Enter / Space / A press it, arrows / D-pad / Tab
	// become navigation inside the button. Everything else stops here so nothing reaches the world.
	const FKey Key = InKeyEvent.GetKey();
	FSlateApplication& App = FSlateApplication::Get();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Virtual_Back || Key == EKeys::F2
		|| App.GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Back)
	{
		if (!InKeyEvent.IsRepeat())
		{
			OnSkip();
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_Special_Right)
	{
		if (!InKeyEvent.IsRepeat())
		{
			OnPrimary();
		}
		return FReply::Handled();
	}
	// The root only holds focus after a click on empty space; any navigation or accept key re-enters the card.
	if (HasAnyUserFocus().IsSet()
		&& (App.GetNavigationDirectionFromKey(InKeyEvent) != EUINavigation::Invalid || App.GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept))
	{
		FocusDefault();
	}
	return FReply::Handled();
}

FReply SNCOnboarding::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return bModal ? FReply::Handled() : FReply::Unhandled();
}

FReply SNCOnboarding::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (!bModal)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InAnalogInputEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY)
	{
		const EUINavigation Direction = FSlateApplication::Get().GetNavigationDirectionFromAnalog(InAnalogInputEvent);
		if (Direction != EUINavigation::Invalid)
		{
			if (HasAnyUserFocus().IsSet())
			{
				FocusDefault();
				return FReply::Handled();
			}
			return FReply::Handled().SetNavigation(Direction, ENavigationGenesis::Controller);
		}
	}
	return FReply::Handled();
}

FReply SNCOnboarding::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bModal)
	{
		return FReply::Unhandled();
	}
	// Clicks on the scrim never reach the world and keep the current selection.
	FReply Reply = FReply::Handled();
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(MouseEvent.GetUserIndex());
	const TSharedPtr<SWidget> Keep = Focused.IsValid() && Focused.Get() != this && HasFocusedDescendants() ? Focused : PrimaryButton.Pin();
	if (Keep.IsValid())
	{
		Reply.SetUserFocus(Keep.ToSharedRef(), EFocusCause::SetDirectly);
	}
	return Reply;
}

FReply SNCOnboarding::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return bModal ? FReply::Handled() : FReply::Unhandled();
}

FReply SNCOnboarding::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return bModal ? FReply::Handled() : FReply::Unhandled();
}

FNavigationReply SNCOnboarding::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (!bModal)
	{
		return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
	}
	// One row of actions: every direction (and Tab / Shift+Tab) steps along it and wraps; it never escapes.
	TArray<TSharedPtr<SWidget>> Buttons;
	GatherButtons(Buttons);
	if (Buttons.Num() == 0)
	{
		return FNavigationReply::Stop();
	}
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(InNavigationEvent.GetUserIndex());
	const int32 Index = Buttons.IndexOfByKey(Focused);
	if (Index == INDEX_NONE)
	{
		return FNavigationReply::Explicit(Buttons.Last());
	}

	int32 Direction = 0;
	switch (InNavigationEvent.GetNavigationType())
	{
	case EUINavigation::Left:
	case EUINavigation::Up:
	case EUINavigation::Previous:
		Direction = -1;
		break;
	case EUINavigation::Right:
	case EUINavigation::Down:
	case EUINavigation::Next:
		Direction = 1;
		break;
	default:
		return FNavigationReply::Stop();
	}
	const int32 NewIndex = (Index + Direction + Buttons.Num()) % Buttons.Num();
	return NewIndex == Index ? FNavigationReply::Stop() : FNavigationReply::Explicit(Buttons[NewIndex]);
}

void SNCOnboarding::GatherButtons(TArray<TSharedPtr<SWidget>>& Out) const
{
	for (const TWeakPtr<SWidget>& Weak : { SkipButton, PrimaryButton })	// left to right
	{
		const TSharedPtr<SWidget> Widget = Weak.Pin();
		if (Widget.IsValid() && Widget->IsEnabled() && Widget->GetVisibility().IsVisible())
		{
			Out.Add(Widget);
		}
	}
}

void SNCOnboarding::RequestFocus(TSharedPtr<SWidget> Widget)
{
	PendingFocus = Widget;
	PendingFocusTries = 0;
	if (Widget.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Widget, EFocusCause::SetDirectly);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Actions (the only calls into gameplay; the controller then calls RefreshStep)

FReply SNCOnboarding::OnPrimary()
{
	if (ANCPlayerController* PC = Owner.Get())
	{
		PC->AdvanceOnboarding();
	}
	return FReply::Handled();
}

FReply SNCOnboarding::OnSkip()
{
	if (ANCPlayerController* PC = Owner.Get())
	{
		// On the closing card "skip" means close, which is the same as finishing.
		if (ShownStep == ENCOnboardingStep::Complete)
		{
			PC->AdvanceOnboarding();
		}
		else
		{
			PC->SkipOnboarding();
		}
	}
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout

TSharedRef<SWidget> SNCOnboarding::BuildModal(const UNCOnboardingComponent& Onboarding)
{
	const bool bWelcome = Onboarding.GetStep() == ENCOnboardingStep::Welcome;

	TSharedPtr<SWidget> Skip, Primary;
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNullWidget::NullWidget
		];
	if (bWelcome)
	{
		Buttons->AddSlot()
		.AutoWidth()
		.Padding(0.f, 0.f, 12.f, 0.f)
		[
			MakeButton(TEXT("Onboarding.Skip"), LOCTEXT("Skip", "跳过引导"), FOnClicked::CreateSP(this, &SNCOnboarding::OnSkip), false, Skip)
		];
	}
	Buttons->AddSlot()
	.AutoWidth()
	[
		MakeButton(TEXT("Onboarding.Primary"), bWelcome ? LOCTEXT("Start", "开始引导") : LOCTEXT("Finish", "开始探索"),
			FOnClicked::CreateSP(this, &SNCOnboarding::OnPrimary), true, Primary)
	];
	SkipButton = Skip;
	PrimaryButton = Primary;

	// Welcome previews the five hands-on lessons; Complete lists what to do next from the component's key text.
	TSharedRef<SWidget> Extra = SNullWidget::NullWidget;
	if (bWelcome)
	{
		const FText Lessons[] = { LOCTEXT("LessonLook", "视角"), LOCTEXT("LessonWalk", "步行"), LOCTEXT("LessonEnter", "上车"),
			LOCTEXT("LessonDrive", "驾驶"), LOCTEXT("LessonExit", "下车") };
		TSharedRef<SHorizontalBox> Chips = SNew(SHorizontalBox);
		for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Lessons)); ++i)
		{
			Chips->AddSlot()
			.AutoWidth()
			.Padding(i > 0 ? 8.f : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SBorder)
				.BorderImage(&PillBrush)
				.BorderBackgroundColor(Cream(.07f))
				.Padding(FMargin(14.f, 5.f, 16.f, 6.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::AsNumber(i + 1))
						.Font(Font("Bold", 15))
						.ColorAndOpacity(Teal())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(Lessons[i])
						.Font(Font("Regular", 18))
						.ColorAndOpacity(Cream(.92f))
					]
				]
			];
		}
		Extra = Chips;
	}
	else
	{
		Extra = MakeKeyList(Onboarding.GetKeys(), 16, 19);
	}

	TSharedRef<SHorizontalBox> Hints = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[MakeHint(INVTEXT("Enter"), INVTEXT("A"), LOCTEXT("HintConfirm", "确认"))];
	if (bWelcome)
	{
		Hints->AddSlot().AutoWidth().Padding(26.f, 0.f, 0.f, 0.f)[MakeHint(INVTEXT("← →"), FText::GetEmpty(), LOCTEXT("HintSelect", "选择"))];
	}
	Hints->AddSlot().AutoWidth().Padding(26.f, 0.f, 0.f, 0.f)
	[
		MakeHint(INVTEXT("Esc"), INVTEXT("B"), bWelcome ? LOCTEXT("HintSkip", "跳过") : LOCTEXT("HintClose", "关闭"))
	];

	const FText StepText = FText::Format(LOCTEXT("StepCount", "{0} / {1}"),
		FText::AsNumber(Onboarding.GetStepIndex()), FText::AsNumber(Onboarding.GetStepCount()));

	return SNew(SBorder)
		.BorderImage(&WhiteBrush)
		.BorderBackgroundColor(DeepInk(.55f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(0.f)
		[
			SNew(SBox)
			.WidthOverride(NCOnb::ModalWidth)
			[
				SNew(SBorder)
				.BorderImage(&PanelBrush)
				.BorderBackgroundColor(Ink(.97f))
				.Padding(FMargin(44.f, 36.f, 44.f, 30.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(28.f)
							.HeightOverride(3.f)
							[
								SNew(SBorder)
								.BorderImage(&WhiteBrush)
								.BorderBackgroundColor(bWelcome ? Teal() : Amber())
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(12.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(bWelcome ? LOCTEXT("EyebrowWelcome", "新手引导") : LOCTEXT("EyebrowComplete", "引导完成"))
							.Font(Font("Bold", 16, 300))
							.ColorAndOpacity(bWelcome ? Teal() : Amber())
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(StepText)
							.Font(Font("Bold", 16, 120))
							.ColorAndOpacity(Mist())
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 14.f, 0.f, 0.f)
					[
						MakeProgress(72.f, 4.f)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 22.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Onboarding.GetTitle()))
						.Font(Font("Bold", 34))
						.ColorAndOpacity(Cream())
						.WrapTextAt(NCOnb::ModalText)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 12.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Onboarding.GetBody()))
						.Font(Font("Regular", 20))
						.ColorAndOpacity(Sand())
						.WrapTextAt(NCOnb::ModalText)
						.LineHeightPercentage(1.1f)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 24.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(bWelcome ? LOCTEXT("LessonsLabel", "五个小练习") : LOCTEXT("NextLabel", "接下来可以"))
						.Font(Font("Bold", 15, 260))
						.ColorAndOpacity(Mist())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 10.f, 0.f, 0.f)
					[
						Extra
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 32.f, 0.f, 0.f)
					[
						Buttons
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 22.f, 0.f, 0.f)
					[
						SNew(SBox)
						.HeightOverride(1.f)
						[
							SNew(SBorder)
							.BorderImage(&WhiteBrush)
							.BorderBackgroundColor(Cream(.08f))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 14.f, 0.f, 0.f)
					[
						Hints
					]
				]
			]
		];
}

TSharedRef<SWidget> SNCOnboarding::BuildCard(const UNCOnboardingComponent& Onboarding)
{
	// Right edge, 36 in like the HUD margin and below the cash / wanted block; the minimap (bottom-left),
	// objective (top-left) and vehicle readout (bottom-right) all stay clear. Never hit-testable.
	return SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.f, 152.f, 36.f, 0.f))
		.Visibility(EVisibility::HitTestInvisible)
		[
			SNew(SBox)
			.WidthOverride(NCOnb::CardWidth)
			[
				SNew(SBorder)
				.BorderImage(&SoftBrush)
				.BorderBackgroundColor(Ink(.84f))
				.Padding(0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(3.f)
						[
							SNew(SBorder)
							.BorderImage(&WhiteBrush)
							.BorderBackgroundColor(Teal())
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(FMargin(20.f, 16.f, 20.f, 14.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("CardEyebrow", "新手引导"))
								.Font(Font("Bold", 15, 260))
								.ColorAndOpacity(Teal())
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("CardStep", "{0} / {1}"),
									FText::AsNumber(Onboarding.GetStepIndex()), FText::AsNumber(Onboarding.GetStepCount())))
								.Font(Font("Bold", 15, 120))
								.ColorAndOpacity(Mist())
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 10.f, 0.f, 0.f)
						[
							MakeProgress(44.f, 4.f)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 14.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Onboarding.GetTitle()))
							.Font(Font("Bold", 22))
							.ColorAndOpacity(Cream())
							.WrapTextAt(NCOnb::CardText)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 6.f, 0.f, 0.f)
						[
							// Live: the drive lesson rewrites its body once the distance is reached.
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								const UNCOnboardingComponent* C = GetComponent();
								return C ? FText::FromString(C->GetBody()) : FText::GetEmpty();
							})
							.Font(Font("Regular", 17))
							.ColorAndOpacity(Sand(.95f))
							.WrapTextAt(NCOnb::CardText)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 12.f, 0.f, 0.f)
						[
							MakeKeyList(Onboarding.GetKeys(), 14, 17)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 10.f, 0.f, 0.f)
						[
							// Progress or a nudge (已走 2.1 / 4 米, nearest car). The generic pause/skip line is in the footer.
							SNew(STextBlock)
							.Text_Lambda([this]() { return FText::FromString(LiveExtra); })
							.Font(Font("Regular", 17))
							.ColorAndOpacity(Teal(.95f))
							.WrapTextAt(NCOnb::CardText)
							.Visibility_Lambda([this]()
							{
								return LiveExtra.IsEmpty() || LiveExtra.Contains(TEXT("F2")) ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
							})
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 12.f, 0.f, 0.f)
						[
							SNew(SBox)
							.HeightOverride(1.f)
							[
								SNew(SBorder)
								.BorderImage(&WhiteBrush)
								.BorderBackgroundColor(Cream(.08f))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 10.f, 0.f, 0.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()[MakeHint(INVTEXT("F2"), FText::GetEmpty(), LOCTEXT("CardSkip", "跳过引导"))]
							+ SHorizontalBox::Slot().AutoWidth().Padding(22.f, 0.f, 0.f, 0.f)[MakeHint(INVTEXT("Esc"), INVTEXT("Menu"), LOCTEXT("CardPause", "暂停"))]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SNCOnboarding::MakeProgress(float SegmentWidth, float Height)
{
	const UNCOnboardingComponent* Onboarding = GetComponent();
	const int32 Count = Onboarding ? FMath::Max(1, Onboarding->GetStepCount()) : 7;
	const float Gap = SegmentWidth > 60.f ? 6.f : 4.f;

	// One segment per step: done = teal, current = dim teal track filling with the lesson's own progress.
	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
	for (int32 Index = 1; Index <= Count; ++Index)
	{
		auto Fill = [this, Index]() -> float
		{
			const UNCOnboardingComponent* C = GetComponent();
			if (!C || !C->IsActive()) return 0.f;
			const int32 Current = C->GetStepIndex();
			if (Index != Current) return Index < Current ? 1.f : 0.f;
			return C->IsModal() ? 1.f : FMath::Clamp(C->GetProgress(), 0.f, 1.f);
		};
		auto Track = [this, Index]() -> FSlateColor
		{
			const UNCOnboardingComponent* C = GetComponent();
			return FSlateColor(C && C->GetStepIndex() == Index ? Teal(.28f) : Cream(.12f));
		};

		Bar->AddSlot()
		.AutoWidth()
		.Padding(Index > 1 ? Gap : 0.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(SegmentWidth)
			.HeightOverride(Height)
			[
				SNew(SBorder)
				.BorderImage(&BarBrush)
				.BorderBackgroundColor_Lambda(Track)
				.HAlign(HAlign_Left)
				.Padding(0.f)
				[
					SNew(SBox)
					.HeightOverride(Height)
					.WidthOverride_Lambda([Fill, SegmentWidth]() { return FOptionalSize(Fill() * SegmentWidth); })
					.Visibility_Lambda([Fill]() { return Fill() > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					[
						SNew(SBorder)
						.BorderImage(&BarBrush)
						.BorderBackgroundColor(Teal())
					]
				]
			]
		];
	}
	return Bar;
}

TSharedRef<SWidget> SNCOnboarding::MakeKeyList(const FString& Keys, int32 KeySize, int32 ActionSize)
{
	// The component writes "WASD / 左摇杆：移动    Shift / L3：冲刺". Each group becomes one line of key caps
	// (keyboard cream, gamepad teal, as in the front end) followed by its action, so no group wraps mid-way.
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	TArray<FString> Groups;
	Keys.ParseIntoArray(Groups, TEXT("    "), true);
	int32 Added = 0;
	for (FString Group : Groups)
	{
		Group.TrimStartAndEndInline();
		if (Group.IsEmpty())
		{
			continue;
		}
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		const int32 Colon = Group.Find(TEXT("："));
		if (Colon == INDEX_NONE)
		{
			Row->AddSlot()
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Group))
				.Font(Font("Regular", ActionSize))
				.ColorAndOpacity(Cream(.92f))
				.AutoWrapText(true)
			];
		}
		else
		{
			TArray<FString> Caps;
			Group.Left(Colon).ParseIntoArray(Caps, TEXT("/"), true);
			for (int32 i = 0; i < Caps.Num(); ++i)
			{
				FString Cap = Caps[i].TrimStartAndEnd();
				Cap.RemoveFromStart(TEXT("手柄 "));
				Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(i > 0 ? 6.f : 0.f, 0.f, 0.f, 0.f)
				[
					MakeKeyCap(FText::FromString(Cap), i > 0, KeySize)
				];
			}
			Row->AddSlot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Group.Mid(Colon + 1).TrimStart()))
				.Font(Font("Regular", ActionSize))
				.ColorAndOpacity(Sand(.95f))
				.AutoWrapText(true)
			];
		}
		List->AddSlot()
		.AutoHeight()
		.Padding(0.f, Added > 0 ? 6.f : 0.f, 0.f, 0.f)
		[
			Row
		];
		++Added;
	}
	return List;
}

TSharedRef<SWidget> SNCOnboarding::MakeKeyCap(const FText& Key, bool bGamepad, int32 Size)
{
	return SNew(SBorder)
		.BorderImage(&OutlineBrush)
		.BorderBackgroundColor(bGamepad ? Teal(.8f) : Cream(.5f))
		.Padding(FMargin(8.f, 1.f, 8.f, 2.f))
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Key)
			.Font(Font("Bold", Size, 40))
			.ColorAndOpacity(bGamepad ? Teal() : Cream(.92f))
		];
}

TSharedRef<SWidget> SNCOnboarding::MakeHint(const FText& Keyboard, const FText& Gamepad, const FText& Action)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeKeyCap(Keyboard, false, 14)
		];
	if (!Gamepad.IsEmpty())
	{
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.f, 0.f, 0.f, 0.f)
		[
			MakeKeyCap(Gamepad, true, 14)
		];
	}
	Row->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(10.f, 0.f, 0.f, 0.f)
	[
		SNew(STextBlock)
		.Text(Action)
		.Font(Font("Regular", 17))
		.ColorAndOpacity(Sand(.9f))
	];
	return Row;
}

TSharedRef<SWidget> SNCOnboarding::MakeButton(FName WidgetTag, const FText& Label, FOnClicked OnClicked, bool bPrimary, TSharedPtr<SWidget>& OutWidget)
{
	TSharedRef<TWeakPtr<SButton>> Self = MakeShared<TWeakPtr<SButton>>();
	auto Focused = [Self]() { const TSharedPtr<SButton> B = Self->Pin(); return B.IsValid() && B->HasAnyUserFocus().IsSet(); };
	auto Pressed = [Self]() { const TSharedPtr<SButton> B = Self->Pin(); return B.IsValid() && B->IsPressed(); };

	// Front-end focus language: cream plate + ink text when focused, teal flash when pressed.
	TSharedPtr<NCOnb::SNCOnboardingButton> Button;
	SAssignNew(Button, NCOnb::SNCOnboardingButton)
		.Tag(WidgetTag)
		.ButtonStyle(&ButtonStyle)
		.ContentPadding(FMargin(0.f))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.OnClicked(OnClicked)
		[
			SNew(SBox)
			.HeightOverride(52.f)
			.MinDesiredWidth(168.f)
			[
				SNew(SBorder)
				.BorderImage(&SoftBrush)
				.BorderBackgroundColor_Lambda([Focused, Pressed, bPrimary]()
				{
					if (Pressed()) return FSlateColor(Teal(.95f));
					if (Focused()) return FSlateColor(Cream(.96f));
					return FSlateColor(bPrimary ? Teal(.16f) : Cream(.07f));
				})
				.Padding(0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						// Teal edge on the focused action, like the front end's menu items.
						SNew(SBox)
						.WidthOverride(3.f)
						[
							SNew(SBorder)
							.BorderImage(&WhiteBrush)
							.BorderBackgroundColor_Lambda([Focused]() { return FSlateColor(Focused() ? Teal() : Teal(0.f)); })
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(25.f, 0.f, 28.f, 0.f))
					[
						SNew(STextBlock)
						.Text(Label)
						.Font(Font(bPrimary ? "Bold" : "Regular", 20, bPrimary ? 60 : 0))
						.ColorAndOpacity_Lambda([Focused, Pressed, bPrimary]()
						{
							if (Focused() || Pressed()) return FSlateColor(Ink());
							return FSlateColor(bPrimary ? Teal() : Cream(.9f));
						})
					]
				]
			]
		];
	*Self = Button;
	OutWidget = Button;
	return Button.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE
