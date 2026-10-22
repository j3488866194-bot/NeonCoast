#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/NavigationReply.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ANCPlayerController;
class UNCGameUserSettings;
class SBox;
class SWidget;

/**
 * Editable copy of every option on the settings page.
 * Nothing reaches UNCGameUserSettings (or disk) until the player presses Apply.
 */
struct FNCSettingsDraft
{
	int32 WindowMode = 1;			// EWindowMode::Type: 0 fullscreen, 1 borderless, 2 windowed
	FIntPoint Resolution = FIntPoint(1920, 1080);
	int32 Quality = 2;				// 0 low .. 3 epic; -1 = custom scalability, left untouched on apply
	float FrameRateLimit = 60.f;	// 0 = unlimited
	bool bVSync = true;
	bool bStartInBorderless = true;	// read at the next launch only, so never part of the display trial

	float MouseSensitivity = 1.f;
	float GamepadSensitivity = 1.f;
	bool bInvertMouseY = false;
	bool bInvertGamepadY = false;
	float GamepadDeadZone = .15f;

	float MasterVolume = 1.f;
	float MusicVolume = .6f;
	float EffectsVolume = 1.f;

	static FNCSettingsDraft Capture(const UNCGameUserSettings& Settings);
	void ResetVideo(const FIntPoint& DesktopResolution);
	void ResetControls();
	void ResetAudio();

	bool DisplayModeEquals(const FNCSettingsDraft& Other) const;
	bool VideoEquals(const FNCSettingsDraft& Other) const;
	bool ControlsEquals(const FNCSettingsDraft& Other) const;
	bool AudioEquals(const FNCSettingsDraft& Other) const;
	bool Equals(const FNCSettingsDraft& Other) const { return VideoEquals(Other) && ControlsEquals(Other) && AudioEquals(Other); }
};

/**
 * Neon Coast title / pause / settings / controls front end.
 * Pages: title, pause, settings, controls; modal dialogs: quit_confirm, video_confirm.
 * Owns its keyboard, mouse and gamepad handling so no input reaches the world while it is open.
 */
class NEONCOAST_API SNCFrontEnd : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNCFrontEnd)
		: _OwnerController(nullptr)
		, _TitleScreen(true)
		{}
		SLATE_ARGUMENT(ANCPlayerController*, OwnerController)
		SLATE_ARGUMENT(bool, TitleScreen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SNCFrontEnd() override;

	/** Shows the title (true) or pause (false) root page and focuses its first action. */
	void OpenRoot(bool bTitleScreen);
	/** Opens settings with a fresh draft captured from the saved settings. */
	void OpenSettingsPage();
	/** One step back (Esc / B). Returns true when the front end consumed it. */
	bool HandleBack();
	/** Puts keyboard/gamepad focus on the current page's (or dialog's) default action. */
	void FocusDefault();
	/** title, pause, settings, controls, quit_confirm or video_confirm. */
	FString GetPageId() const;
	bool IsAwaitingVideoConfirmation() const;

	// SWidget
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;

private:
	enum class EPage : uint8 { Title, Pause, Settings, Controls };
	enum class EDialog : uint8 { None, QuitConfirm, VideoConfirm };
	enum class ETab : uint8 { Video, Controls, Audio };
	enum class EButtonKind : uint8 { Menu, Primary, Secondary, Tab };

	/** One line of the explicit navigation grid (left/right inside, up/down between lines). */
	struct FNavRow
	{
		TArray<TWeakPtr<SWidget>> Items;
		int32 Entry = 0;		// column used when arriving from a line with a different width
		bool bTabRow = false;	// arriving lands on the active tab
	};

	struct FRowHint
	{
		TWeakPtr<SWidget> Widget;
		FText Hint;
		bool bBody = false;		// belongs to the current settings tab body
	};

	// Pages and dialogs.
	void ShowPage(EPage NewPage, FName FocusTag = NAME_None);
	void ShowDialog(EDialog NewDialog);
	void CloseDialog();
	void LeaveSubPage();
	void OpenSettingsAt(ETab InitialTab);
	void RebuildTab();
	void RefreshFooter();
	TSharedRef<SWidget> BuildRootPage();
	TSharedRef<SWidget> BuildSettingsPage();
	TSharedRef<SWidget> BuildControlsPage();
	TSharedRef<SWidget> BuildTabBody();
	TSharedRef<SWidget> BuildVideoTab();
	TSharedRef<SWidget> BuildControlsTab();
	TSharedRef<SWidget> BuildAudioTab();
	TSharedRef<SWidget> BuildQuitDialog();
	TSharedRef<SWidget> BuildVideoDialog();
	TSharedRef<SWidget> BuildDialogCard(const FText& Eyebrow, const FText& Title, const FLinearColor& Accent, TSharedRef<SWidget> Body, TSharedRef<SWidget> Buttons);
	TSharedRef<SWidget> BuildPanel(const FText& Eyebrow, const FText& Title, TSharedRef<SWidget> Body, float Width);

	// Widget factories.
	TSharedRef<SWidget> MakeButton(FName WidgetTag, const FText& Label, FOnClicked OnClicked, EButtonKind Kind, TSharedPtr<SWidget>* OutWidget = nullptr,
		TAttribute<bool> bSelected = false, TFunction<void()> OnNavigationFocus = nullptr);
	TSharedRef<SWidget> MakeChoiceRow(FName WidgetTag, const FText& Label, const FText& Hint, TFunction<FText()> GetValue, TFunction<void(int32)> Step,
		TFunction<bool()> IsDirty, TAttribute<bool> bEnabled = true);
	TSharedRef<SWidget> MakeToggleRow(FName WidgetTag, const FText& Label, const FText& Hint, bool FNCSettingsDraft::* Field);
	TSharedRef<SWidget> MakeSliderRow(FName WidgetTag, const FText& Label, const FText& Hint, float FNCSettingsDraft::* Field, float Min, float Max, float Step);
	TSharedRef<SWidget> MakeAudioPreviewRow();
	TSharedRef<SWidget> MakeRowFrame(const FText& Label, const FText& Hint, TSharedRef<SWidget> Control, TSharedRef<TWeakPtr<SWidget>> Target,
		TFunction<bool()> IsDirty, TFunction<void()> OnRowClick = nullptr);
	TSharedRef<SWidget> MakeKeyCap(const FText& Key, bool bGamepad);
	TSharedRef<SWidget> MakeHint(const FText& Keyboard, const FText& Gamepad, const FText& Action);
	TSharedRef<SWidget> MakeHelpLine(const FText& Action, const FText& Keyboard, const FText& Gamepad);
	TSharedRef<SWidget> MakeHelpSection(const FText& Title, const TArray<TSharedRef<SWidget>>& Lines);

	// Actions.
	FReply OnPrimary();
	FReply OnOpenSettings();
	FReply OnOpenControls();
	FReply OnOpenControlSettings();
	FReply OnControlsBack();
	FReply OnRestartOnboarding();
	FReply OnSkipOnboarding();
	FReply OnAudioPreview();
	FReply OnReturnToTitle();
	FReply OnQuit();
	FReply OnQuitConfirmed();
	FReply OnDialogCancel();
	FReply OnSelectTab(ETab NewTab);
	FReply OnApply();
	FReply OnDefaults();
	FReply OnSettingsBack();
	FReply OnKeepVideo();
	FReply OnRevertVideo();
	void CycleTab(int32 Direction);
	void RevertDisplay();
	void SetStatus(const FText& Text, const FLinearColor& Color, double Seconds = 4.0);

	// Helpers.
	void RefreshResolutionOptions();
	FIntPoint EffectiveResolution(const FNCSettingsDraft& D) const;
	bool IsDraftDirty() const;
	FText GetFocusedHint() const;
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;
	void RequestFocus(TSharedPtr<SWidget> Widget);
	void RestoreFocus();
	void GatherNavRows(TArray<FNavRow>& Out) const;

	TWeakObjectPtr<ANCPlayerController> Owner;

	EPage Page = EPage::Title;
	EDialog Dialog = EDialog::None;
	ETab Tab = ETab::Video;
	TOptional<ETab> PendingTab;
	bool bTitle = true;
	bool bWrapNav = true;

	TSharedPtr<SBox> PageHost;
	TSharedPtr<SBox> FooterHost;
	TSharedPtr<SBox> DialogHost;
	TSharedPtr<SBox> TabHost;

	// Navigation and focus.
	TArray<FNavRow> HeadNav;
	TArray<FNavRow> BodyNav;
	TArray<FNavRow> TailNav;
	TArray<FNavRow> DialogNav;
	TArray<TWeakPtr<SWidget>> TabButtons;
	TArray<FRowHint> RowHints;
	TMap<FName, TWeakPtr<SWidget>> Tagged;
	TWeakPtr<SWidget> PageDefault;
	TWeakPtr<SWidget> DialogDefault;
	TWeakPtr<SWidget> TabFirstFocus;
	TWeakPtr<SWidget> PendingFocus;			// re-applied on tick until the new widgets accept it
	int32 PendingFocusTries = 0;
	TWeakPtr<SWidget> LastFocused;
	TWeakPtr<SWidget> DialogReturnFocus;
	FName RootReturnTag;					// root button that opened the current sub page

	// Settings.
	FNCSettingsDraft Draft;
	FNCSettingsDraft Original;
	TArray<FIntPoint> ResolutionOptions;
	double VideoDeadline = 0.0;
	int32 PreviousWindowMode = 1;
	FIntPoint PreviousResolution = FIntPoint(1920, 1080);

	FText StatusText;
	FLinearColor StatusColor = FLinearColor::White;
	double StatusUntil = 0.0;

	// Styles and brushes are owned here so widgets never point at freed style data.
	FButtonStyle ButtonStyle;
	FCheckBoxStyle ToggleStyle;
	FSliderStyle SliderStyle;
	FSlateNoResource NoBrush;
	FSlateColorBrush WhiteBrush = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush PanelBrush = FSlateRoundedBoxBrush(FLinearColor::White, 6.f);
	FSlateRoundedBoxBrush SoftBrush = FSlateRoundedBoxBrush(FLinearColor::White, 3.f);
	FSlateRoundedBoxBrush OutlineBrush = FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.f, FLinearColor::White, 1.5f);
	FSlateRoundedBoxBrush PillBrush = FSlateRoundedBoxBrush(FLinearColor::White, 14.f);
	FSlateRoundedBoxBrush KnobBrush = FSlateRoundedBoxBrush(FLinearColor::White, 9.f, FVector2f(18.f, 18.f));
	FSlateRoundedBoxBrush BarBrush = FSlateRoundedBoxBrush(FLinearColor::White, 2.f);
	FSlateRoundedBoxBrush ThumbBrush = FSlateRoundedBoxBrush(FLinearColor::White, 10.f, FVector2f(20.f, 20.f));
};
