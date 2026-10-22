#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCCityVisual.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UHierarchicalInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UTextRenderComponent;

/** Primitive meshes used by the city batches (all from /Engine/BasicShapes). */
UENUM()
enum class ENCShape : uint8
{
	Cube,
	Cylinder,
	Sphere,
	Cone,
	Plane,
	Count UMETA(Hidden)
};

/**
 * Imported public models (/Game/PublicAssets/Models), one HISM per mesh with its imported materials.
 * Contract: centred horizontally, base at Z = 0; facade modules are X = width, Y = depth, Z = height, front towards -Y.
 * Dimensions are always measured from the loaded bounds.
 */
enum class ENCModel : uint8
{
	FacadeWindow,
	FacadeDoor,
	FacadeWall,
	FacadeCornice,
	FacadeCrown,
	FacadePier,
	CoastalTree,
	Bench,
	StreetLamp,
	Count
};

/**
 * Procedural, deterministic Neon Coast city built from engine primitives, dressed with imported
 * facade modules, trees and street furniture where those assets exist.
 * Unreal units (cm), Z up, +X east, +Y north. Everything is batched into a
 * small number of HISM components keyed by (shape, palette, collision) or imported mesh.
 */
UCLASS()
class NEONCOAST_API ANCCityVisual : public AActor
{
	GENERATED_BODY()

public:
	ANCCityVisual();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Current city clock in hours [0, 24). Advances one game hour per SecondsPerGameHour real seconds. */
	float GetTimeOfDayHours() const { return TimeOfDayHours; }

	// City layout contract (cm).
	static constexpr float CityHalfExtent = 24000.f;
	static constexpr float FloorHalfExtent = 25000.f;
	static constexpr float GridSpacing = 7200.f;
	static constexpr float GridFirstRoad = -21600.f;
	static constexpr int32 GridRoadCount = 7;
	static constexpr float RoadWidth = 1600.f;
	static constexpr float SidewalkWidth = 800.f;
	static constexpr float CoastRoadX = 21600.f;
	static constexpr float CoastSidewalkX = 22600.f;
	static constexpr float WestCorridorX = 20600.f;
	static constexpr float BeachMinX = 25500.f;
	static constexpr float BeachMaxX = 31000.f;
	static constexpr float SeaLevelZ = -25.f;

	/** Seed for all procedural variation; same seed always yields the same city. */
	UPROPERTY(EditAnywhere, Category = "Neon Coast|City")
	int32 Seed = 1985;

	UPROPERTY(EditAnywhere, Category = "Neon Coast|City")
	bool bBuildDistantSkyline = true;

	/** Neon text on hotel fins, storefront boards and tower crowns. */
	UPROPERTY(EditAnywhere, Category = "Neon Coast|City")
	bool bBuildSigns = true;

	/** Clock at BeginPlay; warm mid-afternoon so the east lane, promenade and beach are in sun. */
	UPROPERTY(EditAnywhere, Category = "Neon Coast|Time", meta = (ClampMin = "0", ClampMax = "24"))
	float StartHour = 15.5f;

	UPROPERTY(EditAnywhere, Category = "Neon Coast|Time", meta = (ClampMin = "1"))
	float SecondsPerGameHour = 240.f;

	UPROPERTY(EditAnywhere, Category = "Neon Coast|Time")
	bool bDayNightCycle = true;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Neon Coast|City")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Neon Coast|Sky")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Neon Coast|Sky")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Neon Coast|Sky")
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere, Category = "Neon Coast|Sky")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TSoftObjectPtr<UMaterialInterface> CoastMaterial;

	UPROPERTY(VisibleAnywhere, Category = "Neon Coast|Sky")
	TObjectPtr<UPostProcessComponent> Grade;

	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TSoftObjectPtr<UMaterialInterface> FallbackMaterial;

	/** Optional masked font material from build_visual_assets.py; engine default text material otherwise. */
	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TSoftObjectPtr<UMaterialInterface> SignTextMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TArray<TSoftObjectPtr<UStaticMesh>> ShapeMeshes;

	/** Imported models indexed by ENCModel; a model that fails to load keeps its primitive stand-in. */
	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TArray<TSoftObjectPtr<UStaticMesh>> ModelMeshes;

	/** Real PBR instances per palette (MI_CoastPBR_<Palette>); empty or missing entries fall back to M_Coast. */
	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TArray<TSoftObjectPtr<UMaterialInterface>> RealismMaterials;

private:
	struct FLot
	{
		FVector2D Facade;	// centre of the street-facing facade line
		FVector2D Normal;	// outward, towards the street
		FVector2D Tangent;	// along the facade
		float Width = 0.f;
		float Depth = 0.f;
		float Height = 0.f;
	};

	struct FBatchKey
	{
		ENCShape Shape;
		int32 Palette;
		bool bCollide;
		bool bShadow;
		bool bProp;
		bool bHidden;	// collision-only stand-in for a primitive replaced by an imported model
		bool operator==(const FBatchKey& O) const
		{
			return Shape == O.Shape && Palette == O.Palette && bCollide == O.bCollide && bShadow == O.bShadow && bProp == O.bProp && bHidden == O.bHidden;
		}
		friend uint32 GetTypeHash(const FBatchKey& K)
		{
			return HashCombine(::GetTypeHash(static_cast<uint8>(K.Shape)), ::GetTypeHash(K.Palette * 16 + (K.bCollide ? 1 : 0) + (K.bShadow ? 2 : 0) + (K.bProp ? 4 : 0) + (K.bHidden ? 8 : 0)));
		}
	};

	/** Street-level treatment of a clad wall. */
	enum class EGround : uint8
	{
		None,		// upper floors only
		Windows,
		Door		// windows with a door panel in the middle column
	};

	void LoadAssets();
	void BuildPalettes();
	void BuildGround();
	void BuildRoadMarkings();
	void BuildBlocks();
	void BuildBlock(int32 BlockX, int32 BlockY);
	void BuildDecoHotel(const FLot& Lot, FRandomStream& Rng);
	void BuildStorefront(const FLot& Lot, FRandomStream& Rng);
	void BuildTower(const FVector2D& Center, float Size, float Height, FRandomStream& Rng, bool bCollide);
	void BuildStreetscape();
	void BuildWaterfront();
	void BuildDistant();
	/** MaxCanopyRadius > 0 limits an imported tree's crown next to facades. */
	void AddPalm(const FVector& Base, float Height, FRandomStream& Rng, bool bCollide, float MaxCanopyRadius = 0.f);
	/** ToRoad turns an imported lamp's front (local -Y) towards the carriageway. */
	void AddLamp(const FVector& Base, bool bCollide, const FVector2D& ToRoad = FVector2D::ZeroVector);
	void FlushBatches();

	bool HasModel(ENCModel Model) const;
	FVector3f ModelSize(ENCModel Model) const;
	FVector ModelPivot(ENCModel Model) const;
	/** Queue an imported model with uniform scale; its base pivot (origin, bounds bottom) lands on Base. */
	void AddModel(ENCModel Model, const FVector& Base, const FQuat& Rot, float Scale);
	void AddTree(const FVector& Base, const FRotator& Lean, float PalmHeight, float MaxCanopyRadius);
	/** Fit a facade module into a CellW x CellH cell on Wall (Along = cell centre, Z0 = cell bottom). False if it would distort. */
	bool AddFacadeModule(ENCModel Model, const FLot& Wall, float Along, float Z0, float CellW, float CellH);
	/** Tile a horizontal band (cornice/crown) across Span at its own proportions; returns its height, 0 if not placed. */
	float TileFacadeRow(ENCModel Model, const FLot& Wall, float Mid, float Span, float Z, float ScaleHint, float MaxH, bool bHang);
	/** Clad a wall plane with imported window modules between RunMin/RunMax (tangent offsets). False if nothing was placed. */
	bool CladWall(const FLot& Wall, float RunMin, float RunMax, float UpperZ, int32 Floors, float Pitch, int32 BandPalette,
		EGround Ground, bool bFinGap, bool bCrown);
	/** Clad the exposed end face of a corner lot (LotCladEnd); StreetMargin keeps clear of the street corner. */
	void CladEndWall(const FLot& Lot, float StreetMargin, float UpperZ, int32 Floors, float Pitch, int32 BandPalette);

	/** Text sign facing Normal, centred at Facade + Normal * Out, height Z. Uses its own RNG so layout never shifts. */
	void AddSign(const FString& Text, const FVector2D& Facade, const FVector2D& Normal, float Out, float Z, float WorldSize, const FColor& Color);
	void AddLotSign(const FLot& Lot, const FString& Text, float Out, float Z, float WorldSize, const FColor& Color);
	void UpdateLighting(float DeltaSeconds, bool bSnap);

	/** Queue an instance. Size is the full world-space extent in cm (meshes are 100cm primitives). */
	void Add(ENCShape Shape, int32 Palette, const FVector& Center, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator,
		bool bCollide = false, bool bShadow = true, bool bProp = false, bool bHideVisual = false);

	/** Queue a box relative to a lot: Along = offset on tangent, Out = offset from facade along normal. */
	void AddLotBox(const FLot& Lot, int32 Palette, float Along, float Out, float CenterZ, float SizeAlong, float SizeOut, float SizeZ,
		bool bCollide = false, bool bShadow = true, ENCShape Shape = ENCShape::Cube);

	static float RoadCenter(int32 Index) { return GridFirstRoad + GridSpacing * Index; }
	bool IsNearRoadCenter(float V, float Margin) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> LoadedMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PaletteMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Batches;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> Signs;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SignMID;

	/** Loaded imported models (null when missing) and their measured bounds, indexed by ENCModel. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> LoadedModels;

	TArray<FBox> ModelBounds;
	TArray<TArray<FTransform>> PendingModels;
	TMap<FBatchKey, TArray<FTransform>> PendingInstances;
	bool bBuilt = false;
	float LotSignChance = 0.f;	// set per lot by BuildBlock; distant strip copies never carry text
	bool bLotCladFront = false;	// set per lot by BuildBlock/BuildDistant: imported facade modules on the street face
	int32 LotCladEnd = 0;		// +1/-1: also clad the end face at +/- Tangent (corner lots facing the coast road)
	int32 SignCount = 0;
	float TimeOfDayHours = 15.5f;
	float AppliedExposureBias = 0.f;
};
