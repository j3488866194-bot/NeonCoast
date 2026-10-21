#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NCHUD.generated.h"

class UFont;

/**
 * Restrained cinematic HUD drawn with Canvas primitives only.
 * Layout is normalised against a 1080p reference height, so it scales with resolution.
 */
UCLASS()
class NEONCOAST_API ANCHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Seconds the full control list stays visible after possessing a pawn. */
	UPROPERTY(EditAnywhere, Category = "Neon Coast|HUD")
	float ControlsHintSeconds = 14.f;

	UPROPERTY(EditAnywhere, Category = "Neon Coast|HUD")
	bool bShowMinimap = true;

	// Palette (sRGB, matches the original Neon Coast interface).
	static FLinearColor Cream() { return FLinearColor(FColor(0xFF, 0xF2, 0xD6)); }
	static FLinearColor Coral() { return FLinearColor(FColor(0xFF, 0x75, 0x5F)); }
	static FLinearColor Seafoam() { return FLinearColor(FColor(0x6E, 0xE6, 0xCA)); }
	static FLinearColor Muted() { return FLinearColor(FColor(0xAF, 0xBF, 0xBD)); }
	static FLinearColor Ink(float Alpha) { FLinearColor C(FColor(0x10, 0x1D, 0x28)); C.A = Alpha; return C; }

private:
	void DrawLabel(const FString& Text, float X, float Y, UFont* Font, float Scale, const FLinearColor& Color, float AlignX = 0.f);
	void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill);
	void DrawMinimap(float X, float Y, float Size, const FVector& Location, float Yaw);
	void DrawControls(bool bInCar, bool bFull);
	static FString DistrictAt(const FVector& Location);

	float UIScale = 1.f;
	TWeakObjectPtr<APawn> LastPawn;
	float PawnChangedTime = -1000.f;
};
