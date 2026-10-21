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
 * Procedural, deterministic Neon Coast city built from engine primitives.
 * Unreal units (cm), Z up, +X east, +Y north. Everything is batched into a
 * small number of HISM components keyed by (shape, palette, collision).
 */
UCLASS()
class NEONCOAST_API ANCCityVisual : public AActor
{
	GENERATED_BODY()

public:
	ANCCityVisual();

	virtual void BeginPlay() override;

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

	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TSoftObjectPtr<UMaterialInterface> FallbackMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Neon Coast|Assets")
	TArray<TSoftObjectPtr<UStaticMesh>> ShapeMeshes;

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
		bool operator==(const FBatchKey& O) const
		{
			return Shape == O.Shape && Palette == O.Palette && bCollide == O.bCollide && bShadow == O.bShadow && bProp == O.bProp;
		}
		friend uint32 GetTypeHash(const FBatchKey& K)
		{
			return HashCombine(::GetTypeHash(static_cast<uint8>(K.Shape)), ::GetTypeHash(K.Palette * 8 + (K.bCollide ? 1 : 0) + (K.bShadow ? 2 : 0) + (K.bProp ? 4 : 0)));
		}
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
	void AddPalm(const FVector& Base, float Height, FRandomStream& Rng, bool bCollide);
	void AddLamp(const FVector& Base, bool bCollide);
	void FlushBatches();

	/** Queue an instance. Size is the full world-space extent in cm (meshes are 100cm primitives). */
	void Add(ENCShape Shape, int32 Palette, const FVector& Center, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator,
		bool bCollide = false, bool bShadow = true, bool bProp = false);

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

	TMap<FBatchKey, TArray<FTransform>> PendingInstances;
	bool bBuilt = false;
};
