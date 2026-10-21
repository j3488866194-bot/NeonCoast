#include "NCCityVisual.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace NCPal
{
	enum : int32
	{
		Asphalt, Sidewalk, Promenade, Lawn, Sand, WetSand, Water, Foam, LineWhite, LineYellow,
		DecoWhite, DecoMint, DecoPink, DecoLemon, DecoLavender, DecoPeach, DecoAqua, Stucco, Terracotta,
		GlassDark, GlassWarm, GlassTower, Concrete, Trunk, Frond, FrondDark, Metal, Wood,
		NeonPink, NeonAqua, NeonAmber, CanvasCoral, CanvasCream, CanvasTeal, LampGlow, Distant, DistantGlass,
		Count
	};

	struct FDef { FLinearColor Color; float Roughness; float Metallic; float Emissive; };

	// Linear-space colours; pastel deco stucco, warm sand, deep teal sea, restrained neon.
	static const FDef Defs[Count] =
	{
		{ FLinearColor(0.045f, 0.047f, 0.052f), 0.85f, 0.f, 0.f },   // Asphalt
		{ FLinearColor(0.42f, 0.40f, 0.37f), 0.80f, 0.f, 0.f },      // Sidewalk
		{ FLinearColor(0.62f, 0.42f, 0.34f), 0.75f, 0.f, 0.f },      // Promenade (coral pavers)
		{ FLinearColor(0.10f, 0.20f, 0.06f), 0.90f, 0.f, 0.f },      // Lawn
		{ FLinearColor(0.78f, 0.64f, 0.44f), 0.92f, 0.f, 0.f },      // Sand
		{ FLinearColor(0.46f, 0.37f, 0.25f), 0.45f, 0.f, 0.f },      // WetSand
		{ FLinearColor(0.010f, 0.075f, 0.095f), 0.04f, 0.1f, 0.f },  // Water
		{ FLinearColor(0.85f, 0.88f, 0.86f), 0.60f, 0.f, 0.15f },    // Foam
		{ FLinearColor(0.80f, 0.80f, 0.76f), 0.70f, 0.f, 0.f },      // LineWhite
		{ FLinearColor(0.85f, 0.55f, 0.08f), 0.70f, 0.f, 0.f },      // LineYellow
		{ FLinearColor(0.88f, 0.86f, 0.80f), 0.65f, 0.f, 0.f },      // DecoWhite
		{ FLinearColor(0.45f, 0.78f, 0.64f), 0.65f, 0.f, 0.f },      // DecoMint
		{ FLinearColor(0.92f, 0.52f, 0.55f), 0.65f, 0.f, 0.f },      // DecoPink
		{ FLinearColor(0.95f, 0.82f, 0.42f), 0.65f, 0.f, 0.f },      // DecoLemon
		{ FLinearColor(0.62f, 0.55f, 0.85f), 0.65f, 0.f, 0.f },      // DecoLavender
		{ FLinearColor(0.96f, 0.64f, 0.45f), 0.65f, 0.f, 0.f },      // DecoPeach
		{ FLinearColor(0.40f, 0.74f, 0.82f), 0.65f, 0.f, 0.f },      // DecoAqua
		{ FLinearColor(0.70f, 0.64f, 0.55f), 0.80f, 0.f, 0.f },      // Stucco
		{ FLinearColor(0.55f, 0.22f, 0.12f), 0.75f, 0.f, 0.f },      // Terracotta
		{ FLinearColor(0.020f, 0.040f, 0.050f), 0.08f, 0.2f, 0.f },  // GlassDark
		{ FLinearColor(0.30f, 0.18f, 0.08f), 0.15f, 0.f, 0.6f },     // GlassWarm (lit interiors)
		{ FLinearColor(0.05f, 0.11f, 0.14f), 0.06f, 0.5f, 0.f },     // GlassTower
		{ FLinearColor(0.32f, 0.31f, 0.30f), 0.85f, 0.f, 0.f },      // Concrete
		{ FLinearColor(0.30f, 0.22f, 0.15f), 0.90f, 0.f, 0.f },      // Trunk
		{ FLinearColor(0.08f, 0.22f, 0.05f), 0.70f, 0.f, 0.f },      // Frond
		{ FLinearColor(0.05f, 0.14f, 0.04f), 0.70f, 0.f, 0.f },      // FrondDark
		{ FLinearColor(0.55f, 0.56f, 0.58f), 0.35f, 1.f, 0.f },      // Metal
		{ FLinearColor(0.36f, 0.22f, 0.12f), 0.70f, 0.f, 0.f },      // Wood
		{ FLinearColor(1.00f, 0.18f, 0.45f), 0.40f, 0.f, 6.f },      // NeonPink
		{ FLinearColor(0.15f, 0.95f, 0.80f), 0.40f, 0.f, 5.f },      // NeonAqua
		{ FLinearColor(1.00f, 0.55f, 0.15f), 0.40f, 0.f, 5.f },      // NeonAmber
		{ FLinearColor(0.95f, 0.36f, 0.28f), 0.85f, 0.f, 0.f },      // CanvasCoral
		{ FLinearColor(0.92f, 0.86f, 0.72f), 0.85f, 0.f, 0.f },      // CanvasCream
		{ FLinearColor(0.20f, 0.62f, 0.58f), 0.85f, 0.f, 0.f },      // CanvasTeal
		{ FLinearColor(1.00f, 0.80f, 0.55f), 0.50f, 0.f, 8.f },      // LampGlow
		{ FLinearColor(0.50f, 0.50f, 0.52f), 0.80f, 0.f, 0.f },      // Distant
		{ FLinearColor(0.10f, 0.16f, 0.20f), 0.10f, 0.4f, 0.f },     // DistantGlass
	};

	static const int32 DecoColors[] = { DecoWhite, DecoMint, DecoPink, DecoLemon, DecoLavender, DecoPeach, DecoAqua };
	static const int32 Neons[] = { NeonPink, NeonAqua, NeonAmber };
	static const int32 Canvases[] = { CanvasCoral, CanvasCream, CanvasTeal };

	template <int32 N>
	static int32 Pick(const int32 (&Arr)[N], FRandomStream& Rng) { return Arr[Rng.RandRange(0, N - 1)]; }
}

ANCCityVisual::ANCCityVisual()
{
	PrimaryActorTick.bCanEverTick = false;

	// Static root so the static HISM batches can attach; lights below are movable children.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->Mobility = EComponentMobility::Static;
	SetRootComponent(SceneRoot);

	// Early-morning golden hour: low sun rising over the Atlantic from the south-east,
	// lighting the street-facing deco facades on the west side of the coast road.
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(SceneRoot);
	Sun->Mobility = EComponentMobility::Movable;
	Sun->SetRelativeRotation(FRotator(-11.f, 158.f, 0.f));
	Sun->Intensity = 9.f;
	Sun->LightColor = FColor(255, 222, 186);
	Sun->DynamicShadowDistanceMovableLight = 24000.f;
	Sun->SetAtmosphereSunLight(true);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->Mobility = EComponentMobility::Movable;
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->Intensity = 1.f;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(SceneRoot);

	// Light coastal haze only; aerial perspective from the atmosphere does most of the depth work.
	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(SceneRoot);
	HeightFog->SetRelativeLocation(FVector(0.f, 0.f, -100.f));
	HeightFog->FogDensity = 0.0035f;
	HeightFog->FogHeightFalloff = 0.12f;
	HeightFog->StartDistance = 3000.f;
	HeightFog->FogMaxOpacity = 0.8f;

	CoastMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_Coast.M_Coast")));
	FallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane"))));
}

void ANCCityVisual::BeginPlay()
{
	Super::BeginPlay();
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;

	LoadAssets();
	BuildPalettes();
	BuildGround();
	BuildRoadMarkings();
	BuildBlocks();
	BuildStreetscape();
	BuildWaterfront();
	if (bBuildDistantSkyline)
	{
		BuildDistant();
	}
	FlushBatches();
}

void ANCCityVisual::LoadAssets()
{
	LoadedMeshes.Reset();
	for (const TSoftObjectPtr<UStaticMesh>& Mesh : ShapeMeshes)
	{
		LoadedMeshes.Add(Mesh.LoadSynchronous());
	}
	while (LoadedMeshes.Num() < static_cast<int32>(ENCShape::Count))
	{
		LoadedMeshes.Add(nullptr);
	}
}

void ANCCityVisual::BuildPalettes()
{
	UMaterialInterface* Base = CoastMaterial.LoadSynchronous();
	if (!Base)
	{
		Base = FallbackMaterial.LoadSynchronous();
	}

	PaletteMaterials.Reset();
	for (int32 i = 0; i < NCPal::Count; ++i)
	{
		const NCPal::FDef& Def = NCPal::Defs[i];
		UMaterialInstanceDynamic* MID = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("BaseColor"), Def.Color);
			MID->SetVectorParameterValue(TEXT("Color"), Def.Color); // BasicShapeMaterial fallback
			MID->SetScalarParameterValue(TEXT("Roughness"), Def.Roughness);
			MID->SetScalarParameterValue(TEXT("Metallic"), Def.Metallic);
			MID->SetScalarParameterValue(TEXT("EmissiveStrength"), Def.Emissive);
		}
		PaletteMaterials.Add(MID);
	}
}

bool ANCCityVisual::IsNearRoadCenter(float V, float Margin) const
{
	for (int32 i = 0; i < GridRoadCount; ++i)
	{
		if (FMath::Abs(V - RoadCenter(i)) < Margin)
		{
			return true;
		}
	}
	return false;
}

void ANCCityVisual::Add(ENCShape Shape, int32 Palette, const FVector& Center, const FVector& Size, const FRotator& Rot,
	bool bCollide, bool bShadow, bool bProp)
{
	const FBatchKey Key{ Shape, Palette, bCollide, bShadow, bProp };
	FVector Scale = Size / 100.f;
	if (Shape == ENCShape::Plane)
	{
		Scale.Z = 1.f;
	}
	PendingInstances.FindOrAdd(Key).Add(FTransform(Rot, Center, Scale));
}

void ANCCityVisual::AddLotBox(const FLot& Lot, int32 Palette, float Along, float Out, float CenterZ, float SizeAlong, float SizeOut, float SizeZ,
	bool bCollide, bool bShadow, ENCShape Shape)
{
	const FVector2D P = Lot.Facade + Lot.Tangent * Along + Lot.Normal * Out;
	const bool bFacesX = FMath::Abs(Lot.Normal.X) > 0.5f;
	const FVector Size = bFacesX ? FVector(SizeOut, SizeAlong, SizeZ) : FVector(SizeAlong, SizeOut, SizeZ);
	Add(Shape, Palette, FVector(P.X, P.Y, CenterZ), Size, FRotator::ZeroRotator, bCollide, bShadow, false);
}

void ANCCityVisual::FlushBatches()
{
	for (TPair<FBatchKey, TArray<FTransform>>& Pair : PendingInstances)
	{
		const FBatchKey& Key = Pair.Key;
		UStaticMesh* Mesh = LoadedMeshes.IsValidIndex(static_cast<int32>(Key.Shape)) ? LoadedMeshes[static_cast<int32>(Key.Shape)].Get() : nullptr;
		if (!Mesh || Pair.Value.Num() == 0)
		{
			continue;
		}

		UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		HISM->SetupAttachment(SceneRoot);
		HISM->SetMobility(EComponentMobility::Static);
		HISM->SetStaticMesh(Mesh);
		if (PaletteMaterials.IsValidIndex(Key.Palette) && PaletteMaterials[Key.Palette])
		{
			HISM->SetMaterial(0, PaletteMaterials[Key.Palette]);
		}
		if (Key.bCollide)
		{
			HISM->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			HISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		else
		{
			HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HISM->SetCanEverAffectNavigation(false);
		}
		HISM->SetCastShadow(Key.bShadow);
		if (Key.bProp)
		{
			HISM->SetCullDistances(0, 20000);
		}
		HISM->RegisterComponent();
		HISM->AddInstances(Pair.Value, false);
		AddInstanceComponent(HISM);
		Batches.Add(HISM);
	}
	PendingInstances.Reset();
}

void ANCCityVisual::BuildGround()
{
	// Gameplay floor: 50000 x 50000, top at Z = 0. Asphalt reads as the street network.
	Add(ENCShape::Cube, NCPal::Asphalt, FVector(0.f, 0.f, -50.f), FVector(FloorHalfExtent * 2.f, FloorHalfExtent * 2.f, 100.f), FRotator::ZeroRotator, true, false);

	// Visual land beyond the playable floor (sits just below it, no collision); a bay separates
	// the city from the far downtown skyline in the west.
	Add(ENCShape::Cube, NCPal::Lawn, FVector(-8500.f, 62500.f, -52.f), FVector(67000.f, 75000.f, 100.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Lawn, FVector(-8500.f, -62500.f, -52.f), FVector(67000.f, 75000.f, 100.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Lawn, FVector(-96000.f, 0.f, -52.f), FVector(108000.f, 200000.f, 100.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Plane, NCPal::Water, FVector(-33500.f, 0.f, SeaLevelZ), FVector(17000.f, 50000.f, 1.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::WetSand, FVector(-33500.f, 0.f, -240.f), FVector(17000.f, 50000.f, 100.f), FRotator::ZeroRotator, true, false);
	Add(ENCShape::Cube, NCPal::DecoWhite, FVector(-FloorHalfExtent - 60.f, 0.f, -40.f), FVector(120.f, 50000.f, 120.f), FRotator::ZeroRotator, false, false);

	// Beach slab, walkable, continuing north/south out of the playable area.
	const float BeachFrom = FloorHalfExtent;
	Add(ENCShape::Cube, NCPal::Sand, FVector((BeachFrom + BeachMaxX) * 0.5f, 0.f, -50.f), FVector(BeachMaxX - BeachFrom, 200000.f, 100.f), FRotator::ZeroRotator, true, false);
	// Wet sand shelf sloping under the waterline, plus a shallow seabed so nobody falls into the void.
	Add(ENCShape::Cube, NCPal::WetSand, FVector(BeachMaxX + 1200.f, 0.f, -60.f), FVector(2600.f, 200000.f, 100.f), FRotator(-2.2f, 0.f, 0.f), true, false);
	Add(ENCShape::Cube, NCPal::WetSand, FVector(BeachMaxX + 6500.f, 0.f, -240.f), FVector(8000.f, 200000.f, 100.f), FRotator::ZeroRotator, true, false);
	Add(ENCShape::Cube, NCPal::WetSand, FVector(BeachMaxX - 500.f, 0.f, -45.f), FVector(1000.f, 200000.f, 100.f), FRotator::ZeroRotator, false, false);

	// Flat reflective ocean and a soft foam line.
	Add(ENCShape::Plane, NCPal::Water, FVector(BeachMaxX + 75000.f, 0.f, SeaLevelZ), FVector(150000.f, 200000.f, 1.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Foam, FVector(BeachMaxX + 900.f, 0.f, SeaLevelZ + 1.f), FVector(140.f, 200000.f, 2.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Foam, FVector(BeachMaxX + 1650.f, 0.f, SeaLevelZ + 0.5f), FVector(60.f, 200000.f, 2.f), FRotator::ZeroRotator, false, false);

	// Block sidewalks / plazas (thin overlays, no collision, keep the floor flat for vehicles).
	for (int32 bx = 0; bx < GridRoadCount - 1; ++bx)
	{
		for (int32 by = 0; by < GridRoadCount - 1; ++by)
		{
			const float CX = RoadCenter(bx) + GridSpacing * 0.5f;
			const float CY = RoadCenter(by) + GridSpacing * 0.5f;
			const float Land = GridSpacing - RoadWidth;
			Add(ENCShape::Cube, NCPal::Sidewalk, FVector(CX, CY, 1.5f), FVector(Land, Land, 3.f), FRotator::ZeroRotator, false, false);
			Add(ENCShape::Cube, NCPal::Concrete, FVector(CX, CY, 3.5f), FVector(Land - 2.f * SidewalkWidth, Land - 2.f * SidewalkWidth, 3.f), FRotator::ZeroRotator, false, false);
		}
	}

	// Outer ring of sidewalk between the perimeter roads and the city edge (west, north, south).
	const float Edge0 = RoadCenter(0) - RoadWidth * 0.5f;
	const float EdgeN = RoadCenter(GridRoadCount - 1) + RoadWidth * 0.5f;
	const float Ring = FloorHalfExtent - EdgeN;
	Add(ENCShape::Cube, NCPal::Sidewalk, FVector(-FloorHalfExtent + Ring * 0.5f, 0.f, 1.5f), FVector(Ring, FloorHalfExtent * 2.f, 3.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Sidewalk, FVector(Edge0 * 0.5f + CoastRoadX * 0.5f, EdgeN + Ring * 0.5f, 1.5f), FVector(EdgeN - Edge0, Ring, 3.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Sidewalk, FVector(Edge0 * 0.5f + CoastRoadX * 0.5f, -EdgeN - Ring * 0.5f, 1.5f), FVector(EdgeN - Edge0, Ring, 3.f), FRotator::ZeroRotator, false, false);

	// East of the coast road: sidewalk, coral promenade with lawn strips, low seat wall at the beach.
	const float CoastEdge = CoastRoadX + RoadWidth * 0.5f;            // 22400
	const float WalkEnd = CoastEdge + 1000.f;                        // 23400
	Add(ENCShape::Cube, NCPal::Sidewalk, FVector((CoastEdge + WalkEnd) * 0.5f, 0.f, 1.5f), FVector(WalkEnd - CoastEdge, 120000.f, 3.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Promenade, FVector((WalkEnd + BeachMinX) * 0.5f, 0.f, 2.f), FVector(BeachMinX - WalkEnd, 120000.f, 4.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Lawn, FVector(23350.f, 0.f, 4.5f), FVector(420.f, 120000.f, 3.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Lawn, FVector(24650.f, 0.f, 4.5f), FVector(420.f, 120000.f, 3.f), FRotator::ZeroRotator, false, false);
	// Visual continuation of the coast road beyond the playable floor.
	Add(ENCShape::Cube, NCPal::Asphalt, FVector(CoastRoadX, 0.f, -1.f), FVector(RoadWidth, 120000.f, 2.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Cube, NCPal::Sidewalk, FVector(CoastRoadX - RoadWidth * 0.5f - SidewalkWidth * 0.5f, 0.f, 0.5f), FVector(SidewalkWidth, 120000.f, 3.f), FRotator::ZeroRotator, false, false);

	for (float Y = -59000.f; Y < 59000.f; Y += 1800.f)
	{
		// Gaps line up with the cross streets as beach access paths.
		if (IsNearRoadCenter(Y, 900.f))
		{
			continue;
		}
		Add(ENCShape::Cube, NCPal::DecoWhite, FVector(BeachMinX - 100.f, Y, 20.f), FVector(160.f, 1500.f, 40.f), FRotator::ZeroRotator, false, true, true);
	}
}

void ANCCityVisual::BuildRoadMarkings()
{
	const float Half = RoadWidth * 0.5f;
	for (int32 r = 0; r < GridRoadCount; ++r)
	{
		const float C = RoadCenter(r);
		for (int32 s = 0; s < GridRoadCount - 1; ++s)
		{
			// Segment between intersections s and s+1.
			const float A = RoadCenter(s) + Half;
			const float B = RoadCenter(s + 1) - Half;
			const float Mid = (A + B) * 0.5f;
			const float Len = B - A;

			// Edge lines (white) on both carriageway edges, for north-south (X = C) and east-west (Y = C) roads.
			for (float Side : { -Half + 40.f, Half - 40.f })
			{
				Add(ENCShape::Cube, NCPal::LineWhite, FVector(C + Side, Mid, 0.6f), FVector(12.f, Len, 1.2f), FRotator::ZeroRotator, false, false);
				Add(ENCShape::Cube, NCPal::LineWhite, FVector(Mid, C + Side, 0.6f), FVector(Len, 12.f, 1.2f), FRotator::ZeroRotator, false, false);
			}

			// Double yellow centre line.
			for (float Off : { -9.f, 9.f })
			{
				Add(ENCShape::Cube, NCPal::LineYellow, FVector(C + Off, Mid, 0.6f), FVector(10.f, Len - 200.f, 1.2f), FRotator::ZeroRotator, false, false);
				Add(ENCShape::Cube, NCPal::LineYellow, FVector(Mid, C + Off, 0.6f), FVector(Len - 200.f, 10.f, 1.2f), FRotator::ZeroRotator, false, false);
			}
		}

		// Zebra crossings on all four approaches of each intersection.
		for (int32 q = 0; q < GridRoadCount; ++q)
		{
			const float X = C;
			const float Y = RoadCenter(q);
			for (int32 k = -5; k <= 5; ++k)
			{
				const float O = k * 130.f;
				Add(ENCShape::Cube, NCPal::LineWhite, FVector(X + O, Y + Half + 220.f, 0.7f), FVector(70.f, 300.f, 1.4f), FRotator::ZeroRotator, false, false, true);
				Add(ENCShape::Cube, NCPal::LineWhite, FVector(X + O, Y - Half - 220.f, 0.7f), FVector(70.f, 300.f, 1.4f), FRotator::ZeroRotator, false, false, true);
				Add(ENCShape::Cube, NCPal::LineWhite, FVector(X + Half + 220.f, Y + O, 0.7f), FVector(300.f, 70.f, 1.4f), FRotator::ZeroRotator, false, false, true);
				Add(ENCShape::Cube, NCPal::LineWhite, FVector(X - Half - 220.f, Y + O, 0.7f), FVector(300.f, 70.f, 1.4f), FRotator::ZeroRotator, false, false, true);
			}
		}
	}
}

void ANCCityVisual::BuildBlocks()
{
	for (int32 bx = 0; bx < GridRoadCount - 1; ++bx)
	{
		for (int32 by = 0; by < GridRoadCount - 1; ++by)
		{
			BuildBlock(bx, by);
		}
	}
}

void ANCCityVisual::BuildBlock(int32 BlockX, int32 BlockY)
{
	FRandomStream Rng(Seed + BlockX * 7919 + BlockY * 104729);

	const float CX = RoadCenter(BlockX) + GridSpacing * 0.5f;
	const float CY = RoadCenter(BlockY) + GridSpacing * 0.5f;
	// Buildable square: facades sit 1600cm from the road centres (>= 1200 required).
	const float B = GridSpacing * 0.5f - RoadWidth * 0.5f - SidewalkWidth;
	const bool bCoast = BlockX == GridRoadCount - 2;
	const bool bDowntown = CX < -6500.f && CY > -3000.f;
	const bool bDecoDistrict = bCoast || CX > 0.f;
	const float StripDepth = Rng.FRandRange(1300.f, 1700.f);

	// Side 0 = east, 1 = west, 2 = north, 3 = south.
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const bool bEW = Side < 2;
		const FVector2D Normal = Side == 0 ? FVector2D(1, 0) : Side == 1 ? FVector2D(-1, 0) : Side == 2 ? FVector2D(0, 1) : FVector2D(0, -1);
		const FVector2D Tangent = bEW ? FVector2D(0, 1) : FVector2D(1, 0);
		const FVector2D FacadeLine = FVector2D(CX, CY) + Normal * B;
		const float SpanMin = bEW ? -B + StripDepth : -B;
		const float SpanMax = bEW ? B - StripDepth : B;
		const bool bHotelFront = bCoast && Side == 0;

		float Pos = SpanMin;
		while (Pos < SpanMax - 300.f)
		{
			float W = bHotelFront ? Rng.FRandRange(1100.f, 1600.f) : Rng.FRandRange(850.f, 1400.f);
			if (SpanMax - (Pos + W) < 700.f)
			{
				W = SpanMax - Pos;
			}

			FLot Lot;
			Lot.Tangent = Tangent;
			Lot.Normal = Normal;
			Lot.Width = W - 30.f;
			Lot.Facade = FacadeLine + Tangent * (Pos + W * 0.5f);
			// Hotels leave a 900cm service/garden gap in front of the west strip.
			Lot.Depth = bHotelFront ? 2.f * B - StripDepth - 900.f : StripDepth;

			const bool bDeco = bHotelFront || (bDecoDistrict && Rng.FRand() < 0.55f);
			if (bDeco)
			{
				const int32 Floors = bHotelFront ? Rng.RandRange(3, 6) : Rng.RandRange(2, 4);
				Lot.Height = 420.f + Floors * 330.f;
				BuildDecoHotel(Lot, Rng);
			}
			else
			{
				const int32 Floors = bDowntown ? Rng.RandRange(3, 5) : Rng.RandRange(1, 3);
				Lot.Height = 420.f + Floors * 320.f;
				BuildStorefront(Lot, Rng);
			}
			Pos += W;
		}
	}

	// Courtyard: towers downtown / inland, pool decks behind the coastal hotels.
	const FVector2D Center(CX, CY);
	if (bDowntown)
	{
		BuildTower(Center, Rng.FRandRange(1500.f, 2000.f), Rng.FRandRange(6500.f, 11000.f), Rng, true);
	}
	else if (!bCoast && Rng.FRand() < 0.3f)
	{
		BuildTower(Center, Rng.FRandRange(1400.f, 1800.f), Rng.FRandRange(3200.f, 5200.f), Rng, true);
	}
	else if (bCoast)
	{
		const float GapX = CX - B + StripDepth + 450.f;
		AddPalm(FVector(GapX, CY - 700.f, 0.f), 800.f, Rng, true);
		AddPalm(FVector(GapX, CY + 700.f, 0.f), 900.f, Rng, true);
	}
	else
	{
		AddPalm(FVector(CX, CY, 0.f), Rng.FRandRange(700.f, 1000.f), Rng, true);
	}
}

void ANCCityVisual::BuildDecoHotel(const FLot& Lot, FRandomStream& Rng)
{
	const float W = Lot.Width;
	const float D = Lot.Depth;
	const float H = Lot.Height;
	const int32 Body = NCPal::Pick(NCPal::DecoColors, Rng);
	const int32 Trim = Body == NCPal::DecoWhite ? NCPal::Pick(NCPal::DecoColors, Rng) : NCPal::DecoWhite;
	const int32 Neon = NCPal::Pick(NCPal::Neons, Rng);

	// Massing (the only colliding parts).
	AddLotBox(Lot, Body, 0.f, -D * 0.5f, H * 0.5f, W, D, H, true);
	const bool bRounded = W > 1100.f && Rng.FRand() < 0.45f;
	if (bRounded)
	{
		const float R = 170.f;
		for (float S : { -1.f, 1.f })
		{
			AddLotBox(Lot, Trim, S * (W * 0.5f - R), -R, H * 0.5f + 30.f, 2.f * R, 2.f * R, H + 60.f, true, true, ENCShape::Cylinder);
		}
	}

	// Ground floor: lit lobby glazing between pilasters, a deep canopy with a neon edge.
	AddLotBox(Lot, NCPal::GlassWarm, 0.f, 3.f, 200.f, W - 160.f, 8.f, 300.f, false, false);
	for (float S : { -1.f, 1.f })
	{
		AddLotBox(Lot, Trim, S * (W * 0.5f - 40.f), 7.f, 210.f, 80.f, 14.f, 420.f, false, false);
	}
	AddLotBox(Lot, Trim, 0.f, 110.f, 432.f, W - 60.f, 220.f, 18.f);
	AddLotBox(Lot, Neon, 0.f, 221.f, 424.f, W - 60.f, 6.f, 6.f, false, false);

	// Central vertical fin carrying a neon blade sign above the roofline.
	const bool bFin = Rng.FRand() < 0.75f;
	const float FinTop = H + Rng.FRandRange(300.f, 750.f);
	if (bFin)
	{
		const float FinH = FinTop - 445.f;
		AddLotBox(Lot, Trim, 0.f, 40.f, 445.f + FinH * 0.5f, 170.f, 80.f, FinH);
		for (float S : { -1.f, 1.f })
		{
			AddLotBox(Lot, Neon, S * 90.f, 50.f, 445.f + FinH * 0.55f, 10.f, 50.f, FinH * 0.8f, false, false);
		}
		AddLotBox(Lot, Neon, 0.f, 82.f, FinTop - 120.f, 120.f, 6.f, 120.f, false, false);
	}

	// Upper floors: eyebrow ledges and punched windows.
	const int32 Floors = FMath::Max(0, FMath::RoundToInt((H - 420.f) / 330.f));
	const int32 Cols = FMath::Max(2, FMath::FloorToInt(W / 280.f));
	const float Step = (W - 200.f) / Cols;
	for (int32 f = 0; f < Floors; ++f)
	{
		const float Z0 = 420.f + f * 330.f;
		AddLotBox(Lot, Trim, 0.f, 28.f, Z0 + 305.f, W - 30.f, 56.f, 14.f);
		for (int32 c = 0; c < Cols; ++c)
		{
			const float A = -W * 0.5f + 100.f + Step * (c + 0.5f);
			if (bFin && FMath::Abs(A) < 150.f)
			{
				continue;
			}
			const int32 Glass = Rng.FRand() < 0.12f ? NCPal::GlassWarm : NCPal::GlassDark;
			AddLotBox(Lot, Glass, A, 3.f, Z0 + 150.f, FMath::Min(170.f, Step - 70.f), 8.f, 180.f, false, false);
		}
	}

	// Stepped roof parapet, occasionally traced in neon.
	AddLotBox(Lot, Trim, 0.f, -20.f, H + 40.f, W, 40.f, 80.f);
	if (Rng.FRand() < 0.6f)
	{
		AddLotBox(Lot, Body, 0.f, -D * 0.3f, H + 130.f, W * 0.5f, D * 0.35f, 260.f);
		AddLotBox(Lot, Trim, 0.f, -D * 0.3f, H + 300.f, W * 0.26f, D * 0.2f, 120.f);
	}
	if (Rng.FRand() < 0.5f)
	{
		AddLotBox(Lot, Neon, 0.f, 2.f, H + 84.f, W, 6.f, 6.f, false, false);
	}
}

void ANCCityVisual::BuildStorefront(const FLot& Lot, FRandomStream& Rng)
{
	const float W = Lot.Width;
	const float D = Lot.Depth;
	const float H = Lot.Height;
	static const int32 Bodies[] = { NCPal::Stucco, NCPal::Terracotta, NCPal::DecoPeach, NCPal::DecoWhite, NCPal::Concrete, NCPal::DecoLemon };
	const int32 Body = NCPal::Pick(Bodies, Rng);
	const int32 Canvas = NCPal::Pick(NCPal::Canvases, Rng);

	AddLotBox(Lot, Body, 0.f, -D * 0.5f, H * 0.5f, W, D, H, true);

	// Shop window, door, striped-colour awning with valance, and a sign band.
	const int32 ShopGlass = Rng.FRand() < 0.7f ? NCPal::GlassWarm : NCPal::GlassDark;
	AddLotBox(Lot, ShopGlass, 0.f, 3.f, 180.f, W - 120.f, 8.f, 270.f, false, false);
	AddLotBox(Lot, NCPal::GlassDark, Rng.FRandRange(-W * 0.3f, W * 0.3f), 6.f, 125.f, 120.f, 10.f, 240.f, false, false);
	AddLotBox(Lot, Canvas, 0.f, 90.f, 345.f, W - 100.f, 180.f, 12.f);
	AddLotBox(Lot, Canvas, 0.f, 178.f, 322.f, W - 100.f, 6.f, 50.f, false, false);
	const int32 Sign = Rng.FRand() < 0.45f ? NCPal::Pick(NCPal::Neons, Rng) : NCPal::CanvasCream;
	AddLotBox(Lot, Sign, 0.f, 8.f, 405.f, W * Rng.FRandRange(0.45f, 0.75f), 12.f, 70.f, false, false);

	const int32 Floors = FMath::Max(0, FMath::RoundToInt((H - 420.f) / 320.f));
	const int32 Cols = FMath::Max(2, FMath::FloorToInt(W / 300.f));
	const float Step = (W - 160.f) / Cols;
	for (int32 f = 0; f < Floors; ++f)
	{
		const float Z0 = 440.f + f * 320.f;
		for (int32 c = 0; c < Cols; ++c)
		{
			const float A = -W * 0.5f + 80.f + Step * (c + 0.5f);
			const int32 Glass = Rng.FRand() < 0.1f ? NCPal::GlassWarm : NCPal::GlassDark;
			AddLotBox(Lot, Glass, A, 3.f, Z0 + 140.f, Step - 90.f, 8.f, 170.f, false, false);
		}
	}

	AddLotBox(Lot, NCPal::DecoWhite, 0.f, 10.f, H + 15.f, W + 10.f, 40.f, 30.f);
	if (Rng.FRand() < 0.5f)
	{
		AddLotBox(Lot, NCPal::Metal, Rng.FRandRange(-W * 0.25f, W * 0.25f), -D * 0.5f, H + 70.f, 160.f, 220.f, 140.f, false, true);
	}
}

void ANCCityVisual::BuildTower(const FVector2D& Center, float Size, float Height, FRandomStream& Rng, bool bCollide)
{
	const int32 Neon = NCPal::Pick(NCPal::Neons, Rng);
	Add(ENCShape::Cube, NCPal::GlassTower, FVector(Center.X, Center.Y, Height * 0.5f), FVector(Size, Size, Height), FRotator::ZeroRotator, bCollide, true);

	// White balcony slabs every two floors wrap all four sides with a single instance.
	for (float Z = 640.f; Z < Height - 100.f; Z += 640.f)
	{
		Add(ENCShape::Cube, NCPal::DecoWhite, FVector(Center.X, Center.Y, Z), FVector(Size + 70.f, Size + 70.f, 28.f));
	}

	// Crown with a neon halo and a red aviation light.
	Add(ENCShape::Cube, NCPal::DecoWhite, FVector(Center.X, Center.Y, Height + 150.f), FVector(Size * 0.7f, Size * 0.7f, 300.f));
	Add(ENCShape::Cube, Neon, FVector(Center.X, Center.Y, Height + 6.f), FVector(Size + 80.f, Size + 80.f, 10.f), FRotator::ZeroRotator, false, false);
	Add(ENCShape::Sphere, NCPal::NeonPink, FVector(Center.X, Center.Y, Height + 330.f), FVector(60.f), FRotator::ZeroRotator, false, false);
}

void ANCCityVisual::AddPalm(const FVector& Base, float Height, FRandomStream& Rng, bool bCollide)
{
	const FRotator Lean(Rng.FRandRange(-6.f, 6.f), 0.f, Rng.FRandRange(-6.f, 6.f));
	Add(ENCShape::Cylinder, NCPal::Trunk, Base + Lean.RotateVector(FVector(0.f, 0.f, Height * 0.5f)), FVector(36.f, 36.f, Height), Lean, bCollide, true);
	const FVector Top = Base + Lean.RotateVector(FVector(0.f, 0.f, Height));
	Add(ENCShape::Cylinder, NCPal::Trunk, Base + FVector(0.f, 0.f, 40.f), FVector(52.f, 52.f, 80.f), FRotator::ZeroRotator, false, true, true);
	Add(ENCShape::Sphere, NCPal::Wood, Top + FVector(0.f, 0.f, -20.f), FVector(80.f, 80.f, 70.f), FRotator::ZeroRotator, false, true, true);

	const float Spin = Rng.FRandRange(0.f, 360.f);
	for (int32 k = 0; k < 7; ++k)
	{
		const FRotator R(-18.f - Rng.FRandRange(0.f, 10.f), Spin + k * (360.f / 7.f), 0.f);
		Add(ENCShape::Sphere, (k & 1) ? NCPal::Frond : NCPal::FrondDark, Top + R.RotateVector(FVector(170.f, 0.f, 0.f)), FVector(360.f, 75.f, 12.f), R, false, true);
	}
	for (int32 k = 0; k < 5; ++k)
	{
		const FRotator R(-52.f, Spin + 25.f + k * 72.f, 0.f);
		Add(ENCShape::Sphere, NCPal::FrondDark, Top + R.RotateVector(FVector(230.f, 0.f, 0.f)) + FVector(0.f, 0.f, 40.f), FVector(300.f, 60.f, 10.f), R, false, true, true);
	}
}

void ANCCityVisual::AddLamp(const FVector& Base, bool bCollide)
{
	Add(ENCShape::Cylinder, NCPal::Metal, Base + FVector(0.f, 0.f, 300.f), FVector(14.f, 14.f, 600.f), FRotator::ZeroRotator, bCollide, true, true);
	Add(ENCShape::Cylinder, NCPal::Metal, Base + FVector(0.f, 0.f, 560.f), FVector(26.f, 26.f, 30.f), FRotator::ZeroRotator, false, true, true);
	Add(ENCShape::Sphere, NCPal::LampGlow, Base + FVector(0.f, 0.f, 610.f), FVector(48.f), FRotator::ZeroRotator, false, false, true);
}

void ANCCityVisual::BuildStreetscape()
{
	FRandomStream Rng(Seed * 31 + 7);
	const float Offset = RoadWidth * 0.5f + 450.f; // 1250 from the road centre, inside the sidewalk band

	for (int32 r = 0; r < GridRoadCount; ++r)
	{
		const float C = RoadCenter(r);
		if (r == GridRoadCount - 1)
		{
			// Coast road: palm-lined west curb (corridor at x=20600 stays clear), lamps on the sea side.
			for (float Y = -59400.f; Y <= 59400.f; Y += 1800.f)
			{
				if (FMath::Abs(Y) < CityHalfExtent && IsNearRoadCenter(Y, 1500.f))
				{
					continue;
				}
				AddPalm(FVector(C - RoadWidth * 0.5f - 550.f, Y, 0.f), Rng.FRandRange(820.f, 1080.f), Rng, true);
				if (FMath::Abs(Y) < 40000.f)
				{
					AddLamp(FVector(CoastRoadX + RoadWidth * 0.5f + 70.f, Y + 900.f, 0.f), false);
				}
			}
			continue;
		}

		for (float Y = -22800.f; Y <= 22800.f; Y += 2400.f)
		{
			if (IsNearRoadCenter(Y, 1700.f))
			{
				continue;
			}
			for (float S : { -1.f, 1.f })
			{
				AddPalm(FVector(C + S * Offset, Y, 0.f), Rng.FRandRange(700.f, 980.f), Rng, true);
			}
		}

		// East-west streets get lamps only, west of the coast road.
		for (float X = -22800.f; X <= 19800.f; X += 3600.f)
		{
			if (IsNearRoadCenter(X, 1700.f))
			{
				continue;
			}
			for (float S : { -1.f, 1.f })
			{
				AddLamp(FVector(X, C + S * Offset, 0.f), true);
			}
		}
	}
}

void ANCCityVisual::BuildWaterfront()
{
	FRandomStream Rng(Seed * 17 + 3);

	// Double palm row on the promenade lawns.
	for (float Y = -59000.f; Y <= 59000.f; Y += 1500.f)
	{
		AddPalm(FVector(23350.f, Y + Rng.FRandRange(-150.f, 150.f), 0.f), Rng.FRandRange(850.f, 1150.f), Rng, true);
		AddPalm(FVector(24650.f, Y + 750.f + Rng.FRandRange(-150.f, 150.f), 0.f), Rng.FRandRange(900.f, 1250.f), Rng, true);
	}

	// Benches facing the sea between the palm rows.
	for (float Y = -29250.f; Y <= 29250.f; Y += 1500.f)
	{
		if (IsNearRoadCenter(Y, 2200.f))
		{
			continue;
		}
		const float X = 24000.f;
		Add(ENCShape::Cube, NCPal::Wood, FVector(X, Y, 45.f), FVector(60.f, 180.f, 10.f), FRotator::ZeroRotator, false, true, true);
		Add(ENCShape::Cube, NCPal::Wood, FVector(X - 30.f, Y, 82.f), FVector(8.f, 180.f, 44.f), FRotator::ZeroRotator, false, true, true);
		for (float S : { -1.f, 1.f })
		{
			Add(ENCShape::Cube, NCPal::Metal, FVector(X, Y + S * 75.f, 22.f), FVector(56.f, 8.f, 44.f), FRotator::ZeroRotator, false, true, true);
		}
	}

	// Cafe terraces with umbrellas beside each beach access path.
	for (int32 i = 0; i < GridRoadCount; ++i)
	{
		for (float S : { -1.f, 1.f })
		{
			const float BaseY = RoadCenter(i) + S * 1300.f;
			const int32 Canvas = NCPal::Pick(NCPal::Canvases, Rng);
			for (int32 t = 0; t < 3; ++t)
			{
				const FVector P(23800.f + t * 420.f, BaseY + Rng.FRandRange(-200.f, 200.f), 0.f);
				Add(ENCShape::Cylinder, NCPal::DecoWhite, P + FVector(0.f, 0.f, 37.f), FVector(80.f, 80.f, 74.f), FRotator::ZeroRotator, false, true, true);
				Add(ENCShape::Cylinder, NCPal::Metal, P + FVector(0.f, 0.f, 130.f), FVector(6.f, 6.f, 260.f), FRotator::ZeroRotator, false, true, true);
				Add(ENCShape::Cone, Canvas, P + FVector(0.f, 0.f, 262.f), FVector(270.f, 270.f, 55.f), FRotator::ZeroRotator, false, true, true);
				for (float C : { -1.f, 1.f })
				{
					Add(ENCShape::Cube, NCPal::Metal, P + FVector(0.f, C * 75.f, 23.f), FVector(42.f, 42.f, 46.f), FRotator::ZeroRotator, false, true, true);
				}
			}
		}
	}

	// Pastel lifeguard huts on stilts, a signature of the beach.
	static const float TowerY[] = { -18000.f, -9000.f, 6500.f, 15500.f, 24500.f, 36000.f, -32000.f };
	for (float Y : TowerY)
	{
		const float X = 27600.f;
		const int32 Body = NCPal::Pick(NCPal::DecoColors, Rng);
		for (float SX : { -1.f, 1.f })
		{
			for (float SY : { -1.f, 1.f })
			{
				Add(ENCShape::Cylinder, NCPal::DecoWhite, FVector(X + SX * 120.f, Y + SY * 120.f, 115.f), FVector(14.f, 14.f, 230.f), FRotator::ZeroRotator, false, true, true);
			}
		}
		Add(ENCShape::Cube, NCPal::DecoWhite, FVector(X, Y, 235.f), FVector(330.f, 330.f, 18.f), FRotator::ZeroRotator, false, true);
		Add(ENCShape::Cube, Body, FVector(X, Y, 355.f), FVector(240.f, 240.f, 220.f), FRotator::ZeroRotator, false, true);
		Add(ENCShape::Cube, NCPal::GlassDark, FVector(X + 121.f, Y, 380.f), FVector(4.f, 170.f, 90.f), FRotator::ZeroRotator, false, false);
		Add(ENCShape::Cube, NCPal::DecoWhite, FVector(X, Y, 478.f), FVector(300.f, 300.f, 24.f), FRotator::ZeroRotator, false, true);
		Add(ENCShape::Cube, NCPal::Wood, FVector(X - 360.f, Y, 115.f), FVector(480.f, 90.f, 10.f), FRotator(27.f, 0.f, 0.f), false, true, true);
		Add(ENCShape::Cylinder, NCPal::Metal, FVector(X, Y, 560.f), FVector(5.f, 5.f, 140.f), FRotator::ZeroRotator, false, false, true);
		Add(ENCShape::Cube, NCPal::CanvasCoral, FVector(X, Y + 30.f, 605.f), FVector(4.f, 60.f, 36.f), FRotator::ZeroRotator, false, false, true);
	}

	// Scattered beach umbrellas and towels.
	for (int32 i = 0; i < 70; ++i)
	{
		const FVector P(Rng.FRandRange(26400.f, 30000.f), Rng.FRandRange(-26000.f, 26000.f), 0.f);
		const int32 Canvas = NCPal::Pick(NCPal::Canvases, Rng);
		const FRotator Tilt(Rng.FRandRange(-8.f, 8.f), 0.f, Rng.FRandRange(-8.f, 8.f));
		Add(ENCShape::Cylinder, NCPal::DecoWhite, P + Tilt.RotateVector(FVector(0.f, 0.f, 110.f)), FVector(5.f, 5.f, 220.f), Tilt, false, true, true);
		Add(ENCShape::Cone, Canvas, P + Tilt.RotateVector(FVector(0.f, 0.f, 215.f)), FVector(240.f, 240.f, 45.f), Tilt, false, true, true);
		Add(ENCShape::Cube, NCPal::Pick(NCPal::Canvases, Rng), P + FVector(120.f, 60.f, 0.6f), FVector(80.f, 170.f, 1.2f), FRotator(0.f, Rng.FRandRange(-20.f, 20.f), 0.f), false, false, true);
	}

	// Distant sailboats.
	for (int32 i = 0; i < 7; ++i)
	{
		const FVector P(Rng.FRandRange(40000.f, 90000.f), Rng.FRandRange(-40000.f, 60000.f), SeaLevelZ);
		const float Yaw = Rng.FRandRange(-30.f, 30.f);
		const FRotator R(0.f, Yaw, 0.f);
		Add(ENCShape::Cube, NCPal::DecoWhite, P + FVector(0.f, 0.f, 40.f), FVector(900.f, 260.f, 120.f), R, false, false);
		Add(ENCShape::Cylinder, NCPal::Metal, P + FVector(0.f, 0.f, 700.f), FVector(16.f, 16.f, 1200.f), R, false, false);
		Add(ENCShape::Cone, NCPal::CanvasCream, P + R.RotateVector(FVector(-180.f, 0.f, 0.f)) + FVector(0.f, 0.f, 680.f), FVector(420.f, 10.f, 1050.f), R, false, false);
	}
}

void ANCCityVisual::BuildDistant()
{
	FRandomStream Rng(Seed * 13 + 11);

	// The deco strip continues north and south of the playable city along the coast road.
	for (float Sign : { -1.f, 1.f })
	{
		float Y = 25600.f;
		while (Y < 62000.f)
		{
			const float W = Rng.FRandRange(1100.f, 1600.f);
			FLot Lot;
			Lot.Normal = FVector2D(1, 0);
			Lot.Tangent = FVector2D(0, 1);
			Lot.Width = W - 30.f;
			Lot.Depth = 1800.f;
			Lot.Facade = FVector2D(20000.f, Sign * (Y + W * 0.5f));
			Lot.Height = 420.f + Rng.RandRange(3, 7) * 330.f;
			BuildDecoHotel(Lot, Rng);
			Y += W;
		}
		// A few taller condo towers behind the strip.
		for (int32 i = 0; i < 6; ++i)
		{
			const FVector2D C(Rng.FRandRange(9000.f, 16000.f), Sign * Rng.FRandRange(30000.f, 60000.f));
			BuildTower(C, Rng.FRandRange(1500.f, 2200.f), Rng.FRandRange(4500.f, 9000.f), Rng, false);
		}
	}

	// Downtown skyline across the bay to the west: low-detail silhouettes, no shadows.
	for (int32 i = 0; i < 30; ++i)
	{
		const FVector2D C(Rng.FRandRange(-75000.f, -48000.f), Rng.FRandRange(-30000.f, 40000.f));
		const float S = Rng.FRandRange(1800.f, 3200.f);
		const float H = Rng.FRandRange(6000.f, 22000.f);
		Add(ENCShape::Cube, Rng.FRand() < 0.6f ? NCPal::DistantGlass : NCPal::Distant, FVector(C.X, C.Y, H * 0.5f), FVector(S, S * Rng.FRandRange(0.7f, 1.3f), H), FRotator::ZeroRotator, false, false);
		Add(ENCShape::Cube, NCPal::DecoWhite, FVector(C.X, C.Y, H + 200.f), FVector(S * 0.6f, S * 0.6f, 400.f), FRotator::ZeroRotator, false, false);
		Add(ENCShape::Sphere, NCPal::NeonPink, FVector(C.X, C.Y, H + 460.f), FVector(120.f), FRotator::ZeroRotator, false, false);
	}
}
