#include "NCCityVisual.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
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
		SignBoard,
		Count
	};

	struct FDef { FLinearColor Color; float Roughness; float Metallic; float Emissive; };

	// Linear-space colours at real painted-render / paving albedos. Palettes with an MI_CoastPBR set only use
	// these when that set is missing; nothing non-emissive is above 0.72.
	static const FDef Defs[Count] =
	{
		{ FLinearColor(0.100f, 0.100f, 0.105f), 0.80f, 0.f, 0.f },   // Asphalt (worn, sun-bleached)
		{ FLinearColor(0.33f, 0.32f, 0.30f), 0.80f, 0.f, 0.f },      // Sidewalk
		{ FLinearColor(0.42f, 0.33f, 0.27f), 0.75f, 0.f, 0.f },      // Promenade (warm pavers)
		{ FLinearColor(0.09f, 0.13f, 0.05f), 0.90f, 0.f, 0.f },      // Lawn
		{ FLinearColor(0.52f, 0.43f, 0.31f), 0.92f, 0.f, 0.f },      // Sand
		{ FLinearColor(0.25f, 0.20f, 0.14f), 0.45f, 0.f, 0.f },      // WetSand
		{ FLinearColor(0.010f, 0.075f, 0.095f), 0.08f, 0.1f, 0.f },  // Water
		{ FLinearColor(0.85f, 0.88f, 0.86f), 0.60f, 0.f, 0.15f },    // Foam
		{ FLinearColor(0.62f, 0.62f, 0.60f), 0.55f, 0.f, 0.f },      // LineWhite (thermoplastic)
		{ FLinearColor(0.60f, 0.40f, 0.06f), 0.55f, 0.f, 0.f },      // LineYellow
		{ FLinearColor(0.66f, 0.64f, 0.60f), 0.75f, 0.f, 0.f },      // DecoWhite
		{ FLinearColor(0.50f, 0.60f, 0.53f), 0.75f, 0.f, 0.f },      // DecoMint
		{ FLinearColor(0.68f, 0.52f, 0.50f), 0.75f, 0.f, 0.f },      // DecoPink
		{ FLinearColor(0.70f, 0.63f, 0.45f), 0.75f, 0.f, 0.f },      // DecoLemon
		{ FLinearColor(0.55f, 0.52f, 0.62f), 0.75f, 0.f, 0.f },      // DecoLavender
		{ FLinearColor(0.70f, 0.55f, 0.44f), 0.75f, 0.f, 0.f },      // DecoPeach
		{ FLinearColor(0.46f, 0.58f, 0.60f), 0.75f, 0.f, 0.f },      // DecoAqua
		{ FLinearColor(0.56f, 0.50f, 0.42f), 0.80f, 0.f, 0.f },      // Stucco
		{ FLinearColor(0.34f, 0.20f, 0.14f), 0.75f, 0.f, 0.f },      // Terracotta
		{ FLinearColor(0.030f, 0.030f, 0.030f), 0.05f, 0.f, 0.f },   // GlassDark (reflections give the depth)
		{ FLinearColor(0.30f, 0.18f, 0.08f), 0.15f, 0.f, 0.6f },     // GlassWarm (lit interiors)
		{ FLinearColor(0.05f, 0.11f, 0.14f), 0.06f, 0.5f, 0.f },     // GlassTower
		{ FLinearColor(0.28f, 0.27f, 0.26f), 0.85f, 0.f, 0.f },      // Concrete
		{ FLinearColor(0.30f, 0.22f, 0.15f), 0.90f, 0.f, 0.f },      // Trunk
		{ FLinearColor(0.08f, 0.22f, 0.05f), 0.70f, 0.f, 0.f },      // Frond
		{ FLinearColor(0.05f, 0.14f, 0.04f), 0.70f, 0.f, 0.f },      // FrondDark
		{ FLinearColor(0.18f, 0.18f, 0.18f), 0.50f, 0.f, 0.f },      // Metal (painted street metal)
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
		{ FLinearColor(0.016f, 0.020f, 0.028f), 0.35f, 0.f, 0.f },   // SignBoard (dark enamel behind lettering)
	};

	static const int32 DecoColors[] = { DecoWhite, DecoMint, DecoPink, DecoLemon, DecoLavender, DecoPeach, DecoAqua };
	static const int32 Neons[] = { NeonPink, NeonAqua, NeonAmber };
	static const int32 Canvases[] = { CanvasCoral, CanvasCream, CanvasTeal };

	template <int32 N>
	static int32 Pick(const int32 (&Arr)[N], FRandomStream& Rng) { return Arr[Rng.RandRange(0, N - 1)]; }

	/** M_Coast surface controls: world-space grain, per-instance tint jitter, contact darkening near the ground. */
	struct FSurface { float Grain; float Variation; float Occlusion; };

	static FSurface SurfaceFor(int32 P)
	{
		switch (P)
		{
		case Asphalt:   return { 1.00f, 0.00f, 0.f };
		case Sidewalk:  return { 0.55f, 0.00f, 0.f };
		case Promenade: return { 0.60f, 0.00f, 0.f };
		case Concrete:  return { 0.50f, 0.04f, 0.f };
		case Lawn:      return { 0.80f, 0.00f, 0.f };
		case Sand:      return { 0.70f, 0.00f, 0.f };
		case WetSand:   return { 0.45f, 0.00f, 0.f };
		case Water:     return { 0.25f, 0.00f, 0.f };
		case Trunk: case Wood: return { 0.40f, 0.06f, 0.f };
		case DecoWhite: case DecoMint: case DecoPink: case DecoLemon: case DecoLavender: case DecoPeach: case DecoAqua:
		case Stucco: case Terracotta:
			return { 0.12f, 0.07f, 1.f };
		case GlassDark: case GlassWarm: return { 0.f, 0.12f, 0.f };
		case GlassTower: case Distant: case DistantGlass: return { 0.f, 0.06f, 0.f };
		case Frond: case FrondDark: return { 0.f, 0.14f, 0.f };
		case CanvasCoral: case CanvasCream: case CanvasTeal: return { 0.f, 0.05f, 0.f };
		default:        return { 0.f, 0.f, 0.f };
		}
	}

	static float Smooth01(float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); }

	/** Palettes with a real PBR set at /Game/Materials/Realism/MI_CoastPBR_<Name>. */
	struct FRealismSet { int32 Palette; const TCHAR* Name; };
	static const FRealismSet RealismSets[] =
	{
		{ Asphalt, TEXT("Asphalt") }, { Sidewalk, TEXT("Sidewalk") }, { Concrete, TEXT("Concrete") }, { Promenade, TEXT("Promenade") },
		{ Lawn, TEXT("Lawn") }, { Sand, TEXT("Sand") }, { WetSand, TEXT("WetSand") },
		{ DecoWhite, TEXT("DecoWhite") }, { DecoMint, TEXT("DecoMint") }, { DecoPink, TEXT("DecoPink") }, { DecoLemon, TEXT("DecoLemon") },
		{ DecoLavender, TEXT("DecoLavender") }, { DecoPeach, TEXT("DecoPeach") }, { DecoAqua, TEXT("DecoAqua") },
		{ Stucco, TEXT("Stucco") }, { Terracotta, TEXT("Terracotta") },
	};

	/** Blackbody sun temperature by elevation: 5600K high, 5000K at 30deg, 4000K at 12deg, 3200K at 4deg and below. */
	static float SunKelvin(float ElevDeg)
	{
		static const float Elev[] = { 4.f, 12.f, 30.f, 45.f };
		static const float Kelvin[] = { 3200.f, 4000.f, 5000.f, 5600.f };
		if (ElevDeg <= Elev[0])
		{
			return Kelvin[0];
		}
		for (int32 i = 1; i < 4; ++i)
		{
			if (ElevDeg <= Elev[i])
			{
				return FMath::Lerp(Kelvin[i - 1], Kelvin[i], (ElevDeg - Elev[i - 1]) / (Elev[i] - Elev[i - 1]));
			}
		}
		return Kelvin[3];
	}

	// Original names only; sRGB sign colours tuned to read against dark enamel boards.
	static const TCHAR* HotelNames[] = { TEXT("AZURINE"), TEXT("MARISOL"), TEXT("SOLANA"), TEXT("BELLAMAR"), TEXT("LUNA"), TEXT("PALOMA"),
		TEXT("TIDEWELL"), TEXT("SEAGLASS"), TEXT("ORCHIDEE"), TEXT("CASA ROSA"), TEXT("VERANO"), TEXT("LAGUNA") };
	static const TCHAR* ShopNames[] = { TEXT("KEY LIME CAFE"), TEXT("SALT & CITRUS"), TEXT("DUNE RECORDS"), TEXT("CABANA SUPPLY"),
		TEXT("LIDO PHARMACY"), TEXT("TANGERINE DINER"), TEXT("VELVET HOUR"), TEXT("PALMETTO BOOKS"), TEXT("MANGO & MINT"),
		TEXT("HARBOR LIGHTS"), TEXT("COASTLINE CYCLES"), TEXT("SUNDECK SURF"), TEXT("BLUE HERON BAR"), TEXT("CORAL CINEMA"),
		TEXT("NIGHT FERRY"), TEXT("ESPRESSO 19"), TEXT("TROPIC LAUNDRY"), TEXT("SEASHELL DELI") };
	static const TCHAR* TowerNames[] = { TEXT("MERIDIAN"), TEXT("BAYFRONT"), TEXT("SOLSTICE"), TEXT("CORAL ONE"), TEXT("AZURE KEY"), TEXT("THE ALCOVE") };
	static const FColor SignColors[] = { FColor(255, 92, 150), FColor(96, 240, 214), FColor(255, 176, 74), FColor(255, 238, 206) };
}

namespace
{
	constexpr int32 MaxSigns = 84;

	/** Sign choices draw from a stream keyed on the lot position, so the layout stream (and every building bound) is untouched. */
	FRandomStream NCSignStream(const FVector2D& P, int32 Seed)
	{
		const uint32 H = static_cast<uint32>(Seed) * 92821u ^ static_cast<uint32>(FMath::RoundToInt(P.X)) * 73856093u ^ static_cast<uint32>(FMath::RoundToInt(P.Y)) * 19349663u;
		return FRandomStream(static_cast<int32>(H & 0x7fffffff));
	}

	/** Asset names under /Game/PublicAssets/Models, in ENCModel order. */
	const TCHAR* const NCModelNames[] = { TEXT("SM_FacadeWindowPanel"), TEXT("SM_FacadeDoorPanel"), TEXT("SM_FacadeWall"), TEXT("SM_FacadeCornice"),
		TEXT("SM_FacadeCrown"), TEXT("SM_FacadePier"), TEXT("SM_CoastalTree"), TEXT("SM_Bench"), TEXT("SM_StreetLamp") };
	static_assert(UE_ARRAY_COUNT(NCModelNames) == static_cast<int32>(ENCModel::Count), "One asset per ENCModel");

	/** Per-mesh draw distance (cm): facades stay up across the city and the near strip, furniture drops out early. */
	constexpr float NCModelCullDistance[] = { 60000.f, 60000.f, 60000.f, 60000.f, 60000.f, 60000.f, 30000.f, 8000.f, 15000.f };
	static_assert(UE_ARRAY_COUNT(NCModelCullDistance) == static_cast<int32>(ENCModel::Count), "One cull distance per ENCModel");

	/** Yaw that turns a model's front (local -Y) towards Dir. */
	float NCYawToFace(const FVector2D& Dir)
	{
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Dir.X, -Dir.Y)));
	}
}

ANCCityVisual::ANCCityVisual()
{
	// Static root so the static HISM batches can attach; lights below are movable children.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->Mobility = EComponentMobility::Static;
	SetRootComponent(SceneRoot);

	// Warm afternoon: at the 15:30 start the sun is ~49deg up in the west-south-west (UpdateLighting snaps it at BeginPlay).
	// Light colour stays white; warmth comes from the blackbody temperature.
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(SceneRoot);
	Sun->Mobility = EComponentMobility::Movable;
	Sun->SetRelativeRotation(FRotator(-49.f, 33.f, 0.f));
	Sun->Intensity = 10.f;
	Sun->LightColor = FColor::White;
	Sun->bUseTemperature = true;
	Sun->Temperature = 5600.f;
	Sun->LightSourceAngle = 0.53f;
	Sun->ContactShadowLength = 0.02f;
	Sun->CastShadows = true;
	Sun->DynamicShadowDistanceMovableLight = 24000.f;
	Sun->SetAtmosphereSunLight(true);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->Mobility = EComponentMobility::Movable;
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->Intensity = 1.f;

	// Humid coastal Mie haze; default Rayleigh and aerial perspective scale.
	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(SceneRoot);
	Atmosphere->SetMieScatteringScale(0.0055f);
	Atmosphere->SetMieAnisotropy(0.8f);

	// Light coastal haze starting past the street; volumetric fog stays off for the 8 GB laptop budget.
	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(SceneRoot);
	HeightFog->SetRelativeLocation(FVector(0.f, 0.f, -100.f));
	HeightFog->FogDensity = 0.006f;
	HeightFog->FogHeightFalloff = 0.2f;
	HeightFog->StartDistance = 6000.f;
	HeightFog->FogMaxOpacity = 0.7f;
	HeightFog->DirectionalInscatteringExponent = 8.f;
	HeightFog->DirectionalInscatteringStartDistance = 8000.f;

	// Grade: fixed exposure in this project (auto exposure is off), so UpdateLighting drives exposure bias,
	// white balance and bloom with the clock to keep night readable rather than black.
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(SceneRoot);
	Grade->bUnbound = true;
	Grade->Priority = -1.f; // any authored volume in the level wins
	FPostProcessSettings& PP = Grade->Settings;
	PP.bOverride_AutoExposureBias = true;
	PP.bOverride_WhiteTemp = true;
	PP.bOverride_ColorSaturation = true;
	PP.bOverride_ColorContrast = true;
	PP.bOverride_BloomIntensity = true;
	PP.bOverride_VignetteIntensity = true;
	PP.bOverride_FilmGrainIntensity = true;
	// Neutral grade: realism comes from geometry, PBR and light, not a filter.
	PP.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.f);
	PP.VignetteIntensity = 0.2f;
	PP.FilmGrainIntensity = 0.f;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;

	CoastMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_Coast.M_Coast")));
	SignTextMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_CoastSignText.M_CoastSignText")));
	FallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone"))));
	ShapeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane"))));

	for (const TCHAR* Name : NCModelNames)
	{
		ModelMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/PublicAssets/Models/%s.%s"), Name, Name))));
	}
	RealismMaterials.SetNum(NCPal::Count);
	for (const NCPal::FRealismSet& Set : NCPal::RealismSets)
	{
		const FString Name = FString::Printf(TEXT("MI_CoastPBR_%s"), Set.Name);
		RealismMaterials[Set.Palette] = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(FString::Printf(TEXT("/Game/Materials/Realism/%s.%s"), *Name, *Name)));
	}
}

void ANCCityVisual::BeginPlay()
{
	Super::BeginPlay();
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;

	TimeOfDayHours = FMath::Fmod(FMath::Max(0.f, StartHour), 24.f);
	SetActorTickEnabled(bDayNightCycle);
	SignMID = nullptr;
	if (bBuildSigns)
	{
		if (UMaterialInterface* TextMat = SignTextMaterial.LoadSynchronous())
		{
			SignMID = UMaterialInstanceDynamic::Create(TextMat, this);
		}
	}

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
	UpdateLighting(0.f, true);
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

	// Imported models: keep the mesh only if its measured bounds are usable; a missing model keeps the primitive path.
	const int32 NumModels = static_cast<int32>(ENCModel::Count);
	LoadedModels.Reset();
	ModelBounds.Reset();
	PendingModels.Reset();
	PendingModels.SetNum(NumModels);
	int32 Found = 0;
	for (int32 i = 0; i < NumModels; ++i)
	{
		UStaticMesh* Mesh = ModelMeshes.IsValidIndex(i) && !ModelMeshes[i].IsNull() ? ModelMeshes[i].LoadSynchronous() : nullptr;
		const FBox Box = Mesh ? Mesh->GetBoundingBox() : FBox(ForceInit);
		if (Mesh && !(Box.IsValid && Box.GetSize().X > 1.f && Box.GetSize().Z > 1.f))
		{
			UE_LOG(LogTemp, Warning, TEXT("NCCityVisual: %s has unusable bounds %s; using primitives."), NCModelNames[i], *Box.ToString());
			Mesh = nullptr;
		}
		LoadedModels.Add(Mesh);
		ModelBounds.Add(Box);
		Found += Mesh ? 1 : 0;
	}
	UE_LOG(LogTemp, Log, TEXT("NCCityVisual: %d/%d public models loaded."), Found, NumModels);
}

bool ANCCityVisual::HasModel(ENCModel Model) const
{
	const int32 Index = static_cast<int32>(Model);
	return LoadedModels.IsValidIndex(Index) && LoadedModels[Index] != nullptr;
}

FVector3f ANCCityVisual::ModelSize(ENCModel Model) const
{
	const int32 Index = static_cast<int32>(Model);
	return ModelBounds.IsValidIndex(Index) ? FVector3f(ModelBounds[Index].GetSize()) : FVector3f::ZeroVector;
}

void ANCCityVisual::BuildPalettes()
{
	UMaterialInterface* Base = CoastMaterial.LoadSynchronous();
	if (!Base)
	{
		Base = FallbackMaterial.LoadSynchronous();
	}

	PaletteMaterials.Reset();
	int32 PBRSets = 0;
	for (int32 i = 0; i < NCPal::Count; ++i)
	{
		// Real PBR set first. It already carries its tint, maps, physical tiling, normal strength and roughness,
		// so no parameter is touched; untextured palettes keep M_Coast below.
		UMaterialInterface* PBR = RealismMaterials.IsValidIndex(i) && !RealismMaterials[i].IsNull() ? RealismMaterials[i].LoadSynchronous() : nullptr;
		if (PBR)
		{
			PaletteMaterials.Add(UMaterialInstanceDynamic::Create(PBR, this));
			++PBRSets;
			continue;
		}

		const NCPal::FDef& Def = NCPal::Defs[i];
		UMaterialInstanceDynamic* MID = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("BaseColor"), Def.Color);
			MID->SetVectorParameterValue(TEXT("Color"), Def.Color); // BasicShapeMaterial fallback
			MID->SetScalarParameterValue(TEXT("Roughness"), Def.Roughness);
			MID->SetScalarParameterValue(TEXT("Metallic"), Def.Metallic);
			MID->SetScalarParameterValue(TEXT("EmissiveStrength"), Def.Emissive);
			const NCPal::FSurface Surf = NCPal::SurfaceFor(i);
			MID->SetScalarParameterValue(TEXT("Grain"), Surf.Grain);
			MID->SetScalarParameterValue(TEXT("InstanceVariation"), Surf.Variation);
			MID->SetScalarParameterValue(TEXT("GroundOcclusion"), Surf.Occlusion);
		}
		PaletteMaterials.Add(MID);
	}
	UE_LOG(LogTemp, Log, TEXT("NCCityVisual: %d/%d palettes use MI_CoastPBR sets."), PBRSets, static_cast<int32>(UE_ARRAY_COUNT(NCPal::RealismSets)));
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
	bool bCollide, bool bShadow, bool bProp, bool bHideVisual)
{
	const FBatchKey Key{ Shape, Palette, bCollide, bShadow, bProp, bHideVisual };
	// Keep thin visual ground overlays in bounded pieces. Physics stays on the
	// original floor; every piece retains the same height and world-projected UVs.
	const bool bGroundOverlay = Palette == NCPal::Sidewalk || Palette == NCPal::Promenade ||
		Palette == NCPal::Lawn || Palette == NCPal::Asphalt;
	if (Shape == ENCShape::Cube && !bCollide && bGroundOverlay && Size.Z <= 4.f &&
		FMath::Max(Size.X, Size.Y) > 2000.f)
	{
		const bool bAlongX = Size.X >= Size.Y;
		const double Length = bAlongX ? Size.X : Size.Y;
		const int32 Count = FMath::CeilToInt(Length / 2000.0);
		const double Span = Length / Count;
		FVector PieceSize = Size;
		(bAlongX ? PieceSize.X : PieceSize.Y) = Span;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FVector Offset = FVector::ZeroVector;
			(bAlongX ? Offset.X : Offset.Y) = -Length * 0.5 + Span * (Index + 0.5);
			PendingInstances.FindOrAdd(Key).Add(FTransform(Rot, Center + Rot.RotateVector(Offset), PieceSize / 100.f));
		}
		return;
	}
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

FVector ANCCityVisual::ModelPivot(ENCModel Model) const
{
	const int32 Index = static_cast<int32>(Model);
	if (const UStaticMeshSocket* Socket = LoadedModels[Index]->FindSocket(TEXT("NCPlacementBase")))
	{
		return Socket->RelativeLocation;
	}
	// Models without a measured foot socket use the import's base origin.
	const FBox& Box = ModelBounds[Index];
	const FVector Center = Box.GetCenter();
	const FVector Extent = Box.GetExtent();
	FVector Pivot(0.f, 0.f, Box.Min.Z);
	if (FMath::Abs(Center.X) > Extent.X * 0.5f || FMath::Abs(Center.Y) > Extent.Y * 0.5f)
	{
		Pivot.X = Center.X;
		Pivot.Y = Center.Y;
	}
	return Pivot;
}

void ANCCityVisual::AddModel(ENCModel Model, const FVector& Base, const FQuat& Rot, float Scale)
{
	const int32 Index = static_cast<int32>(Model);
	if (!HasModel(Model) || !PendingModels.IsValidIndex(Index)) return;
	const FVector Pivot = ModelPivot(Model);
	const FVector S(Scale);
	PendingModels[Index].Add(FTransform(Rot, Base - Rot.RotateVector(Pivot * S), S));
}

bool ANCCityVisual::AddFacadeModule(ENCModel Model, const FLot& Wall, float Along, float Z0, float CellW, float CellH)
{
	const int32 Index = static_cast<int32>(Model);
	if (!HasModel(Model) || !PendingModels.IsValidIndex(Index))
	{
		return false;
	}
	const FBox& Box = ModelBounds[Index];
	const FVector3f Size = ModelSize(Model);
	const float Sx = CellW / Size.X;
	const float Sz = CellH / Size.Z;
	// Modules keep their proportions: at most ~12% anisotropy and 25% overall scale, otherwise the caller falls back.
	const float Aspect = Sx / Sz;
	if (Aspect < 0.88f || Aspect > 1.14f || Sx < 0.75f || Sx > 1.3f)
	{
		return false;
	}
	const FVector Scale(Sx, 0.5f * (Sx + Sz), Sz);
	const FQuat Rot(FRotator(0.f, NCYawToFace(Wall.Normal), 0.f));
	// Back face (local +Y) bottom centre sits 1cm proud of the wall, so the colliding massing never covers a recessed window.
	const FVector Anchor(Box.GetCenter().X, Box.Max.Y, Box.Min.Z);
	const FVector2D P = Wall.Facade + Wall.Tangent * Along + Wall.Normal * 1.f;
	PendingModels[Index].Add(FTransform(Rot, FVector(P.X, P.Y, Z0) - Rot.RotateVector(Anchor * Scale), Scale));
	return true;
}

float ANCCityVisual::TileFacadeRow(ENCModel Model, const FLot& Wall, float Mid, float Span, float Z, float ScaleHint, float MaxH, bool bHang)
{
	if (!HasModel(Model) || Span < 1.f)
	{
		return 0.f;
	}
	const FVector3f M = ModelSize(Model);
	const int32 Count = FMath::Max(1, FMath::RoundToInt(Span / (M.X * ScaleHint)));
	const float Cell = Span / Count;
	const float S = Cell / M.X;
	const float H = M.Z * S;
	if (S < 0.75f || S > 1.3f || H > MaxH)
	{
		return 0.f;
	}
	const float Z0 = bHang ? Z - H : Z;
	for (int32 i = 0; i < Count; ++i)
	{
		AddFacadeModule(Model, Wall, Mid - Span * 0.5f + Cell * (i + 0.5f), Z0, Cell, H);
	}
	return H;
}

bool ANCCityVisual::CladWall(const FLot& Wall, float RunMin, float RunMax, float UpperZ, int32 Floors, float Pitch, int32 BandPalette,
	EGround Ground, bool bFinGap, bool bCrown)
{
	if (!HasModel(ENCModel::FacadeWindow) || (Floors < 1 && Ground == EGround::None))
	{
		return false;
	}
	const FVector3f M = ModelSize(ENCModel::FacadeWindow);
	const float RunW = RunMax - RunMin;
	if (M.X < 50.f || M.Z < 50.f || Pitch / M.Z < 0.85f)
	{
		return false;
	}

	// One scale per wall: module height fitted to the floor pitch within +-8% (never taller than the pitch),
	// width within +-6% of that; whatever the columns leave over stays plaster, framed by piers when they fit.
	const float Sz = FMath::Min(FMath::Clamp(Pitch / M.Z, 0.92f, 1.08f), Pitch / M.Z);
	const int32 Cols = FMath::FloorToInt(RunW / (M.X * FMath::Max(0.92f, Sz * 0.94f)));
	if (Cols < 1)
	{
		return false;
	}
	const float Sx = FMath::Min3(RunW / (Cols * M.X), Sz * 1.06f, 1.08f);
	const float Cell = M.X * Sx;
	const float RowH = M.Z * Sz;
	const float Gap = Pitch - RowH;
	const float Proud = M.Y * 0.5f * (Sx + Sz) + 1.f;
	const float Mid = 0.5f * (RunMin + RunMax);
	const float Used = Cell * Cols;

	float PierW = 0.f;
	if (HasModel(ENCModel::FacadePier))
	{
		const float W = ModelSize(ENCModel::FacadePier).X * Sx;
		if (W > 1.f && (RunW - Used) * 0.5f >= W - 1.f)
		{
			PierW = W;
		}
	}
	const float Span = Used + 2.f * PierW;

	auto PlaceRow = [&](float Z0, bool bGround)
	{
		for (int32 c = 0; c < Cols; ++c)
		{
			const float A = Mid - Used * 0.5f + Cell * (c + 0.5f);
			ENCModel Model = ENCModel::FacadeWindow;
			if (bGround && Ground == EGround::Door && c == Cols / 2)
			{
				Model = ENCModel::FacadeDoor;
			}
			else if (!bGround && bFinGap && FMath::Abs(A) < Cell * 0.5f)
			{
				Model = ENCModel::FacadeWall;	// the blade-sign fin covers this column
			}
			if (Model == ENCModel::FacadeWindow || !AddFacadeModule(Model, Wall, A, Z0, Cell, RowH))
			{
				AddFacadeModule(ENCModel::FacadeWindow, Wall, A, Z0, Cell, RowH);
			}
		}
		if (PierW > 0.f)
		{
			for (float S : { -1.f, 1.f })
			{
				AddFacadeModule(ENCModel::FacadePier, Wall, Mid + S * (Used + PierW) * 0.5f, Z0, PierW, RowH);
			}
		}
	};

	if (Ground != EGround::None)
	{
		PlaceRow(0.f, true);
		// First-floor string course: cornice modules hung under UpperZ, plain plaster for any remaining spandrel.
		const float Top = UpperZ - TileFacadeRow(ENCModel::FacadeCornice, Wall, Mid, Span, UpperZ, Sx, UpperZ - RowH, true);
		if (Top - RowH > 2.f)
		{
			AddLotBox(Wall, BandPalette, Mid, Proud * 0.5f, 0.5f * (RowH + Top), Span, Proud, Top - RowH);
		}
	}
	for (int32 f = 0; f < Floors; ++f)
	{
		const float Z0 = UpperZ + f * Pitch;
		if (Gap > 2.f)
		{
			// Floor-line band where the scaled module is shorter than the pitch.
			AddLotBox(Wall, BandPalette, Mid, Proud * 0.5f, Z0 + Gap * 0.5f, Span, Proud, Gap);
		}
		PlaceRow(Z0 + Gap, false);
	}
	if (bCrown)
	{
		TileFacadeRow(ENCModel::FacadeCrown, Wall, Mid, Span, UpperZ + Floors * Pitch, Sx, 160.f, false);
	}
	return true;
}

void ANCCityVisual::CladEndWall(const FLot& Lot, float StreetMargin, float UpperZ, int32 Floors, float Pitch, int32 BandPalette)
{
	if (LotCladEnd == 0)
	{
		return;
	}
	// The massing's end face as its own wall; Along = +Depth/2 is the street corner.
	FLot End;
	End.Normal = Lot.Tangent * static_cast<float>(LotCladEnd);
	End.Tangent = Lot.Normal;
	End.Facade = Lot.Facade + Lot.Tangent * (LotCladEnd * Lot.Width * 0.5f) - Lot.Normal * (Lot.Depth * 0.5f);
	End.Width = Lot.Depth;
	End.Depth = Lot.Width;
	End.Height = Lot.Height;
	CladWall(End, -Lot.Depth * 0.5f + 40.f, Lot.Depth * 0.5f - StreetMargin, UpperZ, Floors, Pitch, BandPalette, EGround::Windows, false, true);
}

void ANCCityVisual::AddTree(const FVector& Base, const FRotator& Lean, float PalmHeight, float MaxCanopyRadius)
{
	const FVector3f Size = ModelSize(ENCModel::CoastalTree);
	// Own stream keyed on the spot, so the layout stream is untouched.
	FRandomStream Var = NCSignStream(FVector2D(Base.X, Base.Y), Seed + 4271);
	const float Yaw = Var.FRandRange(0.f, 360.f);
	// Modest natural street trees: 540-700cm from the old palm heights, +-8% per tree, uniform scale only (<= 2x source).
	const float Target = FMath::Clamp(PalmHeight * 0.62f, 540.f, 700.f) * Var.FRandRange(0.92f, 1.08f);
	float S = FMath::Clamp(FMath::Max(Target, 420.f) / Size.Z, 0.25f, 2.f);
	const FQuat TreeRotation = FQuat(Lean) * FQuat(FRotator(0.f, Yaw, 0.f));
	if (MaxCanopyRadius > 0.f)
	{
		// Measure from the actual trunk foot, including rotation/lean. Clearance
		// takes precedence over target height so crowns cannot grow back into walls.
		const FBox& Box = ModelBounds[static_cast<int32>(ENCModel::CoastalTree)];
		const FVector Pivot = ModelPivot(ENCModel::CoastalTree);
		float Radius = 1.f;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point(Corner & 1 ? Box.Max.X : Box.Min.X,
				Corner & 2 ? Box.Max.Y : Box.Min.Y, Corner & 4 ? Box.Max.Z : Box.Min.Z);
			Radius = FMath::Max(Radius, static_cast<float>(TreeRotation.RotateVector(Point - Pivot).Size2D()));
		}
		S = FMath::Min(S, MaxCanopyRadius / Radius);
	}
	// Same lean as the hidden collision trunk, so the visible trunk follows it.
	AddModel(ENCModel::CoastalTree, Base, TreeRotation, S);
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
		if (Key.bHidden)
		{
			// Collision-only: the imported model draws in its place; collision transform is unchanged.
			HISM->SetVisibility(false);
			HISM->SetHiddenInGame(true);
		}
		HISM->RegisterComponent();
		HISM->AddInstances(Pair.Value, false);
		AddInstanceComponent(HISM);
		Batches.Add(HISM);
	}
	PendingInstances.Reset();

	// One HISM per imported mesh. Its slots and materials stay exactly as imported (no SetMaterial), visual only.
	for (int32 i = 0; i < PendingModels.Num(); ++i)
	{
		UStaticMesh* Mesh = HasModel(static_cast<ENCModel>(i)) ? LoadedModels[i].Get() : nullptr;
		if (!Mesh || PendingModels[i].Num() == 0)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		HISM->SetupAttachment(SceneRoot);
		HISM->SetMobility(EComponentMobility::Static);
		HISM->SetStaticMesh(Mesh);
		HISM->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HISM->SetGenerateOverlapEvents(false);
		HISM->SetCanEverAffectNavigation(false);
		HISM->SetCastShadow(true);
		HISM->SetCullDistances(0, static_cast<int32>(NCModelCullDistance[i]));
		HISM->RegisterComponent();
		HISM->AddInstances(PendingModels[i], false);
		AddInstanceComponent(HISM);
		Batches.Add(HISM);
	}
	for (TArray<FTransform>& Pending : PendingModels)
	{
		Pending.Reset();
	}
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

			LotSignChance = !bBuildSigns ? 0.f : bHotelFront ? 0.85f : bDecoDistrict ? 0.13f : 0.06f;
			// Coast blocks wear imported facade modules on the coast front and the cross-street returns; the
			// corner lot of each return also gets its end wall, which faces the coast road.
			bLotCladFront = bCoast && Side != 1;
			LotCladEnd = bCoast && Side >= 2 && Pos + W >= SpanMax - 1.f ? 1 : 0;
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
	bLotCladFront = false;
	LotCladEnd = 0;

	// Courtyard: towers downtown / inland, pool decks behind the coastal hotels.
	const FVector2D Center(CX, CY);
	LotSignChance = bBuildSigns ? 0.7f : 0.f;
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
		// Centred in the 900cm garden gap, so the crowns stay clear of both walls.
		AddPalm(FVector(GapX, CY - 700.f, 0.f), 800.f, Rng, true, 430.f);
		AddPalm(FVector(GapX, CY + 700.f, 0.f), 900.f, Rng, true, 430.f);
	}
	else
	{
		AddPalm(FVector(CX, CY, 0.f), Rng.FRandRange(700.f, 1000.f), Rng, true);
	}
	LotSignChance = 0.f;
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

	// Ground floor: pilasters and a deep canopy with a neon edge (lobby glazing below unless the lot is clad).
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

		// Stacked-letter blade sign down the fin face, the signature of the strip.
		FRandomStream SignRng = NCSignStream(Lot.Facade, Seed);
		if (SignRng.FRand() < LotSignChance && SignCount < MaxSigns)
		{
			const FString Name = NCPal::HotelNames[SignRng.RandRange(0, static_cast<int32>(UE_ARRAY_COUNT(NCPal::HotelNames)) - 1)];
			const float Bottom = 445.f + 90.f;
			const float Top = FinTop - 220.f;
			const float Size = FMath::Min(105.f, (Top - Bottom) / (Name.Len() * 1.25f));
			if (Size >= 55.f)
			{
				FString Stacked;
				for (int32 i = 0; i < Name.Len(); ++i)
				{
					if (i > 0)
					{
						Stacked += TEXT("<br>");
					}
					Stacked.AppendChar(Name[i]);
				}
				AddLotSign(Lot, Stacked, 81.f, (Bottom + Top) * 0.5f, Size, NCPal::SignColors[Neon - NCPal::NeonPink]);
			}
		}
	}

	// Clad lots: imported window modules (door panel in the middle at street level) between the pilasters or
	// rounded corners, on the massing that stays as the plaster wall. Otherwise lobby glazing, eyebrow ledges
	// and punched windows. The window draws below run either way so the layout stream never shifts.
	const int32 Floors = FMath::Max(0, FMath::RoundToInt((H - 420.f) / 330.f));
	const float Edge = bRounded ? 345.f : 85.f;
	const bool bClad = bLotCladFront && CladWall(Lot, -W * 0.5f + Edge, W * 0.5f - Edge, 420.f, Floors, 330.f, Trim, EGround::Door, bFin, true);
	if (bLotCladFront)
	{
		CladEndWall(Lot, bRounded ? 345.f : 40.f, 420.f, Floors, 330.f, Trim);
	}
	if (!bClad)
	{
		AddLotBox(Lot, NCPal::GlassWarm, 0.f, 3.f, 200.f, W - 160.f, 8.f, 300.f, false, false);
	}

	const int32 Cols = FMath::Max(2, FMath::FloorToInt(W / 280.f));
	const float Step = (W - 200.f) / Cols;
	for (int32 f = 0; f < Floors; ++f)
	{
		const float Z0 = 420.f + f * 330.f;
		if (!bClad)
		{
			AddLotBox(Lot, Trim, 0.f, 28.f, Z0 + 305.f, W - 30.f, 56.f, 14.f);
		}
		for (int32 c = 0; c < Cols; ++c)
		{
			const float A = -W * 0.5f + 100.f + Step * (c + 0.5f);
			if (bFin && FMath::Abs(A) < 150.f)
			{
				continue;
			}
			const int32 Glass = Rng.FRand() < 0.12f ? NCPal::GlassWarm : NCPal::GlassDark;
			if (!bClad)
			{
				AddLotBox(Lot, Glass, A, 3.f, Z0 + 150.f, FMath::Min(170.f, Step - 70.f), 8.f, 180.f, false, false);
			}
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
	int32 Sign = Rng.FRand() < 0.45f ? NCPal::Pick(NCPal::Neons, Rng) : NCPal::CanvasCream;
	float SignW = W * Rng.FRandRange(0.45f, 0.75f);

	// Some boards become dark enamel with lettering in the board's colour (layout RNG draws unchanged).
	FRandomStream SignRng = NCSignStream(Lot.Facade, Seed);
	if (SignRng.FRand() < LotSignChance && SignCount < MaxSigns)
	{
		const FString Name = NCPal::ShopNames[SignRng.RandRange(0, static_cast<int32>(UE_ARRAY_COUNT(NCPal::ShopNames)) - 1)];
		const float Size = FMath::Clamp((W - 160.f) / (Name.Len() * 0.62f), 34.f, 54.f);
		const float Needed = Name.Len() * Size * 0.62f + 90.f;
		if (Needed <= W - 60.f)
		{
			SignW = FMath::Max(SignW, Needed);
			const FColor Color = Sign == NCPal::CanvasCream ? NCPal::SignColors[3] : NCPal::SignColors[Sign - NCPal::NeonPink];
			Sign = NCPal::SignBoard;
			AddLotSign(Lot, Name, 15.f, 405.f, Size, Color);
		}
	}
	AddLotBox(Lot, Sign, 0.f, 8.f, 405.f, SignW, 12.f, 70.f, false, false);

	// Clad lots: imported window modules on the upper floors from 440cm, clear of the sign band and lettering,
	// topping out under the parapet cap. The shop front stays primitive; the window draws still run.
	const int32 Floors = FMath::Max(0, FMath::RoundToInt((H - 420.f) / 320.f));
	const bool bClad = bLotCladFront && CladWall(Lot, -W * 0.5f + 40.f, W * 0.5f - 40.f, 440.f, Floors, 320.f, NCPal::DecoWhite, EGround::None, false, false);
	if (bLotCladFront)
	{
		CladEndWall(Lot, 40.f, 440.f, Floors, 320.f, NCPal::DecoWhite);
	}
	const int32 Cols = FMath::Max(2, FMath::FloorToInt(W / 300.f));
	const float Step = (W - 160.f) / Cols;
	for (int32 f = 0; f < Floors; ++f)
	{
		const float Z0 = 440.f + f * 320.f;
		for (int32 c = 0; c < Cols; ++c)
		{
			const float A = -W * 0.5f + 80.f + Step * (c + 0.5f);
			const int32 Glass = Rng.FRand() < 0.1f ? NCPal::GlassWarm : NCPal::GlassDark;
			if (!bClad)
			{
				AddLotBox(Lot, Glass, A, 3.f, Z0 + 140.f, Step - 90.f, 8.f, 170.f, false, false);
			}
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

	// Crown lettering towards the coast (east) and the southern approach.
	FRandomStream SignRng = NCSignStream(Center, Seed);
	if (SignRng.FRand() < LotSignChance && SignCount < MaxSigns - 1)
	{
		const FString Name = NCPal::TowerNames[SignRng.RandRange(0, static_cast<int32>(UE_ARRAY_COUNT(NCPal::TowerNames)) - 1)];
		const float Half = Size * 0.35f;
		const float WS = FMath::Min(160.f, Size * 0.62f / (Name.Len() * 0.62f));
		const FColor Color = NCPal::SignColors[Neon - NCPal::NeonPink];
		AddSign(Name, Center + FVector2D(Half, 0.f), FVector2D(1.f, 0.f), 3.f, Height + 150.f, WS, Color);
		AddSign(Name, Center - FVector2D(0.f, Half), FVector2D(0.f, -1.f), 3.f, Height + 150.f, WS, Color);
	}
}

void ANCCityVisual::AddPalm(const FVector& Base, float Height, FRandomStream& Rng, bool bCollide, float MaxCanopyRadius)
{
	const FRotator Lean(Rng.FRandRange(-6.f, 6.f), 0.f, Rng.FRandRange(-6.f, 6.f));
	if (HasModel(ENCModel::CoastalTree))
	{
		// Real tree on the palm's spot; the trunk cylinder survives only as hidden collision at its exact transform.
		if (bCollide)
		{
			Add(ENCShape::Cylinder, NCPal::Trunk, Base + Lean.RotateVector(FVector(0.f, 0.f, Height * 0.5f)), FVector(36.f, 36.f, Height), Lean, true, false, false, true);
		}
		// The retired fronds' draws (spin + 7 droops) still run so the layout stream is unchanged.
		(void)Rng.FRandRange(0.f, 360.f);
		for (int32 k = 0; k < 7; ++k)
		{
			(void)Rng.FRandRange(0.f, 10.f);
		}
		AddTree(Base, Lean, Height, MaxCanopyRadius);
		return;
	}

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

void ANCCityVisual::AddLamp(const FVector& Base, bool bCollide, const FVector2D& ToRoad)
{
	if (HasModel(ENCModel::StreetLamp))
	{
		// Imported lamp with its head at ~625cm; the primitive pole stays only as hidden collision where it collided.
		if (bCollide)
		{
			Add(ENCShape::Cylinder, NCPal::Metal, Base + FVector(0.f, 0.f, 300.f), FVector(14.f, 14.f, 600.f), FRotator::ZeroRotator, true, false, false, true);
		}
		const FVector3f Size = ModelSize(ENCModel::StreetLamp);
		const float S = FMath::Clamp(625.f / Size.Z, 0.25f, 3.f);
		const float Yaw = ToRoad.IsNearlyZero() ? 0.f : NCYawToFace(ToRoad);
		AddModel(ENCModel::StreetLamp, Base, FQuat(FRotator(0.f, Yaw, 0.f)), S);
		// Night glow: a small LampGlow disc tucked under a compact post-top head. Arm-style lamps get none
		// (no per-lamp lights either way).
		if (0.5f * FMath::Max(Size.X, Size.Y) * S <= 45.f)
		{
			Add(ENCShape::Cylinder, NCPal::LampGlow, Base + FVector(0.f, 0.f, Size.Z * S * 0.88f), FVector(24.f, 24.f, 2.f), FRotator::ZeroRotator, false, false, true);
		}
		return;
	}

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
				// 250cm from the facade line: crowns are kept clear of the facade modules.
				AddPalm(FVector(C - RoadWidth * 0.5f - 550.f, Y, 0.f), Rng.FRandRange(820.f, 1080.f), Rng, true, 240.f);
				if (FMath::Abs(Y) < 40000.f)
				{
					AddLamp(FVector(CoastRoadX + RoadWidth * 0.5f + 70.f, Y + 900.f, 0.f), false, FVector2D(-1.f, 0.f));
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
				AddPalm(FVector(C + S * Offset, Y, 0.f), Rng.FRandRange(700.f, 980.f), Rng, true, 330.f);
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
				AddLamp(FVector(X, C + S * Offset, 0.f), true, FVector2D(0.f, -S));
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

	// The measured painted_wooden_bench backrest is at local -Y; its front is +Y.
	// Rotate -90 degrees to face the sea (+X).
	const bool bBenchModel = HasModel(ENCModel::Bench);
	FQuat BenchRot = FQuat::Identity;
	float BenchScale = 1.f;
	if (bBenchModel)
	{
		const FVector3f Size = ModelSize(ENCModel::Bench);
		const float Len = FMath::Max(Size.X, Size.Y);
		BenchRot = FQuat(FRotator(0.f, -90.f, 0.f));
		BenchScale = Len >= 100.f && Len <= 300.f ? 1.f : 180.f / Len;	// only correct an import-unit error
	}
	for (float Y = -29250.f; Y <= 29250.f; Y += 1500.f)
	{
		if (IsNearRoadCenter(Y, 2200.f))
		{
			continue;
		}
		const float X = 24000.f;
		if (bBenchModel)
		{
			AddModel(ENCModel::Bench, FVector(X, Y, 4.f), BenchRot, BenchScale);	// on the promenade top
			continue;
		}
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
	LotSignChance = 0.f;

	// The deco strip continues north and south of the playable city along the coast road, clad like the city front
	// (its facade modules cull with distance).
	LotCladEnd = 0;
	for (float Sign : { -1.f, 1.f })
	{
		bLotCladFront = true;
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
		bLotCladFront = false;
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

void ANCCityVisual::AddLotSign(const FLot& Lot, const FString& Text, float Out, float Z, float WorldSize, const FColor& Color)
{
	AddSign(Text, Lot.Facade, Lot.Normal, Out, Z, WorldSize, Color);
}

void ANCCityVisual::AddSign(const FString& Text, const FVector2D& Facade, const FVector2D& Normal, float Out, float Z, float WorldSize, const FColor& Color)
{
	if (SignCount >= MaxSigns || Text.IsEmpty())
	{
		return;
	}
	const FVector2D P = Facade + Normal * Out;
	// TextRender faces local +X; yaw it onto the street normal.
	const FRotator Facing(0.f, FMath::RadiansToDegrees(FMath::Atan2(Normal.Y, Normal.X)), 0.f);

	UTextRenderComponent* Text3D = NewObject<UTextRenderComponent>(this);
	Text3D->SetupAttachment(SceneRoot);
	Text3D->SetMobility(EComponentMobility::Static);
	Text3D->SetRelativeLocationAndRotation(FVector(P.X, P.Y, Z), Facing);
	Text3D->SetHorizontalAlignment(EHTA_Center);
	Text3D->SetVerticalAlignment(EVRTA_TextCenter);
	Text3D->SetWorldSize(WorldSize);
	Text3D->SetTextRenderColor(Color);
	Text3D->SetText(FText::FromString(Text));
	if (SignMID)
	{
		Text3D->SetTextMaterial(SignMID);
	}
	Text3D->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Text3D->SetCanEverAffectNavigation(false);
	Text3D->SetCastShadow(false);
	Text3D->LDMaxDrawDistance = WorldSize >= 100.f ? 70000.f : 22000.f;
	Text3D->RegisterComponent();
	AddInstanceComponent(Text3D);
	Signs.Add(Text3D);
	++SignCount;
}

void ANCCityVisual::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bDayNightCycle)
	{
		return;
	}
	TimeOfDayHours = FMath::Fmod(TimeOfDayHours + DeltaSeconds / FMath::Max(1.f, SecondsPerGameHour), 24.f);
	UpdateLighting(DeltaSeconds, false);
}

void ANCCityVisual::UpdateLighting(float DeltaSeconds, bool bSnap)
{
	// Sun arc: 0 at sunrise (east), 0.5 at noon (south, high), 1 at sunset (west-north-west); 1..2 below the horizon.
	constexpr float Sunrise = 6.5f;
	constexpr float Sunset = 19.f;
	constexpr float MaxElevation = 64.f;
	constexpr float MoonElevation = 38.f;
	const float H = TimeOfDayHours;
	const float Arc = (H >= Sunrise && H <= Sunset)
		? (H - Sunrise) / (Sunset - Sunrise)
		: 1.f + FMath::Fmod(H - Sunset + 24.f, 24.f) / (24.f - (Sunset - Sunrise));
	const float Angle = PI * Arc;
	const float SinA = FMath::Sin(Angle);
	const float ElevDeg = SinA * (SinA >= 0.f ? MaxElevation : 40.f);

	FVector2D Horiz(FMath::Cos(Angle), -SinA + 0.35f);
	Horiz.Normalize();
	const float ElevRad = FMath::DegreesToRadians(ElevDeg);
	const FVector SunDir(Horiz.X * FMath::Cos(ElevRad), Horiz.Y * FMath::Cos(ElevRad), FMath::Sin(ElevRad));
	const float MoonRad = FMath::DegreesToRadians(MoonElevation);
	const FVector MoonDir(-Horiz.X * FMath::Cos(MoonRad), -Horiz.Y * FMath::Cos(MoonRad), FMath::Sin(MoonRad));

	// Blend weights, all continuous in time.
	const float DayW = FMath::Pow(FMath::Clamp((ElevDeg + 4.f) / 22.f, 0.f, 1.f), 0.6f) * NCPal::Smooth01((ElevDeg + 4.f) / 4.f);
	const float Golden = NCPal::Smooth01(1.f - FMath::Abs(ElevDeg - 6.f) / 14.f);
	const float Night = 1.f - NCPal::Smooth01((ElevDeg + 8.f) / 14.f);
	const float MoonW = NCPal::Smooth01((-ElevDeg - 2.f) / 8.f);
	const float LampsOn = 1.f - NCPal::Smooth01((ElevDeg - 1.f) / 7.f);
	// Dusk band (~sun 16deg .. -6deg, roughly 18:00-19:30): the street is in building shadow while the captured
	// sky dims, so a small exposure lift applies here only; the afternoon and deep night are untouched.
	const float Dusk = NCPal::Smooth01((ElevDeg + 7.f) / 6.f) * (1.f - NCPal::Smooth01((ElevDeg - 12.f) / 10.f)) * (1.f - Night);

	const float SunLux = 10.f * DayW;
	const float MoonLux = 0.9f * MoonW;

	// One directional light: the sun, handing over to a cool moon once both are near zero (no visible pop).
	// The sun stays white and warms through its blackbody temperature as it drops.
	if (Sun)
	{
		const bool bSun = ElevDeg > -3.5f;
		Sun->SetRelativeRotation((bSun ? -SunDir : -MoonDir).Rotation());
		Sun->SetIntensity(bSun ? SunLux : MoonLux);
		Sun->SetTemperature(bSun ? NCPal::SunKelvin(ElevDeg) : 6500.f);
		Sun->SetLightColor(bSun ? FLinearColor::White : FLinearColor(0.62f, 0.74f, 1.f));
	}

	if (SkyLight)
	{
		// The real-time captured sky is the daylight fill; only night gets a lift so streets stay playable.
		SkyLight->SetIntensity(FMath::Lerp(1.f, 1.7f, Night));
		SkyLight->SetLightColor(FMath::Lerp(FLinearColor::White, FLinearColor(0.72f, 0.80f, 1.f), Night));
	}

	if (HeightFog)
	{
		// Humid blue-grey coastal haze; the low sun's warmth comes mostly from its temperature, so gold stays light.
		const FLinearColor DayFog(0.55f, 0.66f, 0.80f);
		const FLinearColor GoldFog(0.80f, 0.62f, 0.50f);
		const FLinearColor NightFog(0.05f, 0.07f, 0.13f);
		HeightFog->SetFogDensity(FMath::Lerp(0.006f, 0.0034f, Night));
		HeightFog->SetFogInscatteringColor(FMath::Lerp(FMath::Lerp(DayFog, GoldFog, 0.5f * Golden), NightFog, Night));
		HeightFog->SetDirectionalInscatteringColor(FLinearColor(1.f, 0.82f, 0.62f) * (0.4f * DayW));
	}

	// Fixed exposure: +0.15 EV by day (sunlit plaster ~sRGB 215-230 without clipping), partial compensation at night
	// so it reads moody but playable, and +0.3 EV through dusk.
	const float TargetBias = FMath::Clamp(0.85f * FMath::Log2(10.f / (SunLux + 2.6f * MoonLux + 0.45f)) + 0.15f * DayW + 0.3f * Dusk, 0.f, 3.4f);
	AppliedExposureBias = bSnap ? TargetBias : FMath::FInterpTo(AppliedExposureBias, TargetBias, DeltaSeconds, 0.8f);
	if (Grade)
	{
		// Neutral: D65 white balance by day, unit saturation, default tonemapper; bloom stays restrained.
		FPostProcessSettings& PP = Grade->Settings;
		PP.AutoExposureBias = AppliedExposureBias;
		PP.WhiteTemp = 6500.f - 900.f * Night;
		PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
		PP.BloomIntensity = FMath::Lerp(0.35f, 0.6f, Night);
	}

	// Emissive palettes: unlit glass tubes by day is the realistic read; neon, lamps and interiors come up after dusk.
	for (int32 i = 0; i < PaletteMaterials.Num() && i < NCPal::Count; ++i)
	{
		UMaterialInstanceDynamic* MID = PaletteMaterials[i];
		const float Base = NCPal::Defs[i].Emissive;
		if (!MID || Base <= 0.f)
		{
			continue;
		}
		float Mult = 1.f;
		if (i == NCPal::NeonPink || i == NCPal::NeonAqua || i == NCPal::NeonAmber)
		{
			Mult = FMath::Lerp(0.15f, 0.6f, Night);
		}
		else if (i == NCPal::LampGlow)
		{
			Mult = FMath::Lerp(0.f, 0.7f, LampsOn);
		}
		else if (i == NCPal::GlassWarm)
		{
			Mult = LampsOn;	// 0 by day, the full 0.6 at night
		}
		MID->SetScalarParameterValue(TEXT("EmissiveStrength"), Base * Mult);
	}
	if (SignMID)
	{
		SignMID->SetScalarParameterValue(TEXT("Glow"), FMath::Lerp(0.4f, 1.8f, LampsOn));
	}
}
