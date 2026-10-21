#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NCHUD.generated.h"

class UFont;
class ANCCityVisual;
class UNCGameplaySubsystem;

/**
 * Restrained cinematic HUD drawn with Canvas primitives only.
 * Layout is normalised against a 1080p reference height (clamped so 720p stays legible).
 * Overlays: full-screen city map (M / View), help (F1), pause; photo mode hides everything.
 */
UCLASS()
class NEONCOAST_API ANCHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/**
	 * Inverse of the full-screen map projection for click placement. Returns true only while the map is open
	 * and Screen (viewport pixels) is inside the map square; OutWorld has Z = 0.
	 */
	bool ScreenToMap(const FVector2D& Screen, FVector& OutWorld) const;

	/** Seconds the one-line control hint stays visible after possessing a pawn. */
	UPROPERTY(EditAnywhere, Category = "Neon Coast|HUD")
	float ControlsHintSeconds = 12.f;

	UPROPERTY(EditAnywhere, Category = "Neon Coast|HUD")
	bool bShowMinimap = true;

	// Palette (sRGB, matches the original Neon Coast interface).
	static FLinearColor Cream() { return FLinearColor(FColor(0xFF, 0xF2, 0xD6)); }
	static FLinearColor Coral() { return FLinearColor(FColor(0xFF, 0x75, 0x5F)); }
	static FLinearColor Seafoam() { return FLinearColor(FColor(0x6E, 0xE6, 0xCA)); }
	static FLinearColor Amber() { return FLinearColor(FColor(0xFF, 0xC4, 0x57)); }
	static FLinearColor Rose() { return FLinearColor(FColor(0xFF, 0x5C, 0x96)); }
	static FLinearColor Lavender() { return FLinearColor(FColor(0xB8, 0xA6, 0xFF)); }
	static FLinearColor Muted() { return FLinearColor(FColor(0xAF, 0xBF, 0xBD)); }
	static FLinearColor Ink(float Alpha) { FLinearColor C(FColor(0x10, 0x1D, 0x28)); C.A = Alpha; return C; }

private:
	/** North-up orthographic map window: world (X east, Y north) to a screen square. */
	struct FMapView
	{
		FVector2D Origin = FVector2D::ZeroVector;
		float Size = 0.f;
		float MinX = 0.f;
		float MaxY = 0.f;
		float K = 1.f;	// pixels per cm
		FVector2D ToScreen(double WX, double WY) const { return FVector2D(Origin.X + (WX - MinX) * K, Origin.Y + (MaxY - WY) * K); }
		bool Contains(const FVector2D& P) const { return P.X >= Origin.X && P.Y >= Origin.Y && P.X <= Origin.X + Size && P.Y <= Origin.Y + Size; }
	};

	enum class EMarker : uint8 { Objective, Waypoint, Depot, Garage, Clinic, Race, Discovery };

	// Primitives.
	void DrawLabel(const FString& Text, float X, float Y, UFont* Font, float Scale, const FLinearColor& Color, float AlignX = 0.f);
	void DrawCentered(const FString& Text, float CX, float CY, UFont* Font, float Scale, const FLinearColor& Color);
	float TextWidth(const FString& Text, UFont* Font, float Scale) const;
	TArray<FString> WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth, int32 MaxLines) const;
	void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill);
	void DrawPill(float CenterX, float Y, const FString& Text, const FLinearColor& Accent, float Alpha);
	void DrawStar(float CX, float CY, float R, const FLinearColor& Color, bool bFilled);
	void DrawOutline(float X, float Y, float W, float H, const FLinearColor& Color, float Thickness);

	// Maps.
	FMapView FullMapView(float SW, float SH) const;
	void DrawMapLayers(const FMapView& V, bool bFull);
	void DrawWorldRect(const FMapView& V, float MinX, float MinY, float MaxX, float MaxY, const FLinearColor& Color);
	void DrawWorldLine(const FMapView& V, const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness);
	void DrawRoute(const FMapView& V, const TArray<FVector2D>& Route, const FLinearColor& Color, float Thickness);
	void DrawMarker(const FMapView& V, const FVector& World, EMarker Kind, float SizePx, bool bClampToEdge);
	void DrawMarkerAt(const FVector2D& P, EMarker Kind, float SizePx);
	void DrawPlayerArrow(const FVector2D& P, float Yaw, float Size);
	void DrawMinimap(const UNCGameplaySubsystem* Game, const APawn* Pawn, const TArray<FVector2D>& Route, const FLinearColor& RouteColor,
		const FVector* Objective, const FVector* Waypoint, float X, float Y, float Size);
	void DrawFullMap(const UNCGameplaySubsystem* Game, const APawn* Pawn);

	// Panels and overlays.
	void DrawStatus(const UNCGameplaySubsystem* Game, float Now, float Y);
	void DrawHelp();
	void DrawPause(const UNCGameplaySubsystem* Game);

	static void BuildRoute(const FVector2D& From, const FVector2D& To, TArray<FVector2D>& Out);
	static float RouteLength(const TArray<FVector2D>& Route);
	static FString FormatDistance(float Cm);
	static FString FormatCash(int32 Cash);
	static FString DistrictAt(const FVector& Location);
	FString ClockText(FString* OutPhase) const;

	float UIScale = 1.f;
	FVector2D CanvasSize = FVector2D(1920.f, 1080.f);
	TWeakObjectPtr<APawn> LastPawn;
	float PawnChangedTime = -1000.f;
	float MinimapSpan = 18000.f;
	FString LastStatus;
	float StatusChangedTime = -1000.f;
	int32 LastAmmo = -1;
	float AmmoChangedTime = -1000.f;
	TWeakObjectPtr<ANCCityVisual> City;
	float NextCitySearch = 0.f;
};
