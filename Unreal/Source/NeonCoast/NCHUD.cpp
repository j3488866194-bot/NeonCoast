#include "NCHUD.h"

#include "CanvasItem.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Scalability.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCCityVisual.h"
#include "NCGameplaySubsystem.h"
#include "NCPlayerController.h"

namespace NCHUDLayout
{
	constexpr float RefHeight = 1080.f;
	constexpr float Margin = 36.f;
	constexpr float MiniSize = 232.f;
	constexpr float InfoH = 60.f;
	// Full-map world window (cm): city floor plus beach and a strip of sea. Square on purpose.
	constexpr float MapMinX = -25000.f;
	constexpr float MapMaxX = 33000.f;
	constexpr float MapHalfY = 29000.f;
	constexpr float RoadHalfW = ANCCityVisual::RoadWidth * 0.5f;
	constexpr float BeachX = ANCCityVisual::FloorHalfExtent;	// sand starts at the floor edge
	constexpr float SeaX = ANCCityVisual::BeachMaxX;

	static float ScaleFor(float ClipY) { return FMath::Clamp(ClipY / RefHeight, 0.8f, 2.5f); }

	static float NearestRoad(float V)
	{
		const int32 I = FMath::Clamp(FMath::RoundToInt((V - ANCCityVisual::GridFirstRoad) / ANCCityVisual::GridSpacing), 0, ANCCityVisual::GridRoadCount - 1);
		return ANCCityVisual::GridFirstRoad + ANCCityVisual::GridSpacing * I;
	}

	static FLinearColor WithAlpha(FLinearColor C, float A) { C.A *= A; return C; }

	/** Liang-Barsky clip of segment AB to an axis-aligned box. */
	static bool ClipSegment(FVector2D& A, FVector2D& B, const FVector2D& Min, const FVector2D& Max)
	{
		const FVector2D D = B - A;
		const double P[4] = { -D.X, D.X, -D.Y, D.Y };
		const double Q[4] = { A.X - Min.X, Max.X - A.X, A.Y - Min.Y, Max.Y - A.Y };
		double T0 = 0.0, T1 = 1.0;
		for (int32 i = 0; i < 4; ++i)
		{
			if (FMath::IsNearlyZero(P[i]))
			{
				if (Q[i] < 0.0) { return false; }
				continue;
			}
			const double R = Q[i] / P[i];
			if (P[i] < 0.0) { if (R > T1) { return false; } T0 = FMath::Max(T0, R); }
			else { if (R < T0) { return false; } T1 = FMath::Min(T1, R); }
		}
		const FVector2D A0 = A;
		A = A0 + D * T0;
		B = A0 + D * T1;
		return true;
	}

	// Map palette (sRGB): night-navy land, pale streets, the coast road picked out in cream.
	static FLinearColor MapSea() { return FLinearColor(FColor(16, 52, 64)); }
	static FLinearColor MapLand() { return FLinearColor(FColor(24, 30, 38)); }
	static FLinearColor MapBlock() { return FLinearColor(FColor(38, 46, 56)); }
	static FLinearColor MapBuilding() { return FLinearColor(FColor(52, 62, 74)); }
	static FLinearColor MapRoad() { return FLinearColor(FColor(118, 128, 134)); }
	static FLinearColor MapCoastRoad() { return FLinearColor(FColor(232, 214, 180)); }
	static FLinearColor MapSand() { return FLinearColor(FColor(160, 136, 98)); }
	static FLinearColor MapPromenade() { return FLinearColor(FColor(120, 82, 72)); }
}

void ANCHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}

	using namespace NCHUDLayout;
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayerController();
	const ANCPlayerController* NCPC = Cast<ANCPlayerController>(PC);
	if (NCPC && NCPC->IsFrontEndOpen()) return;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const float SW = Canvas->ClipX;
	const float SH = Canvas->ClipY;
	CanvasSize = FVector2D(SW, SH);
	UIScale = ScaleFor(SH);
	const float S = UIScale;
	const float M = Margin * S;
	const float Now = World ? static_cast<float>(World->GetRealTimeSeconds()) : 0.f;
	const UNCGameplaySubsystem* Game = World ? World->GetSubsystem<UNCGameplaySubsystem>() : nullptr;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Large = GEngine->GetLargeFont();

	if (!City.IsValid() && World && Now >= NextCitySearch)
	{
		NextCitySearch = Now + 2.f;
		for (TActorIterator<ANCCityVisual> It(World); It; ++It)
		{
			City = *It;
			break;
		}
	}

	// Overlays own the screen.
	if (NCPC && NCPC->IsPhotoMode())
	{
		DrawLabel(TEXT("F6  Return"), SW - M, SH - M - 14.f * S, Small, 0.85f * S, WithAlpha(Muted(), 0.55f), 1.f);
		return;
	}
	if (NCPC && NCPC->IsMapOpen())
	{
		DrawFullMap(Game, Pawn);
		DrawStatus(Game, Now, M);
		return;
	}
	if (NCPC && NCPC->IsHelpOpen())
	{
		DrawHelp();
		return;
	}
	if (PC && PC->IsPaused())
	{
		DrawPause(Game);
		return;
	}
	if (!Pawn)
	{
		return;
	}

	if (LastPawn.Get() != Pawn)
	{
		LastPawn = Pawn;
		PawnChangedTime = Now;
	}

	const ANCCar* Car = Cast<ANCCar>(Pawn);
	const ANCHero* Hero = Cast<ANCHero>(Pawn);
	const FVector Location = Pawn->GetActorLocation();

	// Navigation: a user waypoint takes the route; otherwise the live objective.
	FVector WaypointLoc = FVector::ZeroVector;
	FVector ObjectiveLoc = FVector::ZeroVector;
	const bool bWaypoint = NCPC && NCPC->HasWaypoint();
	const bool bObjective = Game && Game->HasObjective();
	if (bWaypoint) { WaypointLoc = NCPC->GetWaypoint(); }
	if (bObjective) { ObjectiveLoc = Game->GetObjectiveLocation(); }
	TArray<FVector2D> ObjectiveRoute;
	TArray<FVector2D> WaypointRoute;
	if (bObjective) { BuildRoute(FVector2D(Location), FVector2D(ObjectiveLoc), ObjectiveRoute); }
	if (bWaypoint) { BuildRoute(FVector2D(Location), FVector2D(WaypointLoc), WaypointRoute); }

	// --- Bottom-left: minimap and info strip --------------------------------------------------
	const float MS = MiniSize * S;
	const float InfoY = SH - M - InfoH * S;
	if (bShowMinimap)
	{
		const float Speed = Car ? Car->GetSpeedKmh() : 0.f;
		const float TargetSpan = FMath::Lerp(16000.f, 30000.f, FMath::Clamp(Speed / 140.f, 0.f, 1.f));
		MinimapSpan = FMath::FInterpTo(MinimapSpan, TargetSpan, World ? World->GetDeltaSeconds() : 0.016f, 1.5f);
		DrawMinimap(Game, Pawn, bWaypoint ? WaypointRoute : ObjectiveRoute, bWaypoint ? Seafoam() : Amber(),
			bObjective ? &ObjectiveLoc : nullptr, bWaypoint ? &WaypointLoc : nullptr, M, InfoY - MS, MS);
	}

	DrawRect(Ink(0.62f), M, InfoY, MS, InfoH * S);
	FString Phase;
	const FString Clock = ClockText(&Phase);
	DrawLabel(DistrictAt(Location), M + 12.f * S, InfoY + 8.f * S, Small, 0.95f * S, Cream());
	if (!Clock.IsEmpty())
	{
		DrawLabel(Clock, M + MS - 12.f * S, InfoY + 8.f * S, Small, 0.95f * S, Cream(), 1.f);
	}

	float Health = -1.f;
	bool bRecovering = false;
	if (Car) { Health = Car->GetDriverHealth(); }
	else if (Hero) { Health = Hero->GetHealth(); bRecovering = Hero->IsRecovering(); }
	if (Health >= 0.f)
	{
		const float Fraction = FMath::Clamp(Health / 100.f, 0.f, 1.f);
		const float BarW = MS * 0.58f;
		DrawBar(M + 12.f * S, InfoY + 40.f * S, BarW, 5.f * S, Fraction, Fraction < 0.3f ? Coral() : Seafoam());
		if (bRecovering)
		{
			DrawLabel(TEXT("RECOVERING"), M + MS - 12.f * S, InfoY + 33.f * S, Small, 0.8f * S, Coral(), 1.f);
		}
		else if (!Phase.IsEmpty())
		{
			DrawLabel(Phase, M + MS - 12.f * S, InfoY + 33.f * S, Small, 0.8f * S, Muted(), 1.f);
		}
	}

	// --- Top-left: objective -----------------------------------------------------------------
	if (Game)
	{
		const FString Objective = Game->GetObjectiveText();
		const bool bRace = Game->IsRaceActive();
		if (!Objective.IsEmpty() || bRace)
		{
			const float PW = 420.f * S;
			const TArray<FString> Lines = WrapText(Objective, Small, 1.05f * S, PW - 36.f * S, 3);
			const float LineH = 22.f * S;
			const float PH = 40.f * S + Lines.Num() * LineH + (bRace ? 34.f * S : 0.f) + 8.f * S;
			DrawRect(Ink(0.58f), M, M, PW, PH);
			DrawRect(bRace ? Rose() : Amber(), M, M, 3.f * S, PH);
			DrawLabel(bRace ? TEXT("STREET RACE") : TEXT("OBJECTIVE"), M + 18.f * S, M + 10.f * S, Small, 0.85f * S, bRace ? Rose() : Amber());
			if (bObjective)
			{
				DrawLabel(FormatDistance(RouteLength(ObjectiveRoute)), M + PW - 14.f * S, M + 10.f * S, Small, 0.85f * S, Muted(), 1.f);
			}
			float Y = M + 34.f * S;
			for (const FString& Line : Lines)
			{
				DrawLabel(Line, M + 18.f * S, Y, Small, 1.05f * S, Cream());
				Y += LineH;
			}
			if (bRace)
			{
				const int32 Count = Game->GetRaceCheckpointCount();
				const int32 Gate = FMath::Clamp(Game->GetRaceCheckpointIndex() + 1, 1, FMath::Max(1, Count));
				const float Left = Game->GetRaceTimeRemaining();
				DrawLabel(FString::Printf(TEXT("GATE %d / %d"), Gate, Count), M + 18.f * S, Y + 8.f * S, Small, 0.9f * S, Muted());
				DrawLabel(FString::Printf(TEXT("%d:%04.1f"), FMath::FloorToInt(Left / 60.f), FMath::Fmod(Left, 60.f)),
					M + PW - 14.f * S, Y + 2.f * S, Large, 1.0f * S, Left < 15.f ? Coral() : Cream(), 1.f);
			}
		}
	}

	// --- Top-right: cash and wanted ------------------------------------------------------------
	if (Game)
	{
		DrawLabel(FormatCash(Game->GetCash()), SW - M, M - 4.f * S, Large, 1.1f * S, Cream(), 1.f);
		const int32 Wanted = Game->GetWantedLevel();
		if (Wanted > 0)
		{
			const bool bSeen = Game->IsPoliceSeeingPlayer();
			const bool bFlash = bSeen && FMath::Fmod(Now, 0.6f) < 0.3f;
			const float R = 11.f * S;
			for (int32 i = 0; i < 3; ++i)
			{
				const float CX = SW - M - R - i * 30.f * S;
				const bool bOn = (2 - i) < Wanted;
				DrawStar(CX, M + 52.f * S, R, bOn ? (bFlash ? Cream() : Coral()) : WithAlpha(Muted(), 0.35f), bOn);
			}
			if (!bSeen)
			{
				const float BW = 84.f * S;
				DrawLabel(TEXT("EVADING"), SW - M - BW - 10.f * S, M + 68.f * S, Small, 0.8f * S, Muted(), 1.f);
				DrawBar(SW - M - BW, M + 75.f * S, BW, 4.f * S, Game->GetWantedSearchProgress(), Seafoam());
			}
		}
	}

	// --- Bottom-right: vehicle or weapon readout -------------------------------------------------
	const float RX = SW - M;
	const float ReadoutBottom = SH - M - 22.f * S;
	if (Car)
	{
		const float Speed = FMath::Max(0.f, Car->GetSpeedKmh());
		const float Condition = FMath::Clamp(Car->GetCondition(), 0.f, 100.f);
		const float RW = 250.f * S;
		const float RH = 112.f * S;
		const float RY = ReadoutBottom - RH;
		DrawRect(Ink(0.55f), RX - RW, RY, RW, RH);
		DrawRect(Coral(), RX - 3.f * S, RY, 3.f * S, RH);
		DrawLabel(FString::Printf(TEXT("%d"), FMath::RoundToInt(Speed)), RX - 70.f * S, RY + 6.f * S, Large, 2.0f * S, Cream(), 1.f);
		DrawLabel(TEXT("KM/H"), RX - 18.f * S, RY + 38.f * S, Small, 0.9f * S, Muted(), 1.f);
		const float CFrac = Condition / 100.f;
		DrawLabel(TEXT("CONDITION"), RX - RW + 16.f * S, RY + 74.f * S, Small, 0.85f * S, Muted());
		DrawLabel(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Condition)), RX - 18.f * S, RY + 74.f * S, Small, 0.85f * S, Cream(), 1.f);
		DrawBar(RX - RW + 16.f * S, RY + 96.f * S, RW - 34.f * S, 5.f * S, CFrac, CFrac < 0.3f ? Coral() : Cream());
	}
	else if (Hero)
	{
		const int32 Ammo = Hero->GetAmmo();
		const int32 Reserve = Hero->GetReserveAmmo();
		if (Ammo != LastAmmo)
		{
			LastAmmo = Ammo;
			AmmoChangedTime = Now;
		}
		const bool bReloading = Hero->IsReloading();
		if (Hero->IsAiming() || bReloading || Ammo <= 3 || Now - AmmoChangedTime < 4.f)
		{
			const float RW = 200.f * S;
			const float RH = 84.f * S;
			const float RY = ReadoutBottom - RH;
			DrawRect(Ink(0.55f), RX - RW, RY, RW, RH);
			DrawRect(Seafoam(), RX - 3.f * S, RY, 3.f * S, RH);
			DrawLabel(TEXT("AMMO"), RX - RW + 16.f * S, RY + 10.f * S, Small, 0.85f * S, Muted());
			DrawLabel(FString::Printf(TEXT("/ %d"), Reserve), RX - 16.f * S, RY + 30.f * S, Small, 0.95f * S, Muted(), 1.f);
			const float ReserveW = TextWidth(FString::Printf(TEXT("/ %d"), Reserve), Small, 0.95f * S);
			DrawLabel(FString::Printf(TEXT("%d"), Ammo), RX - 24.f * S - ReserveW, RY + 14.f * S, Large, 1.4f * S, Ammo == 0 ? Coral() : Cream(), 1.f);
			if (bReloading)
			{
				DrawLabel(TEXT("RELOADING"), RX - RW + 16.f * S, RY + 56.f * S, Small, 0.8f * S, Seafoam());
				DrawBar(RX - RW + 16.f * S, RY + 74.f * S, RW - 34.f * S, 4.f * S, Hero->GetReloadProgress(), Seafoam());
			}
			else if (Ammo == 0)
			{
				DrawLabel(Reserve > 0 ? TEXT("R / X  Reload") : TEXT("NO AMMO"), RX - RW + 16.f * S, RY + 58.f * S, Small, 0.85f * S, Coral());
			}
		}

		// Crosshair while aiming: four ticks around a dot.
		if (Hero->IsAiming() && !Hero->IsDefeated())
		{
			const float CX = SW * 0.5f;
			const float CY = SH * 0.5f;
			const float Gap = 6.f * S;
			const float Len = 9.f * S;
			const FLinearColor C = Ammo == 0 ? Coral() : Cream();
			DrawLine(CX - Gap - Len, CY, CX - Gap, CY, C, 2.f * S);
			DrawLine(CX + Gap, CY, CX + Gap + Len, CY, C, 2.f * S);
			DrawLine(CX, CY - Gap - Len, CX, CY - Gap, C, 2.f * S);
			DrawLine(CX, CY + Gap, CX, CY + Gap + Len, C, 2.f * S);
			DrawRect(C, CX - 1.f * S, CY - 1.f * S, 2.f * S, 2.f * S);
		}

		if (Hero->IsDefeated())
		{
			DrawRect(Ink(0.35f), 0.f, 0.f, SW, SH);
			DrawCentered(TEXT("KNOCKED OUT"), SW * 0.5f, SH * 0.42f, Large, 1.6f * S, Coral());
		}
	}
	DrawLabel(TEXT("M  Map      F1  Help"), RX, SH - M - 14.f * S, Small, 0.8f * S, WithAlpha(Muted(), 0.7f), 1.f);

	// --- Centre: interaction prompt, notifications, first-moments control hint -------------------
	if (Game)
	{
		const FString Prompt = Game->GetInteractionPrompt(Pawn);
		if (!Prompt.IsEmpty())
		{
			DrawPill(SW * 0.5f, SH * 0.72f, Prompt, Amber(), 1.f);
		}
	}
	DrawStatus(Game, Now, M);

	const float HintAge = Now - PawnChangedTime;
	if (HintAge < ControlsHintSeconds)
	{
		const float Alpha = 1.f - FMath::Clamp((HintAge - (ControlsHintSeconds - 1.5f)) / 1.5f, 0.f, 1.f);
		const FString Hint = Car
			? TEXT("W / RT  Throttle     S / LT  Reverse     Space / B  Brake     Shift / A  Boost     F / Y  Exit")
			: TEXT("WASD  Move     Shift  Sprint     Space / A  Jump     F / Y  Enter vehicle     E  Interact");
		DrawLabel(Hint, SW * 0.5f, SH - M - 14.f * S, Small, 0.85f * S, WithAlpha(Cream(), 0.85f * Alpha), 0.5f);
	}
}

void ANCHUD::DrawLabel(const FString& Text, float X, float Y, UFont* Font, float Scale, const FLinearColor& Color, float AlignX)
{
	if (!Canvas || !Font || Text.IsEmpty())
	{
		return;
	}
	if (AlignX != 0.f)
	{
		X -= TextWidth(Text, Font, Scale) * AlignX;
	}
	FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), FText::FromString(Text), Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.55f * Color.A));
	Canvas->DrawItem(Item);
}

void ANCHUD::DrawCentered(const FString& Text, float CX, float CY, UFont* Font, float Scale, const FLinearColor& Color)
{
	if (!Canvas || !Font)
	{
		return;
	}
	float TW = 0.f;
	float TH = 0.f;
	Canvas->TextSize(Font, Text, TW, TH, Scale, Scale);
	DrawLabel(Text, CX - TW * 0.5f, CY - TH * 0.5f, Font, Scale, Color);
}

float ANCHUD::TextWidth(const FString& Text, UFont* Font, float Scale) const
{
	if (!Canvas || !Font)
	{
		return 0.f;
	}
	float TW = 0.f;
	float TH = 0.f;
	Canvas->TextSize(Font, Text, TW, TH, Scale, Scale);
	return TW;
}

TArray<FString> ANCHUD::WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth, int32 MaxLines) const
{
	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);
	TArray<FString> Lines;
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		if (!Line.IsEmpty() && TextWidth(Candidate, Font, Scale) > MaxWidth)
		{
			Lines.Add(Line);
			Line = Word;
			if (Lines.Num() == MaxLines)
			{
				break;
			}
		}
		else
		{
			Line = Candidate;
		}
	}
	if (Lines.Num() < MaxLines && !Line.IsEmpty())
	{
		Lines.Add(Line);
	}
	else if (Lines.Num() == MaxLines && !Line.IsEmpty())
	{
		Lines.Last() += TEXT("...");
	}
	return Lines;
}

void ANCHUD::DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill)
{
	FLinearColor Track = Cream();
	Track.A = 0.18f;
	DrawRect(Track, X, Y, W, H);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H);
}

void ANCHUD::DrawPill(float CenterX, float Y, const FString& Text, const FLinearColor& Accent, float Alpha)
{
	UFont* Font = GEngine->GetSmallFont();
	const float S = UIScale;
	const float Scale = 1.0f * S;
	float TW = 0.f;
	float TH = 0.f;
	Canvas->TextSize(Font, Text, TW, TH, Scale, Scale);
	const float PadX = 18.f * S;
	const float PadY = 8.f * S;
	const float W = TW + PadX * 2.f;
	const float X = CenterX - W * 0.5f;
	DrawRect(Ink(0.74f * Alpha), X, Y, W, TH + PadY * 2.f);
	DrawRect(NCHUDLayout::WithAlpha(Accent, Alpha), X, Y, 3.f * S, TH + PadY * 2.f);
	DrawLabel(Text, X + PadX, Y + PadY, Font, Scale, NCHUDLayout::WithAlpha(Cream(), Alpha));
}

void ANCHUD::DrawStar(float CX, float CY, float R, const FLinearColor& Color, bool bFilled)
{
	FVector2D Pts[10];
	for (int32 i = 0; i < 10; ++i)
	{
		const float A = FMath::DegreesToRadians(-90.f + 36.f * i);
		const float Rad = (i % 2 == 0) ? R : R * 0.45f;
		Pts[i] = FVector2D(CX + FMath::Cos(A) * Rad, CY + FMath::Sin(A) * Rad);
	}
	const float T = 1.5f * UIScale;
	for (int32 i = 0; i < 10; ++i)
	{
		const FVector2D& P0 = Pts[i];
		const FVector2D& P1 = Pts[(i + 1) % 10];
		if (bFilled)
		{
			for (int32 k = 0; k <= 4; ++k)
			{
				const FVector2D Q = P0 + (P1 - P0) * (k / 4.f);
				DrawLine(CX, CY, Q.X, Q.Y, Color, T);
			}
		}
		DrawLine(P0.X, P0.Y, P1.X, P1.Y, Color, T);
	}
}

void ANCHUD::DrawOutline(float X, float Y, float W, float H, const FLinearColor& Color, float Thickness)
{
	DrawRect(Color, X, Y, W, Thickness);
	DrawRect(Color, X, Y + H - Thickness, W, Thickness);
	DrawRect(Color, X, Y, Thickness, H);
	DrawRect(Color, X + W - Thickness, Y, Thickness, H);
}

ANCHUD::FMapView ANCHUD::FullMapView(float SW, float SH) const
{
	using namespace NCHUDLayout;
	const float S = ScaleFor(SH);
	const float Top = 92.f * S;
	const float Bottom = 64.f * S;
	const float Gap = 28.f * S;
	const float LegendW = 300.f * S;
	const float Size = FMath::Max(64.f, FMath::Min(SH - Top - Bottom, SW - 2.f * Margin * S - Gap - LegendW));
	FMapView V;
	V.Size = Size;
	V.Origin = FVector2D(FMath::RoundToFloat((SW - (Size + Gap + LegendW)) * 0.5f), FMath::RoundToFloat(Top + (SH - Top - Bottom - Size) * 0.5f));
	V.MinX = MapMinX;
	V.MaxY = MapHalfY;
	V.K = Size / (MapMaxX - MapMinX);
	return V;
}

bool ANCHUD::ScreenToMap(const FVector2D& Screen, FVector& OutWorld) const
{
	const ANCPlayerController* NCPC = Cast<ANCPlayerController>(GetOwningPlayerController());
	if (!NCPC || !NCPC->IsMapOpen())
	{
		return false;
	}
	const FMapView V = FullMapView(CanvasSize.X, CanvasSize.Y);
	if (!V.Contains(Screen) || V.K <= 0.f)
	{
		return false;
	}
	// Exact inverse of FMapView::ToScreen.
	OutWorld = FVector(V.MinX + (Screen.X - V.Origin.X) / V.K, V.MaxY - (Screen.Y - V.Origin.Y) / V.K, 0.0);
	return true;
}

void ANCHUD::DrawWorldRect(const FMapView& V, float MinX, float MinY, float MaxX, float MaxY, const FLinearColor& Color)
{
	const FVector2D A = V.ToScreen(MinX, MaxY);
	const FVector2D B = V.ToScreen(MaxX, MinY);
	const float X0 = FMath::Max(A.X, V.Origin.X);
	const float Y0 = FMath::Max(A.Y, V.Origin.Y);
	const float X1 = FMath::Min(B.X, V.Origin.X + V.Size);
	const float Y1 = FMath::Min(B.Y, V.Origin.Y + V.Size);
	if (X1 > X0 && Y1 > Y0)
	{
		DrawRect(Color, X0, Y0, X1 - X0, Y1 - Y0);
	}
}

void ANCHUD::DrawWorldLine(const FMapView& V, const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness)
{
	FVector2D SA = V.ToScreen(A.X, A.Y);
	FVector2D SB = V.ToScreen(B.X, B.Y);
	if (NCHUDLayout::ClipSegment(SA, SB, V.Origin, V.Origin + FVector2D(V.Size, V.Size)))
	{
		DrawLine(SA.X, SA.Y, SB.X, SB.Y, Color, Thickness);
	}
}

void ANCHUD::DrawRoute(const FMapView& V, const TArray<FVector2D>& Route, const FLinearColor& Color, float Thickness)
{
	const FLinearColor Under = Ink(0.85f);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 i = 0; i + 1 < Route.Num(); ++i)
		{
			DrawWorldLine(V, Route[i], Route[i + 1], Pass == 0 ? Under : Color, Pass == 0 ? Thickness + 2.f * UIScale : Thickness);
		}
	}
}

void ANCHUD::DrawMapLayers(const FMapView& V, bool bFull)
{
	using namespace NCHUDLayout;
	const float A = bFull ? 1.f : 0.9f;
	constexpr float Far = 400000.f;
	DrawRect(WithAlpha(MapSea(), A), V.Origin.X, V.Origin.Y, V.Size, V.Size);
	DrawWorldRect(V, -Far, -Far, BeachX, Far, WithAlpha(MapLand(), A));
	DrawWorldRect(V, -42000.f, -ANCCityVisual::FloorHalfExtent, -ANCCityVisual::FloorHalfExtent, ANCCityVisual::FloorHalfExtent, WithAlpha(MapSea(), A));
	DrawWorldRect(V, BeachX, -Far, SeaX, Far, WithAlpha(MapSand(), A));
	DrawWorldRect(V, ANCCityVisual::CoastRoadX + RoadHalfW, -Far, BeachX, Far, WithAlpha(MapPromenade(), A));

	// Blocks: sidewalk ring and the built-up core.
	const float Land = ANCCityVisual::GridSpacing * 0.5f - RoadHalfW;
	const float Core = Land - ANCCityVisual::SidewalkWidth;
	for (int32 bx = 0; bx < ANCCityVisual::GridRoadCount - 1; ++bx)
	{
		for (int32 by = 0; by < ANCCityVisual::GridRoadCount - 1; ++by)
		{
			const float CX = ANCCityVisual::GridFirstRoad + ANCCityVisual::GridSpacing * (bx + 0.5f);
			const float CY = ANCCityVisual::GridFirstRoad + ANCCityVisual::GridSpacing * (by + 0.5f);
			DrawWorldRect(V, CX - Land, CY - Land, CX + Land, CY + Land, WithAlpha(MapBlock(), A));
			DrawWorldRect(V, CX - Core, CY - Core, CX + Core, CY + Core, WithAlpha(MapBuilding(), A));
		}
	}

	// Streets; the coast road runs the full length of the shore.
	const float Edge = ANCCityVisual::FloorHalfExtent;
	for (int32 i = 0; i < ANCCityVisual::GridRoadCount; ++i)
	{
		const float C = ANCCityVisual::GridFirstRoad + ANCCityVisual::GridSpacing * i;
		const bool bCoast = i == ANCCityVisual::GridRoadCount - 1;
		DrawWorldRect(V, -Edge, C - RoadHalfW, ANCCityVisual::CoastRoadX, C + RoadHalfW, WithAlpha(MapRoad(), A));
		DrawWorldRect(V, C - RoadHalfW, bCoast ? -Far : -Edge, C + RoadHalfW, bCoast ? Far : Edge, WithAlpha(bCoast ? MapCoastRoad() : MapRoad(), A));
	}
}

void ANCHUD::DrawMarkerAt(const FVector2D& P, EMarker Kind, float Px)
{
	UFont* Small = GEngine->GetSmallFont();
	const float H = Px * 0.5f;
	const float T = FMath::Max(1.f, 2.f * UIScale);
	auto Badge = [&](const FLinearColor& Fill, const TCHAR* Glyph)
	{
		DrawRect(Ink(0.9f), P.X - H - T, P.Y - H - T, Px + 2.f * T, Px + 2.f * T);
		DrawRect(Fill, P.X - H, P.Y - H, Px, Px);
		DrawCentered(Glyph, P.X, P.Y, Small, Px / (18.f * UIScale) * 0.8f * UIScale, Ink(1.f));
	};
	switch (Kind)
	{
	case EMarker::Objective: Badge(Amber(), TEXT("!")); break;
	case EMarker::Depot:     Badge(Cream(), TEXT("D")); break;
	case EMarker::Garage:    Badge(Seafoam(), TEXT("G")); break;
	case EMarker::Clinic:    Badge(Coral(), TEXT("+")); break;
	case EMarker::Race:      Badge(Rose(), TEXT("R")); break;
	case EMarker::Waypoint:
		DrawOutline(P.X - H, P.Y - H, Px, Px, Ink(0.9f), T + 2.f);
		DrawOutline(P.X - H, P.Y - H, Px, Px, Seafoam(), T);
		DrawRect(Seafoam(), P.X - T, P.Y - T, 2.f * T, 2.f * T);
		break;
	case EMarker::Discovery:
	{
		const float R = H * 0.8f;
		const FLinearColor C = Lavender();
		DrawLine(P.X, P.Y - R, P.X + R, P.Y, C, T);
		DrawLine(P.X + R, P.Y, P.X, P.Y + R, C, T);
		DrawLine(P.X, P.Y + R, P.X - R, P.Y, C, T);
		DrawLine(P.X - R, P.Y, P.X, P.Y - R, C, T);
		break;
	}
	}
}

void ANCHUD::DrawMarker(const FMapView& V, const FVector& World, EMarker Kind, float Px, bool bClampToEdge)
{
	FVector2D P = V.ToScreen(World.X, World.Y);
	if (!V.Contains(P))
	{
		if (!bClampToEdge)
		{
			return;
		}
		const float Inset = Px * 0.7f;
		P.X = FMath::Clamp(P.X, V.Origin.X + Inset, V.Origin.X + V.Size - Inset);
		P.Y = FMath::Clamp(P.Y, V.Origin.Y + Inset, V.Origin.Y + V.Size - Inset);
		Px *= 0.8f;
	}
	DrawMarkerAt(P, Kind, Px);
}

void ANCHUD::DrawPlayerArrow(const FVector2D& P, float Yaw, float Size)
{
	const float Rad = FMath::DegreesToRadians(Yaw);
	const FVector2D Dir(FMath::Cos(Rad), -FMath::Sin(Rad));	// world +Y is screen up
	const FVector2D Right(-Dir.Y, Dir.X);
	const FVector2D Tip = P + Dir * Size;
	const FVector2D BackL = P - Dir * (Size * 0.6f) + Right * (Size * 0.62f);
	const FVector2D BackR = P - Dir * (Size * 0.6f) - Right * (Size * 0.62f);
	const float T = FMath::Max(1.f, 1.5f * UIScale);
	for (int32 k = 0; k <= 10; ++k)
	{
		const FVector2D Q = BackL + (BackR - BackL) * (k / 10.f);
		DrawLine(Tip.X, Tip.Y, Q.X, Q.Y, Coral(), T + 0.5f);
	}
	DrawLine(Tip.X, Tip.Y, BackL.X, BackL.Y, Cream(), T);
	DrawLine(Tip.X, Tip.Y, BackR.X, BackR.Y, Cream(), T);
	DrawLine(BackL.X, BackL.Y, BackR.X, BackR.Y, Cream(), T);
}

void ANCHUD::BuildRoute(const FVector2D& From, const FVector2D& To, TArray<FVector2D>& Out)
{
	using namespace NCHUDLayout;
	Out.Reset();
	struct FSnap { FVector2D P; bool bVertical; };
	// Snap onto the nearest street; beyond the grid (beach, north/south strip) only the coast road continues.
	auto Snap = [](const FVector2D& P) -> FSnap
	{
		const float Edge = ANCCityVisual::FloorHalfExtent;
		const float PX = static_cast<float>(P.X);
		const float PY = static_cast<float>(P.Y);
		if (PX > ANCCityVisual::CoastRoadX || FMath::Abs(PY) > Edge)
		{
			return { FVector2D(ANCCityVisual::CoastRoadX, FMath::Clamp(PY, -MapHalfY, MapHalfY)), true };
		}
		const float RX = NearestRoad(PX);
		const float RY = NearestRoad(PY);
		if (FMath::Abs(PX - RX) <= FMath::Abs(PY - RY))
		{
			return { FVector2D(RX, FMath::Clamp(PY, -Edge, Edge)), true };
		}
		return { FVector2D(FMath::Clamp(PX, -Edge, ANCCityVisual::CoastRoadX), RY), false };
	};

	const FSnap A = Snap(From);
	const FSnap B = Snap(To);
	TArray<FVector2D> Pts = { From, A.P };
	if (A.bVertical && B.bVertical)
	{
		if (!FMath::IsNearlyEqual(A.P.X, B.P.X, 1.0))
		{
			const float Y = NearestRoad((A.P.Y + B.P.Y) * 0.5f);
			Pts.Add(FVector2D(A.P.X, Y));
			Pts.Add(FVector2D(B.P.X, Y));
		}
	}
	else if (!A.bVertical && !B.bVertical)
	{
		if (!FMath::IsNearlyEqual(A.P.Y, B.P.Y, 1.0))
		{
			const float X = NearestRoad((A.P.X + B.P.X) * 0.5f);
			Pts.Add(FVector2D(X, A.P.Y));
			Pts.Add(FVector2D(X, B.P.Y));
		}
	}
	else if (A.bVertical)
	{
		Pts.Add(FVector2D(A.P.X, B.P.Y));
	}
	else
	{
		Pts.Add(FVector2D(B.P.X, A.P.Y));
	}
	Pts.Add(B.P);
	Pts.Add(To);

	for (const FVector2D& P : Pts)
	{
		if (Out.Num() == 0 || FVector2D::DistSquared(Out.Last(), P) > 1.0)
		{
			Out.Add(P);
		}
	}
}

float ANCHUD::RouteLength(const TArray<FVector2D>& Route)
{
	float Len = 0.f;
	for (int32 i = 0; i + 1 < Route.Num(); ++i)
	{
		Len += FVector2D::Distance(Route[i], Route[i + 1]);
	}
	return Len;
}

FString ANCHUD::FormatDistance(float Cm)
{
	const float Meters = Cm / 100.f;
	if (Meters < 1000.f)
	{
		return FString::Printf(TEXT("%d m"), FMath::Max(0, FMath::RoundToInt(Meters / 10.f) * 10));
	}
	return FString::Printf(TEXT("%.1f km"), Meters / 1000.f);
}

FString ANCHUD::FormatCash(int32 Cash)
{
	FString Digits = FString::FromInt(FMath::Abs(Cash));
	for (int32 i = Digits.Len() - 3; i > 0; i -= 3)
	{
		Digits.InsertAt(i, TEXT(','));
	}
	return (Cash < 0 ? TEXT("-$") : TEXT("$")) + Digits;
}

FString ANCHUD::ClockText(FString* OutPhase) const
{
	const ANCCityVisual* CityActor = City.Get();
	if (!CityActor)
	{
		return FString();
	}
	const float Hours = CityActor->GetTimeOfDayHours();
	const int32 Minutes = FMath::FloorToInt(Hours * 60.f) % (24 * 60);
	if (OutPhase)
	{
		*OutPhase = Hours >= 5.5f && Hours < 7.5f ? TEXT("DAWN")
			: Hours >= 7.5f && Hours < 17.f ? TEXT("DAY")
			: Hours >= 17.f && Hours < 19.75f ? TEXT("DUSK")
			: TEXT("NIGHT");
	}
	return FString::Printf(TEXT("%02d:%02d"), Minutes / 60, Minutes % 60);
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

void ANCHUD::DrawMinimap(const UNCGameplaySubsystem* Game, const APawn* Pawn, const TArray<FVector2D>& Route, const FLinearColor& RouteColor,
	const FVector* Objective, const FVector* Waypoint, float X, float Y, float Size)
{
	using namespace NCHUDLayout;
	const float S = UIScale;
	const FVector L = Pawn->GetActorLocation();

	// North-up window centred on the player; widens with speed.
	FMapView V;
	V.Origin = FVector2D(X, Y);
	V.Size = Size;
	V.K = Size / MinimapSpan;
	V.MinX = static_cast<float>(L.X) - MinimapSpan * 0.5f;
	V.MaxY = static_cast<float>(L.Y) + MinimapSpan * 0.5f;
	DrawMapLayers(V, false);

	if (Game)
	{
		const float Px = 13.f * S;
		DrawMarker(V, Game->GetDepotLocation(), EMarker::Depot, Px, false);
		DrawMarker(V, Game->GetGarageLocation(), EMarker::Garage, Px, false);
		DrawMarker(V, Game->GetClinicLocation(), EMarker::Clinic, Px, false);
		DrawMarker(V, Game->GetRaceStartLocation(), EMarker::Race, Px, false);
	}
	DrawRoute(V, Route, RouteColor, 3.f * S);
	if (Objective) { DrawMarker(V, *Objective, EMarker::Objective, 16.f * S, true); }
	if (Waypoint) { DrawMarker(V, *Waypoint, EMarker::Waypoint, 15.f * S, true); }
	DrawPlayerArrow(V.ToScreen(L.X, L.Y), Pawn->GetActorRotation().Yaw, 9.f * S);

	// Frame flashes police red/blue while officers have eyes on the player.
	FLinearColor Frame = WithAlpha(Cream(), 0.3f);
	if (Game && Game->GetWantedLevel() > 0 && Game->IsPoliceSeeingPlayer() && GetWorld())
	{
		Frame = FMath::Fmod(static_cast<float>(GetWorld()->GetRealTimeSeconds()), 0.5f) < 0.25f ? Rose() : FLinearColor(0.2f, 0.45f, 1.f);
	}
	DrawOutline(X, Y, Size, Size, Frame, FMath::Max(1.f, 2.f * S));
	DrawCentered(TEXT("N"), X + Size * 0.5f, Y + 10.f * S, GEngine->GetSmallFont(), 0.8f * S, Cream());
}

void ANCHUD::DrawFullMap(const UNCGameplaySubsystem* Game, const APawn* Pawn)
{
	using namespace NCHUDLayout;
	const float S = UIScale;
	const float SW = Canvas->ClipX;
	const float SH = Canvas->ClipY;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();

	DrawRect(Ink(0.86f), 0.f, 0.f, SW, SH);
	const FMapView V = FullMapView(SW, SH);
	DrawMapLayers(V, true);

	struct FArea { const TCHAR* Name; float X; float Y; };
	static const FArea Areas[] =
	{
		{ TEXT("MERIDIAN DOWNTOWN"), -14400.f, 10800.f }, { TEXT("PALM GROVE"), -14400.f, -10800.f },
		{ TEXT("ART DECO DISTRICT"), 3600.f, 0.f }, { TEXT("NORTH KEY"), 3600.f, 18000.f }, { TEXT("SOUTH MARINA"), 3600.f, -18000.f },
		{ TEXT("OCEAN DRIVE"), 19000.f, 26800.f }, { TEXT("BEACH"), 28000.f, -26800.f },
	};
	for (const FArea& Area : Areas)
	{
		const FVector2D P = V.ToScreen(Area.X, Area.Y);
		DrawCentered(Area.Name, P.X, P.Y, Small, 0.85f * S, WithAlpha(Cream(), 0.7f));
	}

	int32 Discovered = 0;
	int32 DiscoveryTotal = 0;
	if (Game)
	{
		const TArray<FVector>& Spots = Game->GetCollectibleLocations();
		DiscoveryTotal = Spots.Num();
		for (int32 i = 0; i < Spots.Num(); ++i)
		{
			if (Game->IsCollectibleCollected(i)) { ++Discovered; }
			else { DrawMarker(V, Spots[i], EMarker::Discovery, 14.f * S, false); }
		}
		const float Px = 18.f * S;
		DrawMarker(V, Game->GetDepotLocation(), EMarker::Depot, Px, false);
		DrawMarker(V, Game->GetGarageLocation(), EMarker::Garage, Px, false);
		DrawMarker(V, Game->GetClinicLocation(), EMarker::Clinic, Px, false);
		DrawMarker(V, Game->GetRaceStartLocation(), EMarker::Race, Px, false);
	}

	const ANCPlayerController* NCPC = Cast<ANCPlayerController>(GetOwningPlayerController());
	const bool bObjective = Game && Game->HasObjective();
	const bool bWaypoint = NCPC && NCPC->HasWaypoint();
	float ObjectiveDistance = -1.f;
	if (Pawn)
	{
		const FVector2D From(Pawn->GetActorLocation());
		TArray<FVector2D> Route;
		if (bObjective)
		{
			BuildRoute(From, FVector2D(Game->GetObjectiveLocation()), Route);
			ObjectiveDistance = RouteLength(Route);
			DrawRoute(V, Route, Amber(), 3.f * S);
		}
		if (bWaypoint)
		{
			BuildRoute(From, FVector2D(NCPC->GetWaypoint()), Route);
			DrawRoute(V, Route, Seafoam(), 3.f * S);
		}
	}
	if (bObjective) { DrawMarker(V, Game->GetObjectiveLocation(), EMarker::Objective, 20.f * S, true); }
	if (bWaypoint) { DrawMarker(V, NCPC->GetWaypoint(), EMarker::Waypoint, 18.f * S, true); }
	if (Pawn)
	{
		const FVector L = Pawn->GetActorLocation();
		FVector2D P = V.ToScreen(L.X, L.Y);
		P.X = FMath::Clamp(P.X, V.Origin.X, V.Origin.X + V.Size);
		P.Y = FMath::Clamp(P.Y, V.Origin.Y, V.Origin.Y + V.Size);
		DrawPlayerArrow(P, Pawn->GetActorRotation().Yaw, 12.f * S);
	}

	// Cursor crosshair and the district under it.
	FString Hovered;
	float MX = 0.f;
	float MY = 0.f;
	FVector HoverWorld;
	if (NCPC && NCPC->GetMousePosition(MX, MY) && ScreenToMap(FVector2D(MX, MY), HoverWorld))
	{
		const FLinearColor Hair = WithAlpha(Cream(), 0.3f);
		DrawRect(Hair, V.Origin.X, MY, V.Size, 1.f);
		DrawRect(Hair, MX, V.Origin.Y, 1.f, V.Size);
		Hovered = HoverWorld.X > SeaX ? FString(TEXT("ATLANTIC")) : HoverWorld.X > BeachX ? FString(TEXT("BEACH")) : DistrictAt(HoverWorld);
	}

	DrawOutline(V.Origin.X - 1.f, V.Origin.Y - 1.f, V.Size + 2.f, V.Size + 2.f, WithAlpha(Cream(), 0.35f), FMath::Max(1.f, 1.5f * S));
	DrawCentered(TEXT("N"), V.Origin.X + V.Size * 0.5f, V.Origin.Y + 14.f * S, Small, 0.9f * S, Cream());
	DrawCentered(TEXT("Click  Set waypoint          M / View  Close"), V.Origin.X + V.Size * 0.5f, V.Origin.Y + V.Size + 24.f * S, Small, 0.9f * S, Muted());

	// Legend column.
	const float LX = V.Origin.X + V.Size + 28.f * S;
	const float LW = 300.f * S;
	DrawRect(Ink(0.6f), LX, V.Origin.Y, LW, V.Size);
	DrawRect(Seafoam(), LX, V.Origin.Y, LW, 2.f * S);
	float Y = V.Origin.Y + 16.f * S;
	const float TX = LX + 20.f * S;
	DrawLabel(TEXT("NEON COAST"), TX, Y, Medium, 1.0f * S, Cream());
	Y += 32.f * S;
	FString Phase;
	const FString Clock = ClockText(&Phase);
	DrawLabel(TEXT("CITY MAP"), TX, Y, Small, 0.85f * S, Seafoam());
	if (!Clock.IsEmpty())
	{
		DrawLabel(Clock + TEXT("  ") + Phase, LX + LW - 18.f * S, Y, Small, 0.85f * S, Muted(), 1.f);
	}
	Y += 36.f * S;

	struct FEntry { EMarker Kind; const TCHAR* Label; };
	static const FEntry Entries[] =
	{
		{ EMarker::Objective, TEXT("Objective") }, { EMarker::Waypoint, TEXT("Waypoint") }, { EMarker::Depot, TEXT("Courier depot") },
		{ EMarker::Garage, TEXT("Garage") }, { EMarker::Clinic, TEXT("Clinic") }, { EMarker::Race, TEXT("Street race") },
		{ EMarker::Discovery, TEXT("Discovery") },
	};
	const float RowH = 28.f * S;
	DrawPlayerArrow(FVector2D(TX + 8.f * S, Y + 9.f * S), 90.f, 8.f * S);
	DrawLabel(TEXT("You"), TX + 30.f * S, Y, Small, 0.95f * S, Cream());
	Y += RowH;
	for (const FEntry& Entry : Entries)
	{
		DrawMarkerAt(FVector2D(TX + 8.f * S, Y + 9.f * S), Entry.Kind, 15.f * S);
		DrawLabel(Entry.Label, TX + 30.f * S, Y, Small, 0.95f * S, Cream());
		Y += RowH;
	}

	if (Game && DiscoveryTotal > 0)
	{
		Y += 8.f * S;
		DrawLabel(TEXT("DISCOVERIES"), TX, Y, Small, 0.85f * S, Lavender());
		DrawLabel(FString::Printf(TEXT("%d / %d"), Discovered, DiscoveryTotal), LX + LW - 18.f * S, Y, Small, 0.85f * S, Cream(), 1.f);
		Y += RowH;
	}

	const FString Objective = Game ? Game->GetObjectiveText() : FString();
	if (!Objective.IsEmpty())
	{
		Y += 8.f * S;
		DrawLabel(TEXT("OBJECTIVE"), TX, Y, Small, 0.85f * S, Amber());
		if (ObjectiveDistance >= 0.f)
		{
			DrawLabel(FormatDistance(ObjectiveDistance), LX + LW - 18.f * S, Y, Small, 0.85f * S, Muted(), 1.f);
		}
		Y += 24.f * S;
		const float Limit = V.Origin.Y + V.Size - 56.f * S;
		for (const FString& Line : WrapText(Objective, Small, 0.95f * S, LW - 40.f * S, 4))
		{
			if (Y > Limit) { break; }
			DrawLabel(Line, TX, Y, Small, 0.95f * S, Cream());
			Y += 21.f * S;
		}
	}

	if (!Hovered.IsEmpty())
	{
		const float HY = V.Origin.Y + V.Size - 34.f * S;
		DrawLabel(TEXT("CURSOR"), TX, HY, Small, 0.8f * S, Muted());
		DrawLabel(Hovered, LX + LW - 18.f * S, HY, Small, 0.85f * S, Cream(), 1.f);
	}
}

void ANCHUD::DrawStatus(const UNCGameplaySubsystem* Game, float Now, float Y)
{
	const FString Message = Game ? Game->GetStatusText() : FString();
	if (Message != LastStatus)
	{
		LastStatus = Message;
		StatusChangedTime = Now;
	}
	if (Message.IsEmpty() || !Canvas)
	{
		return;
	}
	const float Alpha = FMath::Clamp((Now - StatusChangedTime) / 0.25f, 0.f, 1.f);
	DrawPill(Canvas->ClipX * 0.5f, Y, Message, Seafoam(), Alpha);
}

namespace NCHUDHelp
{
	struct FRow { const TCHAR* Action; const TCHAR* Keys; };
	static const FRow Foot[] =
	{
		{ TEXT("Move"), TEXT("WASD  /  Left Stick") },
		{ TEXT("Camera"), TEXT("Mouse  /  Right Stick") },
		{ TEXT("Sprint"), TEXT("Shift  /  L-Stick Press") },
		{ TEXT("Jump"), TEXT("Space  /  A") },
		{ TEXT("Enter vehicle"), TEXT("F  /  Y") },
		{ TEXT("Interact"), TEXT("E  /  D-Pad Right") },
		{ TEXT("Reload"), TEXT("R  /  X") },
		{ TEXT("Recover"), TEXT("T  /  D-Pad Down") },
	};
	static const FRow Drive[] =
	{
		{ TEXT("Throttle"), TEXT("W  /  RT") },
		{ TEXT("Reverse"), TEXT("S  /  LT") },
		{ TEXT("Steer"), TEXT("A D  /  Left Stick") },
		{ TEXT("Brake"), TEXT("Space  /  B") },
		{ TEXT("Boost"), TEXT("Shift  /  A") },
		{ TEXT("Service"), TEXT("E  /  X") },
		{ TEXT("Exit vehicle"), TEXT("F  /  Y") },
		{ TEXT("Horn"), TEXT("H  /  L-Stick Press") },
		{ TEXT("Radio"), TEXT("Q  /  D-Pad Left") },
		{ TEXT("Recover"), TEXT("T  /  D-Pad Down") },
	};
	static const FRow General[] =
	{
		{ TEXT("City map"), TEXT("M  /  View") },
		{ TEXT("Pause"), TEXT("Esc  /  Start") },
		{ TEXT("Controls"), TEXT("F1") },
		{ TEXT("Photo mode"), TEXT("F6") },
		{ TEXT("Fullscreen"), TEXT("F11") },
		{ TEXT("Screenshot"), TEXT("F12") },
		{ TEXT("Quality (paused)"), TEXT("1  2  3") },
	};
}

void ANCHUD::DrawHelp()
{
	using namespace NCHUDLayout;
	using NCHUDHelp::FRow;
	const float S = UIScale;
	const float SW = Canvas->ClipX;
	const float SH = Canvas->ClipY;
	const float M = Margin * S;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();

	DrawRect(Ink(0.88f), 0.f, 0.f, SW, SH);
	DrawCentered(TEXT("CONTROLS"), SW * 0.5f, M + 30.f * S, Medium, 1.1f * S, Cream());
	DrawCentered(TEXT("Keyboard  /  Controller"), SW * 0.5f, M + 62.f * S, Small, 0.9f * S, Muted());

	struct FCol { const TCHAR* Title; const FRow* Rows; int32 Count; FLinearColor Accent; };
	const FCol Cols[] =
	{
		{ TEXT("ON FOOT"), NCHUDHelp::Foot, static_cast<int32>(UE_ARRAY_COUNT(NCHUDHelp::Foot)), Seafoam() },
		{ TEXT("DRIVING"), NCHUDHelp::Drive, static_cast<int32>(UE_ARRAY_COUNT(NCHUDHelp::Drive)), Coral() },
		{ TEXT("GENERAL"), NCHUDHelp::General, static_cast<int32>(UE_ARRAY_COUNT(NCHUDHelp::General)), Amber() },
	};
	const float Gap = 32.f * S;
	const float ColW = FMath::Min(440.f * S, (SW - 2.f * M - 2.f * Gap) / 3.f);
	const float X0 = (SW - (3.f * ColW + 2.f * Gap)) * 0.5f;
	const float Y0 = M + 100.f * S;
	const float RowH = 28.f * S;
	for (int32 c = 0; c < 3; ++c)
	{
		const FCol& Col = Cols[c];
		const float X = X0 + c * (ColW + Gap);
		DrawRect(Ink(0.5f), X, Y0, ColW, (Col.Count + 1) * RowH + 24.f * S);
		DrawRect(Col.Accent, X, Y0, ColW, 2.f * S);
		DrawLabel(Col.Title, X + 16.f * S, Y0 + 12.f * S, Small, 0.9f * S, Col.Accent);
		float Y = Y0 + 12.f * S + RowH;
		for (int32 r = 0; r < Col.Count; ++r)
		{
			DrawLabel(Col.Rows[r].Action, X + 16.f * S, Y, Small, 0.95f * S, Cream());
			DrawLabel(Col.Rows[r].Keys, X + ColW - 16.f * S, Y, Small, 0.9f * S, Muted(), 1.f);
			Y += RowH;
		}
	}
	DrawCentered(TEXT("F1  Close"), SW * 0.5f, SH - M - 10.f * S, Small, 0.9f * S, Muted());
}

void ANCHUD::DrawPause(const UNCGameplaySubsystem* Game)
{
	using namespace NCHUDLayout;
	using NCHUDHelp::FRow;
	const float S = UIScale;
	const float SW = Canvas->ClipX;
	const float SH = Canvas->ClipY;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Large = GEngine->GetLargeFont();

	DrawRect(Ink(0.8f), 0.f, 0.f, SW, SH);
	const float CX = SW * 0.5f;
	const float TitleY = SH * 0.2f;
	DrawCentered(TEXT("NEON COAST"), CX, TitleY, Large, 2.2f * S, Cream());
	DrawRect(Coral(), CX - 60.f * S, TitleY + 36.f * S, 120.f * S, 3.f * S);
	DrawCentered(TEXT("PAUSED"), CX, TitleY + 60.f * S, Small, 1.0f * S, Seafoam());

	const float ColW = 360.f * S;
	const float Gap = 60.f * S;
	const float LX = CX - Gap * 0.5f - ColW;
	const float RX = CX + Gap * 0.5f;
	const float RowH = 30.f * S;
	float Y = SH * 0.36f;

	static const FRow Menu[] =
	{
		{ TEXT("Resume"), TEXT("Esc  /  Start") },
		{ TEXT("City map"), TEXT("M  /  View") },
		{ TEXT("Controls"), TEXT("F1") },
		{ TEXT("Photo mode"), TEXT("F6") },
		{ TEXT("Fullscreen"), TEXT("F11") },
		{ TEXT("Screenshot"), TEXT("F12") },
	};
	DrawLabel(TEXT("MENU"), LX, Y, Small, 0.85f * S, Seafoam());
	Y += RowH;
	for (const FRow& Row : Menu)
	{
		DrawLabel(Row.Action, LX, Y, Small, 1.0f * S, Cream());
		DrawLabel(Row.Keys, LX + ColW, Y, Small, 0.95f * S, Muted(), 1.f);
		Y += RowH;
	}

	// Graphics quality: the actual engine scalability level, keys 1 / 2 / 3 while paused.
	Y += 10.f * S;
	DrawLabel(TEXT("GRAPHICS QUALITY"), LX, Y, Small, 0.85f * S, Seafoam());
	Y += RowH;
	const int32 Quality = Scalability::GetQualityLevels().GetSingleQualityLevel();
	static const TCHAR* QualityNames[] = { TEXT("1  Low"), TEXT("2  Medium"), TEXT("3  High") };
	float QX = LX;
	for (int32 q = 0; q < 3; ++q)
	{
		const bool bCurrent = Quality == q;
		const float W = TextWidth(QualityNames[q], Small, 1.0f * S);
		DrawLabel(QualityNames[q], QX, Y, Small, 1.0f * S, bCurrent ? Cream() : WithAlpha(Muted(), 0.8f));
		if (bCurrent)
		{
			DrawRect(Seafoam(), QX, Y + 22.f * S, W, 2.f * S);
		}
		QX += W + 28.f * S;
	}
	if (Quality > 2)
	{
		DrawLabel(TEXT("Epic"), LX + ColW, Y, Small, 0.9f * S, Muted(), 1.f);
	}
	else if (Quality < 0)
	{
		DrawLabel(TEXT("Custom"), LX + ColW, Y, Small, 0.9f * S, Muted(), 1.f);
	}

	if (Game)
	{
		Y = SH * 0.36f;
		DrawLabel(TEXT("PROGRESS"), RX, Y, Small, 0.85f * S, Amber());
		Y += RowH;
		const float Best = Game->GetBestRaceTime();
		const FString Rows[][2] =
		{
			{ TEXT("Cash"), FormatCash(Game->GetCash()) },
			{ TEXT("Courier jobs"), FString::FromInt(Game->GetCompletedDeliveries()) },
			{ TEXT("Race wins"), FString::FromInt(Game->GetRaceWins()) },
			{ TEXT("Best race"), Best > 0.f ? FString::Printf(TEXT("%d:%04.1f"), FMath::FloorToInt(Best / 60.f), FMath::Fmod(Best, 60.f)) : FString(TEXT("--")) },
			{ TEXT("Discoveries"), FString::Printf(TEXT("%d / %d"), Game->GetCollectibleCount(), Game->GetCollectibleLocations().Num()) },
		};
		for (const FString (&Row)[2] : Rows)
		{
			DrawLabel(Row[0], RX, Y, Small, 1.0f * S, Cream());
			DrawLabel(Row[1], RX + ColW, Y, Small, 0.95f * S, Muted(), 1.f);
			Y += RowH;
		}
	}
}
