#include "SNCFrontEnd.h"

#include "NCGameUserSettings.h"
#include "NCOnboardingComponent.h"
#include "NCPlayerController.h"

#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "CoreGlobals.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "NCFrontEnd"

namespace
{
	// Palette (sRGB). Matches ANCHUD: warm cream on ink navy, teal for focus, amber for attention.
	FLinearColor Hex(uint8 R, uint8 G, uint8 B, float A)
	{
		FLinearColor C(FColor(R, G, B));
		C.A = A;
		return C;
	}
	FLinearColor Ink(float A = 1.f) { return Hex(0x10, 0x1D, 0x28, A); }
	FLinearColor DeepInk(float A = 1.f) { return Hex(0x09, 0x12, 0x1A, A); }
	FLinearColor Cream(float A = 1.f) { return Hex(0xFF, 0xF2, 0xD6, A); }
	FLinearColor Sand(float A = 1.f) { return Hex(0xD9, 0xCD, 0xB5, A); }
	FLinearColor Mist(float A = 1.f) { return Hex(0x9C, 0xAE, 0xAC, A); }
	FLinearColor Teal(float A = 1.f) { return Hex(0x5F, 0xD8, 0xC0, A); }
	FLinearColor Amber(float A = 1.f) { return Hex(0xFF, 0xC4, 0x57, A); }
	FLinearColor Coral(float A = 1.f) { return Hex(0xFF, 0x8A, 0x6E, A); }

	FSlateFontInfo Font(const ANSICHAR* Face, int32 Size, int32 LetterSpacing = 0)
	{
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Face, Size);
		Info.LetterSpacing = LetterSpacing;
		return Info;
	}

	constexpr double VideoConfirmSeconds = 15.0;
	constexpr int32 NumWindowModes = 3;
	constexpr int32 WindowModeOrder[NumWindowModes] = { EWindowMode::Windowed, EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen };
	constexpr int32 NumFrameRates = 4;
	constexpr float FrameRates[NumFrameRates] = { 30.f, 60.f, 120.f, 0.f };

	bool Same(float A, float B) { return FMath::IsNearlyEqual(A, B, 1.e-3f); }

	bool HasFocus(const TWeakPtr<SWidget>& Widget)
	{
		const TSharedPtr<SWidget> Pinned = Widget.Pin();
		return Pinned.IsValid() && Pinned->HasAnyUserFocus().IsSet();
	}

	FText WindowModeText(int32 Mode)
	{
		switch (Mode)
		{
		case EWindowMode::Fullscreen: return LOCTEXT("ModeFullscreen", "全屏");
		case EWindowMode::WindowedFullscreen: return LOCTEXT("ModeBorderless", "无边框窗口");
		default: return LOCTEXT("ModeWindowed", "窗口化");
		}
	}

	FText ResolutionText(const FIntPoint& R)
	{
		return FText::FromString(FString::Printf(TEXT("%d × %d"), R.X, R.Y));
	}

	/** Hover moves focus, so mouse and pad never show two highlighted items. Synthetic moves (layout changes) are ignored. */
	void FocusFromPointer(const TSharedRef<SWidget>& Widget, const FPointerEvent& Event)
	{
		const auto Delta = Event.GetCursorDelta();
		if (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) < KINDA_SMALL_NUMBER || !Widget->IsEnabled() || Widget->HasAnyUserFocus().IsSet())
		{
			return;
		}
		FSlateApplication::Get().SetUserFocus(Event.GetUserIndex(), Widget, EFocusCause::Mouse);
	}

	// Engine widgets with the dashed focus rectangle removed; focus is drawn by our own bound colours instead.
	class SNCButton : public SButton
	{
	public:
		TFunction<bool(EUINavigation)> OnNavigate;	// returns true when the direction was consumed
		TFunction<void()> OnNavigationFocus;

		virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SButton::OnMouseEnter(MyGeometry, MouseEvent);
			FocusFromPointer(SharedThis(this), MouseEvent);
		}

		virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override
		{
			if (OnNavigate && OnNavigate(InNavigationEvent.GetNavigationType()))
			{
				return FNavigationReply::Stop();
			}
			return SButton::OnNavigation(MyGeometry, InNavigationEvent);
		}

		virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override
		{
			FReply Reply = SButton::OnFocusReceived(MyGeometry, InFocusEvent);
			if (OnNavigationFocus && InFocusEvent.GetCause() == EFocusCause::Navigation)
			{
				OnNavigationFocus();
			}
			return Reply;
		}
	};

	class SNCCheckBox : public SCheckBox
	{
	public:
		virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SCheckBox::OnMouseEnter(MyGeometry, MouseEvent);
			FocusFromPointer(SharedThis(this), MouseEvent);
		}

		// Left/right flips the switch, like the choice rows step their value.
		virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override
		{
			const EUINavigation Nav = InNavigationEvent.GetNavigationType();
			if (Nav == EUINavigation::Left || Nav == EUINavigation::Right)
			{
				ToggleCheckedState();
				return FNavigationReply::Stop();
			}
			return SCheckBox::OnNavigation(MyGeometry, InNavigationEvent);
		}
	};

	class SNCSlider : public SSlider
	{
	public:
		virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SSlider::OnMouseEnter(MyGeometry, MouseEvent);
			FocusFromPointer(SharedThis(this), MouseEvent);
		}
	};

	/** Full-width settings row: hovering or clicking anywhere on it focuses its control. */
	class SNCRow : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SNCRow) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedRef<TWeakPtr<SWidget>> InTarget, TFunction<void()> InOnClick)
		{
			Target = InTarget;
			OnClick = MoveTemp(InOnClick);
			ChildSlot[InArgs._Content.Widget];
		}

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
			if (const TSharedPtr<SWidget> Widget = Target->Pin())
			{
				FocusFromPointer(Widget.ToSharedRef(), MouseEvent);
			}
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			const TSharedPtr<SWidget> Widget = Target->Pin();
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Widget.IsValid() || !Widget->IsEnabled())
			{
				return FReply::Handled();
			}
			if (OnClick)
			{
				OnClick();
			}
			return FReply::Handled().SetUserFocus(Widget.ToSharedRef(), EFocusCause::Mouse);
		}

	private:
		TSharedPtr<TWeakPtr<SWidget>> Target;
		TFunction<void()> OnClick;
	};
}

// ---------------------------------------------------------------------------------------------------------------------
// Draft

FNCSettingsDraft FNCSettingsDraft::Capture(const UNCGameUserSettings& Settings)
{
	FNCSettingsDraft D;
	D.WindowMode = static_cast<int32>(Settings.GetFullscreenMode());
	D.Resolution = Settings.GetScreenResolution();
	const int32 Level = Settings.GetOverallScalabilityLevel();
	D.Quality = Level >= 0 && Level <= 3 ? Level : -1;
	D.FrameRateLimit = Settings.GetFrameRateLimit();
	D.bVSync = Settings.IsVSyncEnabled();
	D.bStartInBorderless = Settings.GetStartInBorderless();
	D.MouseSensitivity = Settings.GetMouseSensitivity();
	D.GamepadSensitivity = Settings.GetGamepadSensitivity();
	D.bInvertMouseY = Settings.GetInvertMouseY();
	D.bInvertGamepadY = Settings.GetInvertGamepadY();
	D.GamepadDeadZone = Settings.GetGamepadDeadZone();
	D.MasterVolume = Settings.GetMasterVolume();
	D.MusicVolume = Settings.GetMusicVolume();
	D.EffectsVolume = Settings.GetEffectsVolume();
	return D;
}

void FNCSettingsDraft::ResetVideo(const FIntPoint& DesktopResolution)
{
	WindowMode = EWindowMode::WindowedFullscreen;
	Resolution = DesktopResolution.X > 0 && DesktopResolution.Y > 0 ? DesktopResolution : FIntPoint(1920, 1080);
	Quality = 2;
	FrameRateLimit = 60.f;
	bVSync = true;
	bStartInBorderless = true;
}

void FNCSettingsDraft::ResetControls()
{
	MouseSensitivity = GamepadSensitivity = 1.f;
	bInvertMouseY = bInvertGamepadY = false;
	GamepadDeadZone = .15f;
}

void FNCSettingsDraft::ResetAudio()
{
	MasterVolume = EffectsVolume = 1.f;
	MusicVolume = .6f;
}

bool FNCSettingsDraft::DisplayModeEquals(const FNCSettingsDraft& O) const
{
	// Borderless always covers the desktop, so its stored resolution is not a visible difference.
	return WindowMode == O.WindowMode && (WindowMode == EWindowMode::WindowedFullscreen || Resolution == O.Resolution);
}

bool FNCSettingsDraft::VideoEquals(const FNCSettingsDraft& O) const
{
	return DisplayModeEquals(O) && Quality == O.Quality && Same(FrameRateLimit, O.FrameRateLimit) && bVSync == O.bVSync
		&& bStartInBorderless == O.bStartInBorderless;
}

bool FNCSettingsDraft::ControlsEquals(const FNCSettingsDraft& O) const
{
	return Same(MouseSensitivity, O.MouseSensitivity) && Same(GamepadSensitivity, O.GamepadSensitivity)
		&& bInvertMouseY == O.bInvertMouseY && bInvertGamepadY == O.bInvertGamepadY && Same(GamepadDeadZone, O.GamepadDeadZone);
}

bool FNCSettingsDraft::AudioEquals(const FNCSettingsDraft& O) const
{
	return Same(MasterVolume, O.MasterVolume) && Same(MusicVolume, O.MusicVolume) && Same(EffectsVolume, O.EffectsVolume);
}

// ---------------------------------------------------------------------------------------------------------------------
// Construction and public contract

void SNCFrontEnd::Construct(const FArguments& InArgs)
{
	Owner = InArgs._OwnerController;
	bTitle = InArgs._TitleScreen;

	ButtonStyle = FButtonStyle()
		.SetNormal(NoBrush)
		.SetHovered(NoBrush)
		.SetPressed(NoBrush)
		.SetDisabled(NoBrush)
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f));

	ToggleStyle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(NoBrush)
		.SetUncheckedHoveredImage(NoBrush)
		.SetUncheckedPressedImage(NoBrush)
		.SetCheckedImage(NoBrush)
		.SetCheckedHoveredImage(NoBrush)
		.SetCheckedPressedImage(NoBrush)
		.SetUndeterminedImage(NoBrush)
		.SetUndeterminedHoveredImage(NoBrush)
		.SetUndeterminedPressedImage(NoBrush)
		.SetBackgroundImage(NoBrush)
		.SetBackgroundHoveredImage(NoBrush)
		.SetBackgroundPressedImage(NoBrush)
		.SetPadding(FMargin(0.f));

	SliderStyle = FSliderStyle()
		.SetNormalBarImage(BarBrush)
		.SetHoveredBarImage(BarBrush)
		.SetDisabledBarImage(BarBrush)
		.SetNormalThumbImage(ThumbBrush)
		.SetHoveredThumbImage(ThumbBrush)
		.SetDisabledThumbImage(ThumbBrush)
		.SetBarThickness(4.f);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(PageHost, SBox)
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		[
			SAssignNew(FooterHost, SBox)
			.Visibility(EVisibility::HitTestInvisible)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(DialogHost, SBox)
			.Visibility(EVisibility::Collapsed)
		]
	];

	OpenRoot(bTitle);
}

SNCFrontEnd::~SNCFrontEnd()
{
	// Never leave an unconfirmed display mode behind if the menu is torn down mid-countdown.
	if (Dialog == EDialog::VideoConfirm && !IsEngineExitRequested())
	{
		RevertDisplay();
	}
}

void SNCFrontEnd::OpenRoot(bool bTitleScreen)
{
	if (Dialog == EDialog::VideoConfirm)
	{
		RevertDisplay();
	}
	if (Dialog != EDialog::None)
	{
		CloseDialog();
	}
	bTitle = bTitleScreen;
	ShowPage(bTitle ? EPage::Title : EPage::Pause, RootReturnTag);
	RootReturnTag = NAME_None;
}

void SNCFrontEnd::OpenSettingsPage()
{
	OpenSettingsAt(ETab::Video);
}

void SNCFrontEnd::OpenSettingsAt(ETab InitialTab)
{
	if (const UNCGameUserSettings* Settings = UNCGameUserSettings::Get())
	{
		Original = FNCSettingsDraft::Capture(*Settings);
	}
	Draft = Original;
	RefreshResolutionOptions();
	Tab = InitialTab;
	PendingTab.Reset();
	StatusUntil = 0.0;
	ShowPage(EPage::Settings);
}

bool SNCFrontEnd::HandleBack()
{
	switch (Dialog)
	{
	case EDialog::VideoConfirm: OnRevertVideo(); return true;
	case EDialog::QuitConfirm: OnDialogCancel(); return true;
	default: break;
	}

	switch (Page)
	{
	case EPage::Title:
		ShowDialog(EDialog::QuitConfirm);
		return true;
	case EPage::Pause:
		if (!Owner.IsValid())
		{
			return false;
		}
		OnPrimary();
		return true;
	case EPage::Settings:
		OnSettingsBack();
		return true;
	case EPage::Controls:
		LeaveSubPage();
		return true;
	}
	return false;
}

void SNCFrontEnd::FocusDefault()
{
	RequestFocus((Dialog != EDialog::None ? DialogDefault : PageDefault).Pin());
}

FString SNCFrontEnd::GetPageId() const
{
	if (Dialog == EDialog::QuitConfirm) return TEXT("quit_confirm");
	if (Dialog == EDialog::VideoConfirm) return TEXT("video_confirm");
	switch (Page)
	{
	case EPage::Pause: return TEXT("pause");
	case EPage::Settings: return TEXT("settings");
	case EPage::Controls: return TEXT("controls");
	default: return TEXT("title");
	}
}

bool SNCFrontEnd::IsAwaitingVideoConfirmation() const
{
	return Dialog == EDialog::VideoConfirm;
}

// ---------------------------------------------------------------------------------------------------------------------
// Frame update, painting, input

void SNCFrontEnd::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Wall clock, so the countdown runs while the world is paused.
	if (Dialog == EDialog::VideoConfirm && FPlatformTime::Seconds() >= VideoDeadline)
	{
		RevertDisplay();
		CloseDialog();
		SetStatus(LOCTEXT("VideoTimedOut", "未确认，已恢复原显示设置。"), Amber(), 6.0);
	}

	if (PendingTab.IsSet())
	{
		const ETab Wanted = PendingTab.GetValue();
		PendingTab.Reset();
		if (Page == EPage::Settings && Wanted != Tab)
		{
			Tab = Wanted;
			RebuildTab();
		}
	}

	if (!FSlateApplication::IsInitialized())
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
	}

	const TSharedPtr<SWidget> Focused = App.GetUserFocusedWidget(0);
	if (Focused.IsValid() && Focused.Get() != this && HasFocusedDescendants())
	{
		LastFocused = Focused;
	}
	else if (!PendingFocus.IsValid())
	{
		// Focus fell back to the game viewport (alt-tab, click-through): take it back so nothing reaches the world.
		static const FName ViewportType(TEXT("SViewport"));
		static const FName WindowType(TEXT("SWindow"));
		const bool bLost = !Focused.IsValid() || Focused->GetType() == ViewportType || Focused->GetType() == WindowType;
		const ANCPlayerController* PC = Owner.Get();
		if (bLost && App.IsActive() && (!PC || PC->IsFrontEndOpen()))
		{
			RestoreFocus();
		}
	}
}

int32 SNCFrontEnd::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Cinematic shading: the street stays readable on the right, text sits on deep ink on the left.
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const bool bDeep = Page == EPage::Settings || Page == EPage::Controls;

	TArray<FSlateGradientStop> Side;
	Side.Add(FSlateGradientStop(FVector2f(0.f, 0.f), DeepInk(bDeep ? .94f : .88f)));
	Side.Add(FSlateGradientStop(FVector2f(Size.X * .30f, 0.f), DeepInk(bDeep ? .86f : .70f)));
	Side.Add(FSlateGradientStop(FVector2f(Size.X * .60f, 0.f), DeepInk(bDeep ? .58f : .20f)));
	Side.Add(FSlateGradientStop(FVector2f(Size.X, 0.f), DeepInk(bDeep ? .42f : .04f)));
	FSlateDrawElement::MakeGradient(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Side, Orient_Vertical);

	TArray<FSlateGradientStop> Top;
	Top.Add(FSlateGradientStop(FVector2f(0.f, 0.f), DeepInk(.50f)));
	Top.Add(FSlateGradientStop(FVector2f(0.f, Size.Y * .16f), DeepInk(0.f)));
	FSlateDrawElement::MakeGradient(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Top, Orient_Horizontal);

	TArray<FSlateGradientStop> Floor;
	Floor.Add(FSlateGradientStop(FVector2f(0.f, Size.Y * .58f), DeepInk(0.f)));
	Floor.Add(FSlateGradientStop(FVector2f(0.f, Size.Y), DeepInk(.74f)));
	FSlateDrawElement::MakeGradient(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Floor, Orient_Horizontal);

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}

FReply SNCFrontEnd::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	// The controller focuses the root; hand focus straight to the current action.
	TSharedPtr<SWidget> Target = LastFocused.Pin();
	if (!Target.IsValid() || !Target->IsEnabled())
	{
		Target = (Dialog != EDialog::None ? DialogDefault : PageDefault).Pin();
	}
	if (Target.IsValid() && Target.Get() != this)
	{
		PendingFocus = Target;
		PendingFocusTries = 0;
		return FReply::Handled().SetUserFocus(Target.ToSharedRef(), EFocusCause::SetDirectly);
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Reached only by keys the focused control did not use. Arrows/D-pad are turned into navigation by the
	// focused control itself (SWidget::OnKeyDown), so they are never handled twice. Everything else stops here.
	const FKey Key = InKeyEvent.GetKey();
	FSlateApplication& App = FSlateApplication::Get();
	const bool bRepeat = InKeyEvent.IsRepeat();

	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Virtual_Back
		|| App.GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Back)
	{
		if (!bRepeat)
		{
			HandleBack();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Gamepad_Special_Right)
	{
		if (!bRepeat)
		{
			if (Dialog == EDialog::None && (Page == EPage::Title || Page == EPage::Pause))
			{
				OnPrimary();
			}
			else if (Dialog != EDialog::VideoConfirm)
			{
				HandleBack();
			}
		}
		return FReply::Handled();
	}

	if (Dialog == EDialog::None && Page == EPage::Settings)
	{
		if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::PageUp)
		{
			CycleTab(-1);
			return FReply::Handled();
		}
		if (Key == EKeys::Gamepad_RightShoulder || Key == EKeys::PageDown)
		{
			CycleTab(1);
			return FReply::Handled();
		}
	}

	// The root itself only holds focus after a click on empty space; any navigation key re-enters the menu.
	if (HasAnyUserFocus().IsSet()
		&& (App.GetNavigationDirectionFromKey(InKeyEvent) != EUINavigation::Invalid || App.GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept))
	{
		RestoreFocus();
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Handled();
}

FReply SNCFrontEnd::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY)
	{
		// Slate's own thresholds and hold-to-repeat timing.
		const EUINavigation Direction = FSlateApplication::Get().GetNavigationDirectionFromAnalog(InAnalogInputEvent);
		if (Direction != EUINavigation::Invalid)
		{
			if (HasAnyUserFocus().IsSet())
			{
				RestoreFocus();
				return FReply::Handled();
			}
			return FReply::Handled().SetNavigation(Direction, ENavigationGenesis::Controller);
		}
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Clicks on empty space never reach the world and do not drop the current selection.
	FReply Reply = FReply::Handled();
	if (const TSharedPtr<SWidget> Keep = LastFocused.Pin())
	{
		Reply.SetUserFocus(Keep.ToSharedRef(), EFocusCause::SetDirectly);
	}
	return Reply;
}

FReply SNCFrontEnd::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled();
}

FReply SNCFrontEnd::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled();
}

FReply SNCFrontEnd::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled();
}

FNavigationReply SNCFrontEnd::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// Explicit row/column model: predictable on every layout, and never escapes into the game viewport.
	EUINavigation Nav = InNavigationEvent.GetNavigationType();
	if (Dialog == EDialog::None && Page == EPage::Settings && (Nav == EUINavigation::Next || Nav == EUINavigation::Previous))
	{
		CycleTab(Nav == EUINavigation::Next ? 1 : -1);	// Tab / Shift+Tab
		return FNavigationReply::Stop();
	}
	if (Nav == EUINavigation::Next) Nav = EUINavigation::Down;
	if (Nav == EUINavigation::Previous) Nav = EUINavigation::Up;

	TArray<FNavRow> Rows;
	GatherNavRows(Rows);
	if (Rows.Num() == 0)
	{
		return FNavigationReply::Stop();
	}

	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(InNavigationEvent.GetUserIndex());
	int32 Row = INDEX_NONE;
	int32 Col = INDEX_NONE;
	for (int32 R = 0; R < Rows.Num() && Row == INDEX_NONE; ++R)
	{
		for (int32 C = 0; C < Rows[R].Items.Num(); ++C)
		{
			if (Rows[R].Items[C].Pin() == Focused)
			{
				Row = R;
				Col = C;
				break;
			}
		}
	}
	if (Row == INDEX_NONE)
	{
		const TSharedPtr<SWidget> Fallback = (Dialog != EDialog::None ? DialogDefault : PageDefault).Pin();
		return Fallback.IsValid() ? FNavigationReply::Explicit(Fallback) : FNavigationReply::Stop();
	}

	int32 NewRow = Row;
	int32 NewCol = Col;
	switch (Nav)
	{
	case EUINavigation::Up:
	case EUINavigation::Down:
	{
		NewRow = Row + (Nav == EUINavigation::Up ? -1 : 1);
		if (!Rows.IsValidIndex(NewRow))
		{
			if (!bWrapNav)
			{
				return FNavigationReply::Stop();
			}
			NewRow = (NewRow + Rows.Num()) % Rows.Num();
		}
		const FNavRow& Target = Rows[NewRow];
		if (Target.bTabRow) NewCol = static_cast<int32>(Tab);
		else if (Target.Items.Num() == Rows[Row].Items.Num()) NewCol = Col;
		else NewCol = Target.Entry;
		NewCol = FMath::Clamp(NewCol, 0, Target.Items.Num() - 1);
		break;
	}
	case EUINavigation::Left:
	case EUINavigation::Right:
		NewCol = Col + (Nav == EUINavigation::Left ? -1 : 1);
		if (!Rows[Row].Items.IsValidIndex(NewCol))
		{
			return FNavigationReply::Stop();
		}
		break;
	default:
		return FNavigationReply::Escape();
	}

	if (NewRow == Row && NewCol == Col)
	{
		return FNavigationReply::Stop();
	}
	return FNavigationReply::Explicit(Rows[NewRow].Items[NewCol].Pin());
}

// ---------------------------------------------------------------------------------------------------------------------
// Page and dialog switching

void SNCFrontEnd::ShowPage(EPage NewPage, FName FocusTag)
{
	Page = NewPage;
	HeadNav.Reset();
	BodyNav.Reset();
	TailNav.Reset();
	RowHints.Reset();
	TabButtons.Reset();
	PageDefault.Reset();
	TabFirstFocus.Reset();
	LastFocused.Reset();
	bWrapNav = Page == EPage::Title || Page == EPage::Pause;

	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	switch (Page)
	{
	case EPage::Settings: Content = BuildSettingsPage(); break;
	case EPage::Controls: Content = BuildControlsPage(); break;
	default: Content = BuildRootPage(); break;
	}
	PageHost->SetContent(Content);
	PageHost->SetVisibility(Dialog == EDialog::None ? EVisibility::Visible : EVisibility::HitTestInvisible);
	RefreshFooter();

	TSharedPtr<SWidget> Focus = PageDefault.Pin();
	if (!FocusTag.IsNone())
	{
		if (const TWeakPtr<SWidget>* Found = Tagged.Find(FocusTag))
		{
			if (Found->IsValid())
			{
				Focus = Found->Pin();
			}
		}
	}
	if (Dialog == EDialog::None)
	{
		RequestFocus(Focus);
	}
}

void SNCFrontEnd::ShowDialog(EDialog NewDialog)
{
	if (Dialog == EDialog::None)
	{
		DialogReturnFocus = FSlateApplication::Get().GetUserFocusedWidget(0);
	}
	Dialog = NewDialog;
	DialogNav.Reset();
	DialogDefault.Reset();

	DialogHost->SetContent(NewDialog == EDialog::VideoConfirm ? BuildVideoDialog() : BuildQuitDialog());
	DialogHost->SetVisibility(EVisibility::Visible);
	PageHost->SetVisibility(EVisibility::HitTestInvisible);	// visible behind the scrim, but not clickable or navigable
	RefreshFooter();
	RequestFocus(DialogDefault.Pin());
}

void SNCFrontEnd::CloseDialog()
{
	Dialog = EDialog::None;
	DialogNav.Reset();
	DialogDefault.Reset();
	DialogHost->SetContent(SNullWidget::NullWidget);
	DialogHost->SetVisibility(EVisibility::Collapsed);
	PageHost->SetVisibility(EVisibility::Visible);
	RefreshFooter();

	const TSharedPtr<SWidget> Back = DialogReturnFocus.Pin();
	DialogReturnFocus.Reset();
	RequestFocus(Back.IsValid() && Back.Get() != this ? Back : PageDefault.Pin());
}

void SNCFrontEnd::LeaveSubPage()
{
	const FName ReturnTag = RootReturnTag;
	RootReturnTag = NAME_None;
	ShowPage(bTitle ? EPage::Title : EPage::Pause, ReturnTag);
}

void SNCFrontEnd::RequestFocus(TSharedPtr<SWidget> Widget)
{
	PendingFocus = Widget;
	PendingFocusTries = 0;
	if (Widget.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Widget, EFocusCause::SetDirectly);
	}
}

void SNCFrontEnd::RestoreFocus()
{
	const TSharedPtr<SWidget> Last = LastFocused.Pin();
	if (Last.IsValid() && Last->IsEnabled() && Last.Get() != this)
	{
		RequestFocus(Last);
	}
	else
	{
		FocusDefault();
	}
}

void SNCFrontEnd::GatherNavRows(TArray<FNavRow>& Out) const
{
	auto AddRows = [&Out](const TArray<FNavRow>& Source)
	{
		for (const FNavRow& Row : Source)
		{
			FNavRow Live = Row;
			Live.Items.RemoveAll([](const TWeakPtr<SWidget>& Item)
			{
				const TSharedPtr<SWidget> Widget = Item.Pin();
				return !Widget.IsValid() || !Widget->IsEnabled() || !Widget->GetVisibility().IsVisible();
			});
			if (Live.Items.Num() > 0)
			{
				Out.Add(MoveTemp(Live));
			}
		}
	};
	if (Dialog != EDialog::None)
	{
		AddRows(DialogNav);
		return;
	}
	AddRows(HeadNav);
	AddRows(BodyNav);
	AddRows(TailNav);
}

void SNCFrontEnd::RefreshFooter()
{
	if (!FooterHost.IsValid())
	{
		return;
	}
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	auto Add = [this, &Row](const FText& Keyboard, const FText& Pad, const FText& Action)
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(30.f, 0.f, 0.f, 0.f)[MakeHint(Keyboard, Pad, Action)];
	};

	const FText Select = LOCTEXT("HintSelect", "选择");
	const FText Confirm = LOCTEXT("HintConfirm", "确认");
	if (Dialog != EDialog::None)
	{
		Add(INVTEXT("← →"), FText::GetEmpty(), Select);
		Add(INVTEXT("Enter"), INVTEXT("A"), Confirm);
		Add(INVTEXT("Esc"), INVTEXT("B"), Dialog == EDialog::VideoConfirm ? LOCTEXT("HintRevert", "恢复") : LOCTEXT("HintCancel", "取消"));
	}
	else
	{
		switch (Page)
		{
		case EPage::Title:
			Add(INVTEXT("↑ ↓"), FText::GetEmpty(), Select);
			Add(INVTEXT("Enter"), INVTEXT("A"), Confirm);
			Add(INVTEXT("Esc"), INVTEXT("B"), LOCTEXT("HintQuit", "退出"));
			break;
		case EPage::Pause:
			Add(INVTEXT("↑ ↓"), FText::GetEmpty(), Select);
			Add(INVTEXT("Enter"), INVTEXT("A"), Confirm);
			Add(INVTEXT("Esc"), INVTEXT("B"), LOCTEXT("HintResume", "继续游戏"));
			break;
		case EPage::Settings:
			Add(INVTEXT("Tab"), INVTEXT("LB  RB"), LOCTEXT("HintTabs", "切换分页"));
			Add(INVTEXT("← →"), FText::GetEmpty(), LOCTEXT("HintAdjust", "调整"));
			Add(INVTEXT("Enter"), INVTEXT("A"), Confirm);
			Add(INVTEXT("Esc"), INVTEXT("B"), LOCTEXT("HintBack", "返回"));
			break;
		case EPage::Controls:
			Add(INVTEXT("Esc"), INVTEXT("B"), LOCTEXT("HintBack2", "返回"));
			break;
		}
	}

	FooterHost->SetContent(
		SNew(SBox)
		.HAlign(HAlign_Right)
		.Padding(FMargin(120.f, 0.f, 112.f, 34.f))
		[
			Row
		]);
}

// ---------------------------------------------------------------------------------------------------------------------
// Root (title / pause)

TSharedRef<SWidget> SNCFrontEnd::BuildRootPage()
{
	const ANCPlayerController* PC = Owner.Get();
	const bool bCanContinue = PC && PC->CanContinueGame();
	const FString Progress = PC ? PC->GetFrontEndProgressText() : FString();

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	auto Add = [this, &Menu](FName WidgetTag, const FText& Label, FOnClicked OnClicked)
	{
		TSharedPtr<SWidget> Button;
		Menu->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[MakeButton(WidgetTag, Label, OnClicked, EButtonKind::Menu, &Button)];
		FNavRow Row;
		Row.Items.Add(Button);
		HeadNav.Add(Row);
		if (!PageDefault.IsValid())
		{
			PageDefault = Button;
		}
	};

	const FText PrimaryLabel = bTitle && !bCanContinue ? LOCTEXT("StartGame", "开始游戏") : LOCTEXT("ContinueGame", "继续游戏");
	Add(TEXT("Menu.Primary"), PrimaryLabel, FOnClicked::CreateSP(this, &SNCFrontEnd::OnPrimary));
	Add(TEXT("Menu.Settings"), LOCTEXT("MenuSettings", "设置"), FOnClicked::CreateSP(this, &SNCFrontEnd::OnOpenSettings));
	Add(TEXT("Menu.Controls"), LOCTEXT("MenuControls", "操作指南"), FOnClicked::CreateSP(this, &SNCFrontEnd::OnOpenControls));
	if (!bTitle)
	{
		Add(TEXT("Menu.ReturnTitle"), LOCTEXT("MenuReturnTitle", "返回主菜单"), FOnClicked::CreateSP(this, &SNCFrontEnd::OnReturnToTitle));
	}
	Add(TEXT("Menu.Quit"), LOCTEXT("MenuQuit", "退出游戏"), FOnClicked::CreateSP(this, &SNCFrontEnd::OnQuit));

	const FText Eyebrow = bTitle ? LOCTEXT("BrandCn", "霓虹海岸") : LOCTEXT("Paused", "已暂停");
	const FText Tagline = bTitle
		? LOCTEXT("Tagline", "海风、霓虹，和一条漫长的海岸公路。")
		: LOCTEXT("PauseTagline", "城市在等你回来。");

	// Title stacks the wordmark so the full name fits the 540 column at display size; pause keeps one quieter line.
	const FSlateFontInfo WordmarkFont = Font("Bold", bTitle ? 80 : 46, 180);
	TSharedRef<SVerticalBox> Wordmark = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(bTitle ? INVTEXT("NEON") : INVTEXT("NEON COAST"))
			.Font(WordmarkFont)
			.ColorAndOpacity(Cream())
		];
	if (bTitle)
	{
		// Caps have no descenders: tuck the second line into the first line's descent for a locked-up mark.
		Wordmark->AddSlot()
		.AutoHeight()
		.Padding(0.f, -22.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(INVTEXT("COAST"))
			.Font(WordmarkFont)
			.ColorAndOpacity(Cream())
		];
	}

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Eyebrow)
			.Font(Font("Bold", bTitle ? 22 : 20, bTitle ? 620 : 420))
			.ColorAndOpacity(bTitle ? Teal() : Amber())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, bTitle ? 6.f : 4.f, 0.f, 0.f)
		[
			Wordmark
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 16.f, 0.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(72.f)
			.HeightOverride(3.f)
			[
				SNew(SBorder)
				.BorderImage(&WhiteBrush)
				.BorderBackgroundColor(Amber())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 18.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(Tagline)
			.Font(Font("Regular", 21))
			.ColorAndOpacity(Sand(.92f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, bTitle ? 60.f : 44.f, 0.f, 0.f)
		[
			Menu
		];

	if (!Progress.IsEmpty())
	{
		// The controller sends "现金 $250 · 配送 0 · …". Show it as a stat strip (label over value) so no item
		// ever wraps mid-way; anything that does not parse that way falls back to the plain wrapped string.
		TArray<FString> Parts;
		Progress.ParseIntoArray(Parts, TEXT("·"));
		bool bStrip = Parts.Num() >= 2 && Parts.Num() <= 4;
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			int32 Space = INDEX_NONE;
			bStrip &= Part.FindLastChar(TEXT(' '), Space) && Space > 0;
		}

		TSharedRef<SWidget> Summary = SNew(STextBlock)
			.Text(FText::FromString(Progress))
			.Font(Font("Regular", 19))
			.ColorAndOpacity(Cream(.9f))
			.AutoWrapText(true);
		if (bStrip)
		{
			TSharedRef<SHorizontalBox> Stats = SNew(SHorizontalBox);
			for (const FString& Part : Parts)
			{
				int32 Space = INDEX_NONE;
				Part.FindLastChar(TEXT(' '), Space);
				Stats->AddSlot()
				.AutoWidth()
				.Padding(0.f, 0.f, 36.f, 0.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Part.Left(Space).TrimEnd()))
						.Font(Font("Regular", 16))
						.ColorAndOpacity(Mist())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 2.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Part.Mid(Space + 1)))
						.Font(Font("Bold", 22))
						.ColorAndOpacity(Cream(.92f))
					]
				];
			}
			Summary = Stats;
		}

		Column->AddSlot()
		.AutoHeight()
		.Padding(0.f, 34.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(&SoftBrush)
			.BorderBackgroundColor(Cream(.05f))
			.Padding(FMargin(20.f, 14.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ProgressLabel", "当前进度"))
					.Font(Font("Bold", 15, 260))
					.ColorAndOpacity(Mist())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 10.f, 0.f, 0.f)
				[
					Summary
				]
			]
		];
	}

	return SNew(SBox)
		.Padding(FMargin(132.f, 0.f, 0.f, 40.f))
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(540.f)
			[
				Column
			]
		];
}

// ---------------------------------------------------------------------------------------------------------------------
// Settings

TSharedRef<SWidget> SNCFrontEnd::BuildSettingsPage()
{
	// Tabs.
	FNavRow TabRow;
	TabRow.bTabRow = true;
	TSharedRef<SHorizontalBox> TabStrip = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 14.f, 0.f)
		[
			MakeKeyCap(INVTEXT("LB"), true)
		];

	const struct { ETab Id; FName Tag; FText Label; FText Hint; } Tabs[] =
	{
		{ ETab::Video, TEXT("Settings.Tab.Video"), LOCTEXT("TabVideo", "画面"), LOCTEXT("TabVideoHint", "显示模式、分辨率、画质与帧率。") },
		{ ETab::Controls, TEXT("Settings.Tab.Controls"), LOCTEXT("TabControls", "操控"), LOCTEXT("TabControlsHint", "鼠标与手柄的灵敏度、垂直反转和右摇杆死区。") },
		{ ETab::Audio, TEXT("Settings.Tab.Audio"), LOCTEXT("TabAudio", "声音"), LOCTEXT("TabAudioHint", "主音量、音乐与音效。") },
	};
	for (const auto& Entry : Tabs)
	{
		const ETab Id = Entry.Id;
		TSharedPtr<SWidget> Button;
		TabStrip->AddSlot()
		.AutoWidth()
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			MakeButton(Entry.Tag, Entry.Label, FOnClicked::CreateSP(this, &SNCFrontEnd::OnSelectTab, Id), EButtonKind::Tab, &Button,
				TAttribute<bool>::CreateLambda([this, Id]() { return Tab == Id; }),
				[this, Id]() { PendingTab = Id; })
		];
		TabRow.Items.Add(Button);
		TabButtons.Add(Button);
		RowHints.Add({ Button, Entry.Hint, false });
	}
	TabStrip->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(8.f, 0.f, 0.f, 0.f)
	[
		MakeKeyCap(INVTEXT("RB"), true)
	];
	HeadNav.Add(TabRow);

	// Footer actions.
	TSharedPtr<SWidget> Defaults, Back, Apply;
	TSharedRef<SWidget> DefaultsButton = MakeButton(TEXT("Settings.Defaults"), LOCTEXT("Defaults", "恢复默认"), FOnClicked::CreateSP(this, &SNCFrontEnd::OnDefaults), EButtonKind::Secondary, &Defaults);
	TSharedRef<SWidget> BackButton = MakeButton(TEXT("Settings.Back"), LOCTEXT("Back", "返回"), FOnClicked::CreateSP(this, &SNCFrontEnd::OnSettingsBack), EButtonKind::Secondary, &Back);
	TSharedRef<SWidget> ApplyButton = MakeButton(TEXT("Settings.Apply"), LOCTEXT("Apply", "应用"), FOnClicked::CreateSP(this, &SNCFrontEnd::OnApply), EButtonKind::Primary, &Apply);
	FNavRow Footer;
	Footer.Items = { Defaults, Back, Apply };
	Footer.Entry = 2;
	TailNav.Add(Footer);
	RowHints.Add({ Defaults, LOCTEXT("DefaultsHint", "把当前分页恢复为默认值。按“应用”后才会保存。"), false });
	RowHints.Add({ Back, LOCTEXT("BackHint", "返回上一级。未应用的更改将被放弃。"), false });
	RowHints.Add({ Apply, LOCTEXT("ApplyHint", "应用并保存三个分页中的所有更改。"), false });

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			TabStrip
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(SBox)
			.HeightOverride(1.f)
			[
				SNew(SBorder)
				.BorderImage(&WhiteBrush)
				.BorderBackgroundColor(Cream(.10f))
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 18.f, 0.f, 0.f)
		[
			SAssignNew(TabHost, SBox)
			.VAlign(VAlign_Top)
			[
				BuildTabBody()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(&SoftBrush)
			.BorderBackgroundColor(Cream(.04f))
			.Padding(FMargin(22.f, 14.f))
			[
				SNew(SBox)
				.MinDesiredHeight(54.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetFocusedHint(); })
					.Font(Font("Regular", 19))
					.ColorAndOpacity(Sand(.95f))
					.AutoWrapText(true)
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 18.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 20.f, 0.f)
			[
				// About 396 units remain beside the three 168-wide buttons. Messages are written to fit one line;
				// the fixed wrap width keeps any longer one whole (no one-frame AutoWrap lag) instead of running under the buttons.
				SNew(STextBlock)
				.Text_Lambda([this]() { return GetStatusText(); })
				.ColorAndOpacity_Lambda([this]() { return GetStatusColor(); })
				.Font(Font("Regular", 18))
				.AutoWrapText(true)
				.WrapTextAt(370.f)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				DefaultsButton
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(12.f, 0.f, 0.f, 0.f)
			[
				BackButton
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(12.f, 0.f, 0.f, 0.f)
			[
				ApplyButton
			]
		];

	PageDefault = TabFirstFocus;
	return BuildPanel(bTitle ? LOCTEXT("SettingsEyebrowTitle", "主菜单") : LOCTEXT("SettingsEyebrowPause", "暂停菜单"),
		LOCTEXT("SettingsTitle", "设置"), Body, 1040.f);
}

void SNCFrontEnd::RebuildTab()
{
	if (TabHost.IsValid())
	{
		TabHost->SetContent(BuildTabBody());
	}
	PageDefault = TabFirstFocus;
}

TSharedRef<SWidget> SNCFrontEnd::BuildTabBody()
{
	BodyNav.Reset();
	RowHints.RemoveAll([](const FRowHint& Hint) { return Hint.bBody; });
	TabFirstFocus.Reset();
	switch (Tab)
	{
	case ETab::Controls: return BuildControlsTab();
	case ETab::Audio: return BuildAudioTab();
	default: return BuildVideoTab();
	}
}

TSharedRef<SWidget> SNCFrontEnd::BuildVideoTab()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Add = [&Box](TSharedRef<SWidget> Row) { Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Row]; };

	Add(MakeChoiceRow(TEXT("Settings.WindowMode"), LOCTEXT("WindowMode", "显示模式"),
		LOCTEXT("WindowModeHint", "无边框窗口切换最快；全屏独占显示器。更改后需在 15 秒内确认，否则自动恢复。"),
		[this]() { return WindowModeText(Draft.WindowMode); },
		[this](int32 Dir)
		{
			int32 Index = 0;
			for (int32 i = 0; i < NumWindowModes; ++i)
			{
				if (WindowModeOrder[i] == Draft.WindowMode) Index = i;
			}
			Draft.WindowMode = WindowModeOrder[(Index + Dir + NumWindowModes) % NumWindowModes];
		},
		[this]() { return Draft.WindowMode != Original.WindowMode; }));

	Add(MakeChoiceRow(TEXT("Settings.Resolution"), LOCTEXT("Resolution", "分辨率"),
		LOCTEXT("ResolutionHint", "窗口化与全屏下的画面分辨率；无边框窗口始终跟随桌面。更改后需在 15 秒内确认。"),
		[this]()
		{
			if (Draft.WindowMode == EWindowMode::WindowedFullscreen)
			{
				return FText::Format(LOCTEXT("ResolutionDesktop", "跟随桌面 · {0}"), ResolutionText(EffectiveResolution(Draft)));
			}
			return ResolutionText(Draft.Resolution);
		},
		[this](int32 Dir)
		{
			if (ResolutionOptions.Num() == 0) return;
			const int32 Current = ResolutionOptions.IndexOfByKey(Draft.Resolution);
			const int32 Index = Current == INDEX_NONE ? 0 : (Current + Dir + ResolutionOptions.Num()) % ResolutionOptions.Num();
			Draft.Resolution = ResolutionOptions[Index];
		},
		[this]() { return Draft.WindowMode != EWindowMode::WindowedFullscreen && Draft.Resolution != Original.Resolution; },
		TAttribute<bool>::CreateLambda([this]() { return Draft.WindowMode != EWindowMode::WindowedFullscreen; })));

	Add(MakeChoiceRow(TEXT("Settings.Quality"), LOCTEXT("Quality", "画面质量"),
		LOCTEXT("QualityHint", "整体调整阴影、反射、植被、视距与后期效果。笔记本电脑建议“高”。"),
		[this]()
		{
			switch (Draft.Quality)
			{
			case 0: return LOCTEXT("QualityLow", "低");
			case 1: return LOCTEXT("QualityMedium", "中");
			case 2: return LOCTEXT("QualityHigh", "高");
			case 3: return LOCTEXT("QualityEpic", "极高");
			default: return LOCTEXT("QualityCustom", "自定义");
			}
		},
		[this](int32 Dir) { Draft.Quality = Draft.Quality < 0 ? (Dir > 0 ? 0 : 3) : (Draft.Quality + Dir + 4) % 4; },
		[this]() { return Draft.Quality != Original.Quality; }));

	Add(MakeChoiceRow(TEXT("Settings.FrameRate"), LOCTEXT("FrameRate", "帧率上限"),
		LOCTEXT("FrameRateHint", "限制最高帧率可以降低功耗、发热与风扇噪音。"),
		[this]()
		{
			if (Same(Draft.FrameRateLimit, 0.f)) return LOCTEXT("FrameRateUnlimited", "不限");
			return FText::FromString(FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(Draft.FrameRateLimit)));
		},
		[this](int32 Dir)
		{
			int32 Index = INDEX_NONE;
			for (int32 i = 0; i < NumFrameRates; ++i)
			{
				if (Same(FrameRates[i], Draft.FrameRateLimit)) Index = i;
			}
			Index = Index == INDEX_NONE ? (Dir > 0 ? 0 : NumFrameRates - 1) : (Index + Dir + NumFrameRates) % NumFrameRates;
			Draft.FrameRateLimit = FrameRates[Index];
		},
		[this]() { return !Same(Draft.FrameRateLimit, Original.FrameRateLimit); }));

	Add(MakeToggleRow(TEXT("Settings.VSync"), LOCTEXT("VSync", "垂直同步"),
		LOCTEXT("VSyncHint", "消除画面撕裂，可能略微增加输入延迟。"), &FNCSettingsDraft::bVSync));

	// Read once at launch: applying it never touches the current window or the 15 s display trial.
	Add(MakeToggleRow(TEXT("Settings.StartInBorderless"), LOCTEXT("StartInBorderless", "启动时自动无边框"),
		LOCTEXT("StartInBorderlessHint", "开启时，每次启动都以无边框窗口铺满桌面；关闭则沿用保存的显示模式。下次启动生效。"),
		&FNCSettingsDraft::bStartInBorderless));

	return Box;
}

TSharedRef<SWidget> SNCFrontEnd::BuildControlsTab()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Add = [&Box](TSharedRef<SWidget> Row) { Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Row]; };

	Add(MakeSliderRow(TEXT("Settings.MouseSensitivity"), LOCTEXT("MouseSensitivity", "鼠标灵敏度"),
		LOCTEXT("MouseSensitivityHint", "鼠标转动镜头的速度。100% 为默认值。"),
		&FNCSettingsDraft::MouseSensitivity, .25f, 3.f, .05f));
	Add(MakeToggleRow(TEXT("Settings.InvertMouseY"), LOCTEXT("InvertMouseY", "反转鼠标垂直视角"),
		LOCTEXT("InvertMouseYHint", "关闭时：鼠标向前推 = 镜头向上看。开启后上下方向相反。"), &FNCSettingsDraft::bInvertMouseY));
	Add(MakeSliderRow(TEXT("Settings.GamepadSensitivity"), LOCTEXT("GamepadSensitivity", "手柄灵敏度"),
		LOCTEXT("GamepadSensitivityHint", "右摇杆转动镜头的速度。100% 为默认值。"),
		&FNCSettingsDraft::GamepadSensitivity, .25f, 3.f, .05f));
	Add(MakeToggleRow(TEXT("Settings.InvertGamepadY"), LOCTEXT("InvertGamepadY", "反转手柄垂直视角"),
		LOCTEXT("InvertGamepadYHint", "关闭时：右摇杆向上推 = 镜头向上看。习惯飞行式操作时再开启。"), &FNCSettingsDraft::bInvertGamepadY));
	Add(MakeSliderRow(TEXT("Settings.GamepadDeadZone"), LOCTEXT("DeadZone", "右摇杆死区"),
		LOCTEXT("DeadZoneHint", "摇杆松开后镜头仍在漂移时调高；数值越低，细微推动越灵敏。"),
		&FNCSettingsDraft::GamepadDeadZone, .05f, .35f, .01f));

	return Box;
}

TSharedRef<SWidget> SNCFrontEnd::BuildAudioTab()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Add = [&Box](TSharedRef<SWidget> Row) { Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Row]; };

	Add(MakeSliderRow(TEXT("Settings.MasterVolume"), LOCTEXT("MasterVolume", "主音量"),
		LOCTEXT("MasterVolumeHint", "所有声音的总音量。"), &FNCSettingsDraft::MasterVolume, 0.f, 1.f, .05f));
	Add(MakeSliderRow(TEXT("Settings.MusicVolume"), LOCTEXT("MusicVolume", "音乐"),
		LOCTEXT("MusicVolumeHint", "车载电台与背景音乐。"), &FNCSettingsDraft::MusicVolume, 0.f, 1.f, .05f));
	Add(MakeSliderRow(TEXT("Settings.EffectsVolume"), LOCTEXT("EffectsVolume", "音效"),
		LOCTEXT("EffectsVolumeHint", "引擎、喇叭、环境与界面音效。"), &FNCSettingsDraft::EffectsVolume, 0.f, 1.f, .05f));
	Add(MakeAudioPreviewRow());

	return Box;
}

TSharedRef<SWidget> SNCFrontEnd::MakeAudioPreviewRow()
{
	// Plays at the APPLIED levels, never the draft, so the value shows what will actually be heard.
	TSharedRef<TWeakPtr<SWidget>> Target = MakeShared<TWeakPtr<SWidget>>();
	auto Focused = [Target]() { return HasFocus(*Target); };
	auto Muted = []()
	{
		const UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
		return !Settings || Settings->GetMasterVolume() <= KINDA_SMALL_NUMBER || Settings->GetEffectsVolume() <= KINDA_SMALL_NUMBER;
	};

	TSharedPtr<SNCButton> Button;
	SAssignNew(Button, SNCButton)
		.Tag(TEXT("Settings.AudioPreview"))
		.ButtonStyle(&ButtonStyle)
		.ContentPadding(FMargin(0.f))
		.OnClicked(FOnClicked::CreateSP(this, &SNCFrontEnd::OnAudioPreview))
		[
			SNew(SBox)
			.HeightOverride(48.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 18.f, 0.f)
				[
					SNew(STextBlock)
					.Text_Lambda([Muted]()
					{
						const UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
						if (Muted() || !Settings) return LOCTEXT("PreviewMuted", "主音量或音效为 0");
						return FText::Format(LOCTEXT("PreviewLevels", "已应用 · 主 {0}% · 音效 {1}%"),
							FText::AsNumber(FMath::RoundToInt(Settings->GetMasterVolume() * 100.f)),
							FText::AsNumber(FMath::RoundToInt(Settings->GetEffectsVolume() * 100.f)));
					})
					.Font(Font("Regular", 20))
					.ColorAndOpacity_Lambda([Focused, Muted]() { return FSlateColor(Muted() ? Mist(.8f) : Focused() ? Cream() : Sand(.85f)); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SBorder)
					.BorderImage(&PillBrush)
					.BorderBackgroundColor_Lambda([Focused, Muted]() { return FSlateColor(Muted() ? Cream(.08f) : Focused() ? Teal() : Teal(.16f)); })
					.Padding(FMargin(18.f, 4.f, 18.f, 5.f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PreviewPlay", "试听"))
						.Font(Font("Bold", 18, 60))
						.ColorAndOpacity_Lambda([Focused, Muted]() { return FSlateColor(Muted() ? Mist(.7f) : Focused() ? Ink() : Teal()); })
					]
				]
			]
		];
	*Target = Button;
	Tagged.Add(TEXT("Settings.AudioPreview"), Button);

	return MakeRowFrame(LOCTEXT("AudioPreview", "试听当前音量"),
		LOCTEXT("AudioPreviewHint", "先应用音量更改，再试听。试听使用已保存的主音量与音效音量。"),
		Button.ToSharedRef(), Target, nullptr);
}

// ---------------------------------------------------------------------------------------------------------------------
// Controls guide

TSharedRef<SWidget> SNCFrontEnd::BuildControlsPage()
{
	TSharedPtr<SWidget> Adjust, Back;
	TSharedRef<SWidget> AdjustButton = MakeButton(TEXT("Controls.OpenSettings"), LOCTEXT("AdjustControls", "调整操控设置"),
		FOnClicked::CreateSP(this, &SNCFrontEnd::OnOpenControlSettings), EButtonKind::Secondary, &Adjust);
	TSharedRef<SWidget> BackButton = MakeButton(TEXT("Controls.Back"), LOCTEXT("ControlsBack", "返回"),
		FOnClicked::CreateSP(this, &SNCFrontEnd::OnControlsBack), EButtonKind::Primary, &Back);

	// Tutorial replay (title and pause). Skip only appears while a tutorial is running.
	ANCPlayerController* PC = Owner.Get();
	const UNCOnboardingComponent* Onboarding = PC ? PC->GetOnboarding() : nullptr;
	const bool bOnboardingActive = Onboarding && Onboarding->IsActive();
	TSharedPtr<SWidget> Replay, SkipTutorial;
	TSharedRef<SWidget> ReplayButton = MakeButton(TEXT("Controls.RestartOnboarding"), LOCTEXT("RestartOnboarding", "重新体验新手引导"),
		FOnClicked::CreateSP(this, &SNCFrontEnd::OnRestartOnboarding), EButtonKind::Secondary, &Replay);
	TSharedRef<SWidget> SkipButton = SNullWidget::NullWidget;
	if (bOnboardingActive)
	{
		SkipButton = MakeButton(TEXT("Controls.SkipOnboarding"), LOCTEXT("SkipOnboarding", "跳过引导"),
			FOnClicked::CreateSP(this, &SNCFrontEnd::OnSkipOnboarding), EButtonKind::Secondary, &SkipTutorial);
	}

	FNavRow Footer;
	Footer.Items.Add(Replay);
	if (SkipTutorial.IsValid())
	{
		Footer.Items.Add(SkipTutorial);
	}
	Footer.Items.Add(Adjust);
	Footer.Items.Add(Back);
	Footer.Entry = Footer.Items.Num() - 1;
	TailNav.Add(Footer);
	PageDefault = Back;

	const FText Mouse = LOCTEXT("KeyMouse", "鼠标");
	const FText Dash = FText::GetEmpty();

	TArray<TSharedRef<SWidget>> OnFoot =
	{
		MakeHelpLine(LOCTEXT("HelpMove", "移动"), INVTEXT("W A S D"), LOCTEXT("PadLeftStick", "左摇杆")),
		MakeHelpLine(LOCTEXT("HelpSprint", "冲刺"), INVTEXT("Shift"), INVTEXT("L3")),
		MakeHelpLine(LOCTEXT("HelpJump", "跳跃"), INVTEXT("Space"), INVTEXT("A")),
		MakeHelpLine(LOCTEXT("HelpEnter", "上车 / 下车"), INVTEXT("F"), INVTEXT("Y")),
		MakeHelpLine(LOCTEXT("HelpInteract", "互动 / 接受任务"), INVTEXT("E"), LOCTEXT("PadDpadRight", "十字键 →")),
		MakeHelpLine(LOCTEXT("HelpGreet", "打招呼"), INVTEXT("E"), INVTEXT("X")),
		MakeHelpLine(LOCTEXT("HelpAim", "瞄准"), LOCTEXT("KeyRmb", "鼠标右键"), INVTEXT("LT")),
		MakeHelpLine(LOCTEXT("HelpFire", "射击"), LOCTEXT("KeyLmb", "鼠标左键"), INVTEXT("RT")),
		MakeHelpLine(LOCTEXT("HelpReload", "换弹"), INVTEXT("R"), INVTEXT("X")),
	};
	TArray<TSharedRef<SWidget>> Driving =
	{
		MakeHelpLine(LOCTEXT("HelpThrottle", "油门 / 倒车"), INVTEXT("W / S"), INVTEXT("RT / LT")),
		MakeHelpLine(LOCTEXT("HelpSteer", "转向"), INVTEXT("A / D"), LOCTEXT("PadLeftStick2", "左摇杆")),
		MakeHelpLine(LOCTEXT("HelpBrake", "刹车"), INVTEXT("Space"), INVTEXT("B")),
		MakeHelpLine(LOCTEXT("HelpBoost", "加速"), INVTEXT("Shift"), INVTEXT("A")),
		MakeHelpLine(LOCTEXT("HelpLights", "车灯"), INVTEXT("L"), LOCTEXT("PadDpadUp", "十字键 ↑")),
		MakeHelpLine(LOCTEXT("HelpRecover", "车辆复位"), INVTEXT("T"), LOCTEXT("PadDpadDown", "十字键 ↓")),
		MakeHelpLine(LOCTEXT("HelpRadio", "切换电台"), INVTEXT("Q"), LOCTEXT("PadDpadLeft", "十字键 ←")),
		MakeHelpLine(LOCTEXT("HelpHorn", "喇叭"), INVTEXT("H"), INVTEXT("L3")),
		MakeHelpLine(LOCTEXT("HelpExit", "下车"), INVTEXT("F"), INVTEXT("Y")),
	};
	TArray<TSharedRef<SWidget>> GeneralA =
	{
		MakeHelpLine(LOCTEXT("HelpLook", "转动镜头"), Mouse, LOCTEXT("PadRightStick", "右摇杆")),
		MakeHelpLine(LOCTEXT("HelpMap", "地图"), INVTEXT("M"), LOCTEXT("PadView", "视图键")),
		MakeHelpLine(LOCTEXT("HelpPause", "暂停菜单"), INVTEXT("Esc"), LOCTEXT("PadMenu", "菜单键")),
	};
	TArray<TSharedRef<SWidget>> GeneralB =
	{
		MakeHelpLine(LOCTEXT("HelpHelp", "按键提示"), INVTEXT("F1"), Dash),
		MakeHelpLine(LOCTEXT("HelpPhoto", "拍照模式"), INVTEXT("F6"), Dash),
		MakeHelpLine(LOCTEXT("HelpShot", "截图"), INVTEXT("F12"), Dash),
	};

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[MakeHelpSection(LOCTEXT("SectionOnFoot", "步行"), OnFoot)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(56.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[MakeHelpSection(LOCTEXT("SectionDriving", "驾驶"), Driving)]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 22.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[MakeHelpSection(LOCTEXT("SectionGeneral", "通用"), GeneralA)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(56.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[MakeHelpSection(LOCTEXT("SectionMore", "其他"), GeneralB)]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 20.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(&SoftBrush)
			.BorderBackgroundColor(Teal(.08f))
			.Padding(FMargin(0.f, 14.f, 22.f, 14.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 18.f, 0.f)
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
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CameraNote", "镜头方向：默认右摇杆向上推 = 镜头向上看，鼠标向前推同理。喜欢飞行式反转，可在“设置 › 操控”中分别开启鼠标或手柄的垂直反转；灵敏度与右摇杆死区也在那里调整。"))
					.Font(Font("Regular", 18))
					.ColorAndOpacity(Sand(.95f))
					.AutoWrapText(true)
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNullWidget::NullWidget
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 18.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ReplayButton]
			+ SHorizontalBox::Slot().AutoWidth().Padding(bOnboardingActive ? 12.f : 0.f, 0.f, 0.f, 0.f)[SkipButton]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(18.f, 0.f, 18.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SkipOnboardingTip", "游戏中按 F2 跳过引导"))
				.Font(Font("Regular", 17))
				.ColorAndOpacity(Mist())
				.AutoWrapText(true)
			]
			+ SHorizontalBox::Slot().AutoWidth()[AdjustButton]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12.f, 0.f, 0.f, 0.f)[BackButton]
		];

	return BuildPanel(LOCTEXT("ControlsEyebrow", "键盘 · 鼠标 · 手柄"), LOCTEXT("ControlsTitle", "操作指南"), Body, 1180.f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialogs

TSharedRef<SWidget> SNCFrontEnd::BuildDialogCard(const FText& Eyebrow, const FText& Title, const FLinearColor& Accent, TSharedRef<SWidget> Body, TSharedRef<SWidget> Buttons)
{
	return SNew(SBorder)
		.BorderImage(&WhiteBrush)
		.BorderBackgroundColor(DeepInk(.62f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(0.f)
		[
			SNew(SBox)
			.WidthOverride(660.f)
			[
				SNew(SBorder)
				.BorderImage(&PanelBrush)
				.BorderBackgroundColor(Ink(.98f))
				.Padding(FMargin(44.f, 38.f, 44.f, 38.f))
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
								.BorderBackgroundColor(Accent)
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(12.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(Eyebrow)
							.Font(Font("Bold", 16, 300))
							.ColorAndOpacity(Accent)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 12.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(Title)
						.Font(Font("Bold", 34))
						.ColorAndOpacity(Cream())
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 14.f, 0.f, 0.f)
					[
						Body
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 34.f, 0.f, 0.f)
					[
						Buttons
					]
				]
			]
		];
}

TSharedRef<SWidget> SNCFrontEnd::BuildQuitDialog()
{
	TSharedPtr<SWidget> Cancel, Confirm;
	TSharedRef<SWidget> CancelButton = MakeButton(TEXT("Dialog.Cancel"), LOCTEXT("QuitCancel", "取消"),
		FOnClicked::CreateSP(this, &SNCFrontEnd::OnDialogCancel), EButtonKind::Secondary, &Cancel);
	TSharedRef<SWidget> ConfirmButton = MakeButton(TEXT("Dialog.Confirm"), LOCTEXT("QuitConfirm", "退出到桌面"),
		FOnClicked::CreateSP(this, &SNCFrontEnd::OnQuitConfirmed), EButtonKind::Primary, &Confirm);
	FNavRow Row;
	Row.Items = { Cancel, Confirm };
	DialogNav.Add(Row);
	DialogDefault = Cancel;

	return BuildDialogCard(LOCTEXT("QuitEyebrow", "退出游戏"), LOCTEXT("QuitTitle", "要退出到桌面吗？"), Amber(),
		SNew(STextBlock)
		.Text(bTitle ? LOCTEXT("QuitBodyTitle", "你的进度已自动保存，下次启动可以继续。") : LOCTEXT("QuitBodyPause", "退出前会自动保存当前进度。"))
		.Font(Font("Regular", 20))
		.ColorAndOpacity(Sand())
		.AutoWrapText(true),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget]
		+ SHorizontalBox::Slot().AutoWidth()[CancelButton]
		+ SHorizontalBox::Slot().AutoWidth().Padding(12.f, 0.f, 0.f, 0.f)[ConfirmButton]);
}

TSharedRef<SWidget> SNCFrontEnd::BuildVideoDialog()
{
	TSharedPtr<SWidget> Revert, Keep;
	TSharedRef<SWidget> RevertButton = MakeButton(TEXT("Dialog.Revert"), LOCTEXT("VideoRevert", "恢复"),
		FOnClicked::CreateSP(this, &SNCFrontEnd::OnRevertVideo), EButtonKind::Secondary, &Revert);
	TSharedRef<SWidget> KeepButton = MakeButton(TEXT("Dialog.Keep"), LOCTEXT("VideoKeep", "保留设置"),
		FOnClicked::CreateSP(this, &SNCFrontEnd::OnKeepVideo), EButtonKind::Primary, &Keep);
	FNavRow Row;
	Row.Items = { Revert, Keep };
	DialogNav.Add(Row);
	DialogDefault = Revert;	// a blind confirm press on a broken display must not keep it

	const FText Current = FText::Format(LOCTEXT("VideoCurrent", "现在显示：{0} · {1}。画面正常的话，请选择“保留设置”。"),
		WindowModeText(Draft.WindowMode), ResolutionText(EffectiveResolution(Draft)));
	constexpr float TrackWidth = 572.f;

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Current)
			.Font(Font("Regular", 20))
			.ColorAndOpacity(Sand())
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 22.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const int32 Seconds = FMath::Max(0, FMath::CeilToInt(VideoDeadline - FPlatformTime::Seconds()));
				return FText::Format(LOCTEXT("VideoCountdown", "{0} 秒后自动恢复为之前的显示设置"), FText::AsNumber(Seconds));
			})
			.Font(Font("Bold", 19))
			.ColorAndOpacity(Amber())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(&BarBrush)
			.BorderBackgroundColor(Cream(.10f))
			.HAlign(HAlign_Left)
			.Padding(0.f)
			[
				SNew(SBox)
				.HeightOverride(4.f)
				.WidthOverride_Lambda([this, TrackWidth]()
				{
					const double Left = FMath::Clamp((VideoDeadline - FPlatformTime::Seconds()) / VideoConfirmSeconds, 0.0, 1.0);
					return FOptionalSize(static_cast<float>(Left) * TrackWidth);
				})
				[
					SNew(SBorder)
					.BorderImage(&BarBrush)
					.BorderBackgroundColor(Amber())
				]
			]
		];

	return BuildDialogCard(LOCTEXT("VideoEyebrow", "显示设置"), LOCTEXT("VideoTitle", "保留这些显示设置吗？"), Teal(), Body,
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget]
		+ SHorizontalBox::Slot().AutoWidth()[RevertButton]
		+ SHorizontalBox::Slot().AutoWidth().Padding(12.f, 0.f, 0.f, 0.f)[KeepButton]);
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared layout pieces

TSharedRef<SWidget> SNCFrontEnd::BuildPanel(const FText& Eyebrow, const FText& Title, TSharedRef<SWidget> Body, float Width)
{
	return SNew(SBox)
		.Padding(FMargin(120.f, 64.f, 120.f, 96.f))
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(Width)
			[
				SNew(SBorder)
				.BorderImage(&PanelBrush)
				.BorderBackgroundColor(Ink(.90f))
				.Padding(FMargin(48.f, 36.f, 48.f, 32.f))
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
								.BorderBackgroundColor(Amber())
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(12.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(Eyebrow)
							.Font(Font("Bold", 16, 300))
							.ColorAndOpacity(Teal())
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(INVTEXT("NEON COAST"))
							.Font(Font("Bold", 15, 400))
							.ColorAndOpacity(Cream(.35f))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(Title)
						.Font(Font("Bold", 42))
						.ColorAndOpacity(Cream())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 20.f, 0.f, 0.f)
					[
						SNew(SBox)
						.HeightOverride(1.f)
						[
							SNew(SBorder)
							.BorderImage(&WhiteBrush)
							.BorderBackgroundColor(Cream(.10f))
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(0.f, 22.f, 0.f, 0.f)
					[
						Body
					]
				]
			]
		];
}

TSharedRef<SWidget> SNCFrontEnd::MakeButton(FName WidgetTag, const FText& Label, FOnClicked OnClicked, EButtonKind Kind, TSharedPtr<SWidget>* OutWidget, TAttribute<bool> bSelected, TFunction<void()> OnNavigationFocus)
{
	TSharedRef<TWeakPtr<SNCButton>> Self = MakeShared<TWeakPtr<SNCButton>>();
	auto Focused = [Self]() { const TSharedPtr<SNCButton> B = Self->Pin(); return B.IsValid() && B->HasAnyUserFocus().IsSet(); };
	auto Pressed = [Self]() { const TSharedPtr<SNCButton> B = Self->Pin(); return B.IsValid() && B->IsPressed(); };
	auto Selected = [bSelected]() { return bSelected.Get(); };

	// One focus language everywhere: cream plate + ink text, teal edge; pressed flashes teal.
	auto Background = [Focused, Pressed, Selected, Kind]() -> FSlateColor
	{
		if (Pressed()) return Teal(.95f);
		if (Focused()) return Cream(.96f);
		switch (Kind)
		{
		case EButtonKind::Primary: return Teal(.16f);
		case EButtonKind::Secondary: return Cream(.07f);
		case EButtonKind::Tab: return Selected() ? Cream(.08f) : Cream(0.f);
		default: return Cream(0.f);
		}
	};
	auto TextColor = [Focused, Pressed, Selected, Kind]() -> FSlateColor
	{
		if (Focused() || Pressed()) return Ink();
		switch (Kind)
		{
		case EButtonKind::Primary: return Teal();
		case EButtonKind::Tab: return Selected() ? Cream() : Mist();
		default: return Cream(.9f);
		}
	};
	auto AccentColor = [Focused, Selected, Kind]() -> FSlateColor
	{
		if (Focused()) return Teal();
		return Kind == EButtonKind::Tab && Selected() ? Teal() : Teal(0.f);
	};

	TSharedRef<SWidget> Inner = SNullWidget::NullWidget;
	float Height = 52.f;
	float MinWidth = 168.f;
	if (Kind == EButtonKind::Menu)
	{
		Height = 58.f;
		MinWidth = 0.f;
		Inner = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(4.f)
				[
					SNew(SBorder)
					.BorderImage(&WhiteBrush)
					.BorderBackgroundColor_Lambda(AccentColor)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(24.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font("Regular", 25))
				.ColorAndOpacity_Lambda(TextColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 22.f, 2.f)
			[
				SNew(STextBlock)
				.Text(INVTEXT("›"))
				.Font(Font("Bold", 28))
				.ColorAndOpacity(Ink())
				.Visibility_Lambda([Focused]() { return Focused() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
			];
	}
	else if (Kind == EButtonKind::Tab)
	{
		Height = 50.f;
		MinWidth = 150.f;
		Inner = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(28.f, 0.f)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font("Bold", 21, 120))
				.ColorAndOpacity_Lambda(TextColor)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(22.f, 0.f, 22.f, 0.f)
			[
				SNew(SBox)
				.HeightOverride(3.f)
				[
					SNew(SBorder)
					.BorderImage(&WhiteBrush)
					.BorderBackgroundColor_Lambda(AccentColor)
				]
			];
	}
	else
	{
		Inner = SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(28.f, 0.f))
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font(Kind == EButtonKind::Primary ? "Bold" : "Regular", 20, Kind == EButtonKind::Primary ? 60 : 0))
				.ColorAndOpacity_Lambda(TextColor)
			];
	}

	TSharedPtr<SNCButton> Button;
	SAssignNew(Button, SNCButton)
		.Tag(WidgetTag)
		.ButtonStyle(&ButtonStyle)
		.ContentPadding(FMargin(0.f))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.OnClicked(OnClicked)
		[
			SNew(SBox)
			.HeightOverride(Height)
			.MinDesiredWidth(MinWidth)
			[
				SNew(SBorder)
				.BorderImage(&SoftBrush)
				.BorderBackgroundColor_Lambda(Background)
				.Padding(0.f)
				[
					Inner
				]
			]
		];
	Button->OnNavigationFocus = MoveTemp(OnNavigationFocus);
	*Self = Button;
	Tagged.Add(WidgetTag, Button);
	if (OutWidget)
	{
		*OutWidget = Button;
	}
	return Button.ToSharedRef();
}

TSharedRef<SWidget> SNCFrontEnd::MakeRowFrame(const FText& Label, const FText& Hint, TSharedRef<SWidget> Control, TSharedRef<TWeakPtr<SWidget>> Target, TFunction<bool()> IsDirty, TFunction<void()> OnRowClick)
{
	BodyNav.Add(FNavRow{ { *Target } });
	RowHints.Add({ *Target, Hint, true });
	if (!TabFirstFocus.IsValid())
	{
		TabFirstFocus = *Target;
	}

	auto Focused = [Target]() { return HasFocus(*Target); };
	auto Enabled = [Target]() { const TSharedPtr<SWidget> W = Target->Pin(); return W.IsValid() && W->IsEnabled(); };

	return SNew(SNCRow, Target, MoveTemp(OnRowClick))
		[
			SNew(SBorder)
			.BorderImage(&SoftBrush)
			.BorderBackgroundColor_Lambda([Focused]() { return FSlateColor(Focused() ? Cream(.09f) : Cream(.025f)); })
			.Padding(0.f)
			[
				SNew(SBox)
				.HeightOverride(60.f)
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
							.BorderBackgroundColor_Lambda([Focused]() { return FSlateColor(Focused() ? Teal() : Teal(0.f)); })
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(22.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(Label)
						.Font(Font("Regular", 22))
						.ColorAndOpacity_Lambda([Focused, Enabled]()
						{
							if (!Enabled()) return FSlateColor(Mist(.55f));
							return FSlateColor(Focused() ? Cream() : Sand(.92f));
						})
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(12.f, 0.f, 0.f, 0.f)
					[
						// Changed but not yet applied.
						SNew(SBox)
						.WidthOverride(8.f)
						.HeightOverride(8.f)
						.HAlign(HAlign_Left)
						.Visibility_Lambda([IsDirty]() { return IsDirty && IsDirty() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
						[
							SNew(SBorder)
							.BorderImage(&PillBrush)
							.BorderBackgroundColor(Amber())
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 18.f, 0.f)
					[
						SNew(SBox)
						.WidthOverride(440.f)
						[
							Control
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SNCFrontEnd::MakeChoiceRow(FName WidgetTag, const FText& Label, const FText& Hint, TFunction<FText()> GetValue, TFunction<void(int32)> Step, TFunction<bool()> IsDirty, TAttribute<bool> bEnabled)
{
	TSharedRef<TWeakPtr<SWidget>> Target = MakeShared<TWeakPtr<SWidget>>();
	auto Focused = [Target]() { return HasFocus(*Target); };

	auto Arrow = [this, Step, Focused, Target](const FText& Glyph, int32 Direction) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(&ButtonStyle)
			.ContentPadding(FMargin(16.f, 0.f))
			.IsFocusable(false)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([Step, Direction, Target]()
			{
				Step(Direction);
				if (const TSharedPtr<SWidget> W = Target->Pin())
				{
					FSlateApplication::Get().SetAllUserFocus(W, EFocusCause::Mouse);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(Glyph)
				.Font(Font("Bold", 28))
				.ColorAndOpacity_Lambda([Focused]() { return FSlateColor(Focused() ? Teal() : Mist(.75f)); })
			];
	};

	TSharedPtr<SNCButton> Button;
	SAssignNew(Button, SNCButton)
		.Tag(WidgetTag)
		.ButtonStyle(&ButtonStyle)
		.ContentPadding(FMargin(0.f))
		.IsEnabled(bEnabled)
		.OnClicked_Lambda([Step]() { Step(1); return FReply::Handled(); })
		[
			SNew(SBox)
			.HeightOverride(48.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					Arrow(INVTEXT("‹"), -1)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([GetValue]() { return GetValue(); })
					.Font(Font("Regular", 22))
					.ColorAndOpacity_Lambda([Focused]() { return FSlateColor(Focused() ? Teal() : Cream()); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					Arrow(INVTEXT("›"), 1)
				]
			]
		];
	Button->OnNavigate = [Step](EUINavigation Nav)
	{
		if (Nav == EUINavigation::Left) { Step(-1); return true; }
		if (Nav == EUINavigation::Right) { Step(1); return true; }
		return false;
	};
	*Target = Button;
	Tagged.Add(WidgetTag, Button);
	return MakeRowFrame(Label, Hint, Button.ToSharedRef(), Target, MoveTemp(IsDirty));
}

TSharedRef<SWidget> SNCFrontEnd::MakeToggleRow(FName WidgetTag, const FText& Label, const FText& Hint, bool FNCSettingsDraft::* Field)
{
	TSharedRef<TWeakPtr<SWidget>> Target = MakeShared<TWeakPtr<SWidget>>();
	auto Focused = [Target]() { return HasFocus(*Target); };
	auto On = [this, Field]() { return Draft.*Field; };

	TSharedPtr<SNCCheckBox> Check;
	SAssignNew(Check, SNCCheckBox)
		.Tag(WidgetTag)
		.Style(&ToggleStyle)
		.Padding(FMargin(0.f))
		.IsChecked_Lambda([On]() { return On() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Field](ECheckBoxState State) { Draft.*Field = State == ECheckBoxState::Checked; })
		[
			SNew(SBox)
			.HeightOverride(48.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 18.f, 0.f)
				[
					SNew(STextBlock)
					.Text_Lambda([On]() { return On() ? LOCTEXT("On", "开启") : LOCTEXT("Off", "关闭"); })
					.Font(Font("Regular", 22))
					.ColorAndOpacity_Lambda([Focused, On]() { return FSlateColor(Focused() ? Teal() : On() ? Cream() : Sand(.8f)); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 16.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(56.f)
					.HeightOverride(28.f)
					[
						SNew(SBorder)
						.BorderImage(&PillBrush)
						.BorderBackgroundColor_Lambda([On, Focused]() { return FSlateColor(On() ? Teal() : Cream(Focused() ? .34f : .2f)); })
						.Padding(FMargin(5.f, 0.f))
						.VAlign(VAlign_Center)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(&KnobBrush)
								.ColorAndOpacity(Cream())
								.Visibility_Lambda([On]() { return On() ? EVisibility::Hidden : EVisibility::HitTestInvisible; })
							]
							+ SOverlay::Slot()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(&KnobBrush)
								.ColorAndOpacity(Ink())
								.Visibility_Lambda([On]() { return On() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
							]
						]
					]
				]
			]
		];
	*Target = Check;
	Tagged.Add(WidgetTag, Check);

	return MakeRowFrame(Label, Hint, Check.ToSharedRef(), Target,
		[this, Field]() { return Draft.*Field != Original.*Field; },
		[this, Field]() { Draft.*Field = !(Draft.*Field); });	// clicking the label flips the switch too
}

TSharedRef<SWidget> SNCFrontEnd::MakeSliderRow(FName WidgetTag, const FText& Label, const FText& Hint, float FNCSettingsDraft::* Field, float Min, float Max, float Step)
{
	TSharedRef<TWeakPtr<SWidget>> Target = MakeShared<TWeakPtr<SWidget>>();
	auto Focused = [Target]() { return HasFocus(*Target); };

	TSharedPtr<SNCSlider> Slider;
	SAssignNew(Slider, SNCSlider)
		.Tag(WidgetTag)
		.Style(&SliderStyle)
		.MinValue(Min)
		.MaxValue(Max)
		.StepSize(Step)
		.RequiresControllerLock(false)	// left/right adjusts directly; up/down leaves the row
		.Value_Lambda([this, Field]() { return Draft.*Field; })
		.OnValueChanged_Lambda([this, Field, Min, Max, Step](float Value) { Draft.*Field = FMath::Clamp(FMath::GridSnap(Value, Step), Min, Max); })
		.SliderBarColor_Lambda([Focused]() { return FSlateColor(Cream(Focused() ? .38f : .2f)); })
		.SliderHandleColor_Lambda([Focused]() { return FSlateColor(Focused() ? Teal() : Cream()); });
	*Target = Slider;
	Tagged.Add(WidgetTag, Slider);

	TSharedRef<SWidget> Control = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(16.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.HeightOverride(40.f)
			.VAlign(VAlign_Center)
			[
				Slider.ToSharedRef()
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(18.f, 0.f, 16.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(76.f)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text_Lambda([this, Field]() { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Draft.*Field * 100.f))); })
				.Font(Font("Regular", 22))
				.ColorAndOpacity_Lambda([Focused]() { return FSlateColor(Focused() ? Teal() : Cream()); })
			]
		];

	return MakeRowFrame(Label, Hint, Control, Target, [this, Field]() { return !Same(Draft.*Field, Original.*Field); });
}

TSharedRef<SWidget> SNCFrontEnd::MakeKeyCap(const FText& Key, bool bGamepad)
{
	return SNew(SBorder)
		.BorderImage(&OutlineBrush)
		.BorderBackgroundColor(bGamepad ? Teal(.8f) : Cream(.5f))
		.Padding(FMargin(9.f, 2.f, 9.f, 3.f))
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Key)
			.Font(Font("Bold", 15, 40))
			.ColorAndOpacity(bGamepad ? Teal() : Cream(.92f))
		];
}

TSharedRef<SWidget> SNCFrontEnd::MakeHint(const FText& Keyboard, const FText& Gamepad, const FText& Action)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeKeyCap(Keyboard, false)
		];
	if (!Gamepad.IsEmpty())
	{
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.f, 0.f, 0.f, 0.f)
		[
			MakeKeyCap(Gamepad, true)
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

TSharedRef<SWidget> SNCFrontEnd::MakeHelpLine(const FText& Action, const FText& Keyboard, const FText& Gamepad)
{
	return SNew(SBox)
		.HeightOverride(34.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Action)
				.Font(Font("Regular", 19))
				.ColorAndOpacity(Cream(.92f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(150.f)
				.HAlign(HAlign_Left)
				[
					MakeKeyCap(Keyboard, false)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(120.f)
				.HAlign(HAlign_Left)
				[
					Gamepad.IsEmpty()
						? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(INVTEXT("—")).Font(Font("Regular", 17)).ColorAndOpacity(Mist(.6f)))
						: MakeKeyCap(Gamepad, true)
				]
			]
		];
}

TSharedRef<SWidget> SNCFrontEnd::MakeHelpSection(const FText& Title, const TArray<TSharedRef<SWidget>>& Lines)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(Font("Bold", 18, 260))
				.ColorAndOpacity(Teal())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.WidthOverride(150.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ColumnKeyboard", "键盘 / 鼠标"))
					.Font(Font("Regular", 14))
					.ColorAndOpacity(Mist())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.WidthOverride(120.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ColumnGamepad", "手柄"))
					.Font(Font("Regular", 14))
					.ColorAndOpacity(Mist())
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 8.f, 0.f, 4.f)
		[
			SNew(SBox)
			.HeightOverride(1.f)
			[
				SNew(SBorder)
				.BorderImage(&WhiteBrush)
				.BorderBackgroundColor(Cream(.10f))
			]
		];
	for (const TSharedRef<SWidget>& Line : Lines)
	{
		Box->AddSlot().AutoHeight()[Line];
	}
	return Box;
}

// ---------------------------------------------------------------------------------------------------------------------
// Actions

FReply SNCFrontEnd::OnPrimary()
{
	if (ANCPlayerController* PC = Owner.Get())
	{
		PC->StartOrResumeGame();
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnOpenSettings()
{
	RootReturnTag = TEXT("Menu.Settings");
	OpenSettingsAt(ETab::Video);
	return FReply::Handled();
}

FReply SNCFrontEnd::OnOpenControls()
{
	RootReturnTag = TEXT("Menu.Controls");
	ShowPage(EPage::Controls);
	return FReply::Handled();
}

FReply SNCFrontEnd::OnOpenControlSettings()
{
	OpenSettingsAt(ETab::Controls);
	return FReply::Handled();
}

FReply SNCFrontEnd::OnControlsBack()
{
	LeaveSubPage();
	return FReply::Handled();
}

FReply SNCFrontEnd::OnRestartOnboarding()
{
	if (ANCPlayerController* PC = Owner.Get())
	{
		PC->RestartOnboarding();	// the controller closes the menu and shows the welcome card
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnSkipOnboarding()
{
	ANCPlayerController* PC = Owner.Get();
	if (!PC)
	{
		return FReply::Handled();
	}
	PC->SkipOnboarding();
	// If the controller kept the menu open, drop the now-stale skip button and stay on the replay action.
	if (PC->IsFrontEndOpen() && Page == EPage::Controls && Dialog == EDialog::None)
	{
		ShowPage(EPage::Controls, TEXT("Controls.RestartOnboarding"));
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnAudioPreview()
{
	// Deliberately the applied settings: the draft is not heard until 应用.
	const UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
	if (!Settings || Settings->GetMasterVolume() <= KINDA_SMALL_NUMBER || Settings->GetEffectsVolume() <= KINDA_SMALL_NUMBER)
	{
		SetStatus(LOCTEXT("PreviewSilent", "主音量或音效为 0，无法试听。"), Amber());
		return FReply::Handled();
	}
	ANCPlayerController* PC = Owner.Get();
	if (!PC || !PC->PreviewAudio())
	{
		SetStatus(LOCTEXT("PreviewFailed", "暂时无法试听。"), Coral());
		return FReply::Handled();
	}
	SetStatus(Draft.AudioEquals(Original)
		? LOCTEXT("PreviewPlaying", "正在试听当前音量。")
		: LOCTEXT("PreviewApplied", "试听的是已应用的音量。"), Draft.AudioEquals(Original) ? Teal() : Amber());
	return FReply::Handled();
}

FReply SNCFrontEnd::OnReturnToTitle()
{
	RootReturnTag = NAME_None;
	if (ANCPlayerController* PC = Owner.Get())
	{
		PC->ReturnToMainMenu();	// keeps the session and calls OpenRoot(true)
	}
	else
	{
		OpenRoot(true);
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnQuit()
{
	ShowDialog(EDialog::QuitConfirm);
	return FReply::Handled();
}

FReply SNCFrontEnd::OnQuitConfirmed()
{
	if (ANCPlayerController* PC = Owner.Get())
	{
		PC->QuitToDesktop();
	}
	return FReply::Handled();
}

FReply SNCFrontEnd::OnDialogCancel()
{
	CloseDialog();
	return FReply::Handled();
}

FReply SNCFrontEnd::OnSelectTab(ETab NewTab)
{
	PendingTab.Reset();
	if (NewTab != Tab)
	{
		Tab = NewTab;
		RebuildTab();
	}
	return FReply::Handled();
}

void SNCFrontEnd::CycleTab(int32 Direction)
{
	bool bOnTabs = false;
	for (const TWeakPtr<SWidget>& Button : TabButtons)
	{
		bOnTabs |= HasFocus(Button);
	}
	PendingTab.Reset();
	Tab = static_cast<ETab>((static_cast<int32>(Tab) + Direction + 3) % 3);
	RebuildTab();
	RequestFocus(bOnTabs && TabButtons.IsValidIndex(static_cast<int32>(Tab)) ? TabButtons[static_cast<int32>(Tab)].Pin() : TabFirstFocus.Pin());
}

FReply SNCFrontEnd::OnApply()
{
	UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
	if (!Settings)
	{
		SetStatus(LOCTEXT("ApplyFailed", "无法读取设置，未作更改。"), Coral());
		return FReply::Handled();
	}
	if (!IsDraftDirty())
	{
		SetStatus(LOCTEXT("NothingToApply", "没有需要应用的更改。"), Mist());
		return FReply::Handled();
	}

	const bool bDisplayChanged = !Draft.DisplayModeEquals(Original);
	PreviousWindowMode = static_cast<int32>(Settings->GetFullscreenMode());
	PreviousResolution = Settings->GetScreenResolution();

	Draft.Resolution = EffectiveResolution(Draft);
	Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(Draft.WindowMode));
	Settings->SetScreenResolution(Draft.Resolution);
	if (Draft.Quality >= 0 && Draft.Quality != Original.Quality)
	{
		Settings->SetOverallScalabilityLevel(Draft.Quality);
	}
	Settings->SetFrameRateLimit(Draft.FrameRateLimit);
	Settings->SetVSyncEnabled(Draft.bVSync);
	Settings->SetStartInBorderless(Draft.bStartInBorderless);
	Settings->SetMouseSensitivity(Draft.MouseSensitivity);
	Settings->SetGamepadSensitivity(Draft.GamepadSensitivity);
	Settings->SetInvertMouseY(Draft.bInvertMouseY);
	Settings->SetInvertGamepadY(Draft.bInvertGamepadY);
	Settings->SetGamepadDeadZone(Draft.GamepadDeadZone);
	Settings->SetMasterVolume(Draft.MasterVolume);
	Settings->SetMusicVolume(Draft.MusicVolume);
	Settings->SetEffectsVolume(Draft.EffectsVolume);

	// Applies input/audio too and saves. LastConfirmed* still hold the old display mode until Keep, so a
	// crash during the countdown lets startup fall back to the last mode the player confirmed.
	Settings->ApplySettings(false);

	if (bDisplayChanged)
	{
		Original = FNCSettingsDraft::Capture(*Settings);
		Original.WindowMode = PreviousWindowMode;
		Original.Resolution = PreviousResolution;
		Draft = FNCSettingsDraft::Capture(*Settings);
		VideoDeadline = FPlatformTime::Seconds() + VideoConfirmSeconds;
		ShowDialog(EDialog::VideoConfirm);
		return FReply::Handled();
	}

	Settings->AcceptVideoMode();
	Settings->SaveSettings();
	Original = Draft = FNCSettingsDraft::Capture(*Settings);
	SetStatus(LOCTEXT("Applied", "设置已应用并保存。"), Teal());
	return FReply::Handled();
}

FReply SNCFrontEnd::OnDefaults()
{
	switch (Tab)
	{
	case ETab::Video:
	{
		const UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
		Draft.ResetVideo(Settings ? Settings->GetDesktopResolution() : FIntPoint::ZeroValue);
		RefreshResolutionOptions();
		break;
	}
	case ETab::Controls: Draft.ResetControls(); break;
	case ETab::Audio: Draft.ResetAudio(); break;
	}
	SetStatus(IsDraftDirty()
		? LOCTEXT("DefaultsSet", "本页已恢复默认，按“应用”保存。")
		: LOCTEXT("DefaultsSame", "本页已经是默认值。"), Amber());
	return FReply::Handled();
}

FReply SNCFrontEnd::OnSettingsBack()
{
	Draft = Original;	// cancel: nothing was written
	LeaveSubPage();
	return FReply::Handled();
}

FReply SNCFrontEnd::OnKeepVideo()
{
	if (UNCGameUserSettings* Settings = UNCGameUserSettings::Get())
	{
		Settings->AcceptVideoMode();
		Settings->SaveSettings();
		Original = Draft = FNCSettingsDraft::Capture(*Settings);
	}
	CloseDialog();
	SetStatus(LOCTEXT("VideoKept", "显示设置已保留并保存。"), Teal());
	return FReply::Handled();
}

FReply SNCFrontEnd::OnRevertVideo()
{
	RevertDisplay();
	CloseDialog();
	SetStatus(LOCTEXT("VideoReverted", "显示已恢复，其他更改仍保留。"), Amber());
	return FReply::Handled();
}

void SNCFrontEnd::RevertDisplay()
{
	if (Dialog != EDialog::VideoConfirm)
	{
		return;
	}
	if (UNCGameUserSettings* Settings = UNCGameUserSettings::Get())
	{
		Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(PreviousWindowMode));
		Settings->SetScreenResolution(PreviousResolution);
		Settings->ApplyResolutionSettings(false);
		Settings->AcceptVideoMode();
		Settings->SaveSettings();
		Original = Draft = FNCSettingsDraft::Capture(*Settings);
	}
	RefreshResolutionOptions();
	VideoDeadline = 0.0;
}

void SNCFrontEnd::SetStatus(const FText& Text, const FLinearColor& Color, double Seconds)
{
	StatusText = Text;
	StatusColor = Color;
	StatusUntil = FPlatformTime::Seconds() + Seconds;
}

// ---------------------------------------------------------------------------------------------------------------------
// Settings helpers

void SNCFrontEnd::RefreshResolutionOptions()
{
	ResolutionOptions = { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080) };
	if (const UNCGameUserSettings* Settings = UNCGameUserSettings::Get())
	{
		const FIntPoint Desktop = Settings->GetDesktopResolution();
		if (Desktop.X > 0 && Desktop.Y > 0)
		{
			ResolutionOptions.RemoveAll([Desktop](const FIntPoint& R) { return R.X > Desktop.X || R.Y > Desktop.Y; });
			ResolutionOptions.AddUnique(Desktop);
		}
	}
	if (Draft.Resolution.X > 0 && Draft.Resolution.Y > 0)
	{
		ResolutionOptions.AddUnique(Draft.Resolution);	// keep whatever the player is using now
	}
	ResolutionOptions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y != B.X * B.Y ? A.X * A.Y < B.X * B.Y : A.X < B.X; });
}

FIntPoint SNCFrontEnd::EffectiveResolution(const FNCSettingsDraft& D) const
{
	if (D.WindowMode == EWindowMode::WindowedFullscreen)
	{
		if (const UNCGameUserSettings* Settings = UNCGameUserSettings::Get())
		{
			const FIntPoint Desktop = Settings->GetDesktopResolution();
			if (Desktop.X > 0 && Desktop.Y > 0)
			{
				return Desktop;
			}
		}
	}
	return D.Resolution;
}

bool SNCFrontEnd::IsDraftDirty() const
{
	return !Draft.Equals(Original);
}

FText SNCFrontEnd::GetFocusedHint() const
{
	for (const FRowHint& Hint : RowHints)
	{
		if (HasFocus(Hint.Widget))
		{
			return Hint.Hint;
		}
	}
	switch (Tab)
	{
	case ETab::Controls: return LOCTEXT("ControlsOverview", "默认情况下，右摇杆向上推 = 镜头向上看。鼠标与手柄的反转互相独立。");
	case ETab::Audio: return LOCTEXT("AudioOverview", "音量在按下“应用”后生效。");
	default: return LOCTEXT("VideoOverview", "显示模式与分辨率的更改需要在 15 秒内确认。");
	}
}

FText SNCFrontEnd::GetStatusText() const
{
	if (FPlatformTime::Seconds() < StatusUntil)
	{
		return StatusText;
	}
	if (IsDraftDirty())
	{
		return LOCTEXT("Unsaved", "有未应用的更改 · 按“应用”保存");
	}
	return FText::GetEmpty();
}

FSlateColor SNCFrontEnd::GetStatusColor() const
{
	return FSlateColor(FPlatformTime::Seconds() < StatusUntil ? StatusColor : Amber());
}

#undef LOCTEXT_NAMESPACE
