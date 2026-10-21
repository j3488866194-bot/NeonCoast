#include "NCHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/DateTime.h"
#include "NCCar.h"
#include "NCHero.h"

namespace NCHUDLayout
{
	constexpr float RefHeight = 1080.f;
	constexpr float Margin = 40.f;
	constexpr float PanelW = 340.f;
	constexpr float MapSize = 190.f;
	// Minimap world window (cm): city floor plus the beach strip.
	constexpr float MapMinX = -25000.f;
	constexpr float MapMaxX = 33000.f;
	constexpr float MapHalfY = 29000.f;
}

void ANCHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (LastPawn.Get() != Pawn)
	{
		LastPawn = Pawn;
		PawnChangedTime = Now;
	}

	using namespace NCHUDLayout;
	UIScale = FMath::Max(0.5f, Canvas->ClipY / RefHeight);
	const float S = UIScale;
	const float SW = Canvas->ClipX;
	const float SH = Canvas->ClipY;
	const float M = Margin * S;

	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Large = GEngine->GetLargeFont();

	const ANCCar* Car = Cast<ANCCar>(Pawn);
	const ANCHero* Hero = Cast<ANCHero>(Pawn);
	const FVector Location = Pawn->GetActorLocation();

	// --- Bottom-left: place, local time, hero health ---------------------------------------
	const float PW = PanelW * S;
	const float PH = 112.f * S;
	const float PX = M;
	const float PY = SH - M - PH;
	DrawRect(Ink(0.55f), PX, PY, PW, PH);
	DrawRect(Seafoam(), PX, PY, 3.f * S, PH);

	const FDateTime Clock = FDateTime::Now();
	DrawLabel(DistrictAt(Location), PX + 18.f * S, PY + 12.f * S, Medium, 1.0f * S, Cream());
	DrawLabel(FString::Printf(TEXT("LOCAL  %02d:%02d"), Clock.GetHour(), Clock.GetMinute()), PX + PW - 16.f * S, PY + 16.f * S, Small, 0.95f * S, Muted(), 1.f);

	float Health = -1.f;
	bool bRecovering = false;
	if (Car)
	{
		Health = Car->GetDriverHealth();
	}
	else if (Hero)
	{
		Health = Hero->GetHealth();
		bRecovering = Hero->IsRecovering();
	}
	if (Health >= 0.f)
	{
		const float Fraction = FMath::Clamp(Health / 100.f, 0.f, 1.f);
		const FLinearColor Fill = Fraction < 0.3f ? Coral() : Seafoam();
		DrawLabel(TEXT("HEALTH"), PX + 18.f * S, PY + 52.f * S, Small, 0.9f * S, Muted());
		DrawLabel(FString::Printf(TEXT("%d"), FMath::RoundToInt(Health)), PX + PW - 16.f * S, PY + 52.f * S, Small, 0.9f * S, Cream(), 1.f);
		DrawBar(PX + 18.f * S, PY + 76.f * S, PW - 34.f * S, 6.f * S, Fraction, Fill);
		if (bRecovering)
		{
			DrawLabel(TEXT("RECOVERING"), PX + 18.f * S, PY + 88.f * S, Small, 0.85f * S, Coral());
		}
	}

	// --- Minimap above the place panel --------------------------------------------------------
	if (bShowMinimap)
	{
		const float MS = MapSize * S;
		DrawMinimap(PX, PY - MS - 12.f * S, MS, Location, Pawn->GetActorRotation().Yaw);
	}

	// --- Bottom-right: vehicle readout ----------------------------------------------------------
	if (Car)
	{
		const float Speed = FMath::Max(0.f, Car->GetSpeedKmh());
		const float Condition = FMath::Clamp(Car->GetCondition(), 0.f, 100.f);
		const float RX = SW - M;
		const float RW = 260.f * S;
		const float RH = 128.f * S;
		const float RY = SH - M - RH;
		DrawRect(Ink(0.55f), RX - RW, RY, RW, RH);
		DrawRect(Coral(), RX - 3.f * S, RY, 3.f * S, RH);

		DrawLabel(FString::Printf(TEXT("%d"), FMath::RoundToInt(Speed)), RX - 70.f * S, RY + 8.f * S, Large, 2.2f * S, Cream(), 1.f);
		DrawLabel(TEXT("KM/H"), RX - 18.f * S, RY + 42.f * S, Small, 0.9f * S, Muted(), 1.f);

		const float CFrac = Condition / 100.f;
		DrawLabel(TEXT("CONDITION"), RX - RW + 18.f * S, RY + 84.f * S, Small, 0.85f * S, Muted());
		DrawLabel(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Condition)), RX - 18.f * S, RY + 84.f * S, Small, 0.85f * S, Cream(), 1.f);
		DrawBar(RX - RW + 18.f * S, RY + 106.f * S, RW - 36.f * S, 6.f * S, CFrac, CFrac < 0.3f ? Coral() : Cream());
	}

	// --- Top-right: controls (full list after possession, then a compact reminder) -----------
	DrawControls(Car != nullptr, Now - PawnChangedTime < ControlsHintSeconds);
}

void ANCHUD::DrawLabel(const FString& Text, float X, float Y, UFont* Font, float Scale, const FLinearColor& Color, float AlignX)
{
	if (!Canvas || !Font)
	{
		return;
	}
	if (AlignX != 0.f)
	{
		float TW = 0.f;
		float TH = 0.f;
		Canvas->TextSize(Font, Text, TW, TH, Scale, Scale);
		X -= TW * AlignX;
	}
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	Canvas->DrawItem(Item);
}

void ANCHUD::DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill)
{
	FLinearColor Track = Cream();
	Track.A = 0.18f;
	DrawRect(Track, X, Y, W, H);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H);
}

void ANCHUD::DrawMinimap(float X, float Y, float Size, const FVector& Location, float Yaw)
{
	using namespace NCHUDLayout;
	const float K = Size / (MapMaxX - MapMinX);
	auto ToScreen = [&](float WX, float WY) { return FVector2D(X + (WX - MapMinX) * K, Y + (MapHalfY - WY) * K); };

	DrawRect(Ink(0.55f), X, Y, Size, Size);

	// Beach and sea on the east edge.
	const FVector2D Beach = ToScreen(25000.f, MapHalfY);
	const FVector2D Sea = ToScreen(31000.f, MapHalfY);
	DrawRect(FLinearColor(0.78f, 0.64f, 0.44f, 0.35f), Beach.X, Y, Sea.X - Beach.X, Size);
	FLinearColor SeaColor = Seafoam();
	SeaColor.A = 0.22f;
	DrawRect(SeaColor, Sea.X, Y, X + Size - Sea.X, Size);

	// Street grid (centres every 7200 from -21600 to 21600, 1600 wide).
	FLinearColor Road = Muted();
	Road.A = 0.45f;
	const float RoadPx = FMath::Max(1.f, 1600.f * K);
	for (int32 i = 0; i < 7; ++i)
	{
		const float C = -21600.f + 7200.f * i;
		const FVector2D V = ToScreen(C, 25000.f);
		const FVector2D H = ToScreen(-25000.f, C);
		DrawRect(i == 6 ? Cream() : Road, V.X - RoadPx * 0.5f, ToScreen(0.f, 25000.f).Y, RoadPx, 50000.f * K);
		DrawRect(Road, ToScreen(-25000.f, 0.f).X, H.Y - RoadPx * 0.5f, 50000.f * K, RoadPx);
	}

	// Player marker with heading.
	const FVector2D P = ToScreen(FMath::Clamp(static_cast<float>(Location.X), MapMinX, MapMaxX), FMath::Clamp(static_cast<float>(Location.Y), -MapHalfY, MapHalfY));
	const float Rad = FMath::DegreesToRadians(Yaw);
	const FVector2D Dir(FMath::Cos(Rad), -FMath::Sin(Rad));
	const float Len = 14.f * UIScale;
	DrawLine(P.X, P.Y, P.X + Dir.X * Len, P.Y + Dir.Y * Len, Coral(), 2.f * UIScale);
	const float Dot = 6.f * UIScale;
	DrawRect(Coral(), P.X - Dot * 0.5f, P.Y - Dot * 0.5f, Dot, Dot);

	DrawLabel(TEXT("N"), X + Size * 0.5f, Y + 4.f * UIScale, GEngine->GetSmallFont(), 0.8f * UIScale, Muted(), 0.5f);
}

void ANCHUD::DrawControls(bool bInCar, bool bFull)
{
	using namespace NCHUDLayout;
	const float S = UIScale;
	const float RX = Canvas->ClipX - Margin * S;
	float Y = Margin * S;
	UFont* Small = GEngine->GetSmallFont();

	struct FHint { const TCHAR* Action; const TCHAR* Keys; };
	static const FHint CarHints[] =
	{
		{ TEXT("Drive"), TEXT("WASD  /  Left Stick") },
		{ TEXT("Look"), TEXT("Mouse  /  Right Stick") },
		{ TEXT("Handbrake"), TEXT("Space  /  B") },
		{ TEXT("Boost"), TEXT("Shift  /  A") },
		{ TEXT("Exit  (bail out while moving)"), TEXT("F  /  Y") },
		{ TEXT("Recover"), TEXT("T  /  D-Pad Down") },
		{ TEXT("Pause"), TEXT("Esc  /  Start") },
	};
	static const FHint FootHints[] =
	{
		{ TEXT("Move"), TEXT("WASD  /  Left Stick") },
		{ TEXT("Look"), TEXT("Mouse  /  Right Stick") },
		{ TEXT("Jump"), TEXT("Space  /  A") },
		{ TEXT("Sprint"), TEXT("Shift  /  Left Stick") },
		{ TEXT("Enter vehicle"), TEXT("F  /  Y") },
		{ TEXT("Greet"), TEXT("E  /  X") },
		{ TEXT("Recover"), TEXT("T  /  D-Pad Down") },
		{ TEXT("Pause"), TEXT("Esc  /  Start") },
	};

	if (!bFull)
	{
		const FString Compact = bInCar
			? TEXT("F / Y  exit     T / D-Pad Down  recover     Esc / Start  pause")
			: TEXT("F / Y  enter vehicle     E / X  greet     Esc / Start  pause");
		DrawLabel(Compact, RX, Y, Small, 0.85f * S, Muted(), 1.f);
		return;
	}

	const FHint* Hints = bInCar ? CarHints : FootHints;
	const int32 Count = bInCar ? UE_ARRAY_COUNT(CarHints) : UE_ARRAY_COUNT(FootHints);
	const float RowH = 22.f * S;
	const float BoxW = 360.f * S;
	const float BoxH = (Count + 1) * RowH + 20.f * S;
	DrawRect(Ink(0.45f), RX - BoxW, Y, BoxW, BoxH);
	DrawRect(Cream(), RX - BoxW, Y, BoxW, 2.f * S);

	Y += 10.f * S;
	DrawLabel(bInCar ? TEXT("DRIVING") : TEXT("ON FOOT"), RX - BoxW + 16.f * S, Y, Small, 0.9f * S, Seafoam());
	Y += RowH;
	for (int32 i = 0; i < Count; ++i)
	{
		DrawLabel(Hints[i].Action, RX - BoxW + 16.f * S, Y, Small, 0.85f * S, Cream());
		DrawLabel(Hints[i].Keys, RX - 16.f * S, Y, Small, 0.85f * S, Muted(), 1.f);
		Y += RowH;
	}
}

FString ANCHUD::DistrictAt(const FVector& L)
{
	// Same districts as the original prototype, mapped to Unreal coordinates (+X east, +Y north).
	if (L.X > 22400.f) { return TEXT("OCEAN PROMENADE"); }
	if (L.X > 16000.f) { return TEXT("OCEAN DRIVE"); }
	if (L.X < -6500.f && L.Y > -3000.f) { return TEXT("MERIDIAN DOWNTOWN"); }
	if (L.Y < -13500.f) { return TEXT("SOUTH MARINA"); }
	if (L.Y > 13500.f) { return TEXT("NORTH KEY"); }
	if (L.X < -6500.f) { return TEXT("PALM GROVE"); }
	return TEXT("ART DECO DISTRICT");
}
