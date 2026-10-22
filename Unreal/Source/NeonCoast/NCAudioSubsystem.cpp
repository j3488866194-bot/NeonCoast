#include "NCAudioSubsystem.h"
#include "NCCar.h"
#include "NCHero.h"
#include "NCGameplaySubsystem.h"
#include "NCGameUserSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
    float EffectsGain()
    {
        const auto* Settings = UNCGameUserSettings::Get();
        return Settings ? Settings->GetEffectsGain() : 1.f;
    }
    float MusicGain()
    {
        const auto* Settings = UNCGameUserSettings::Get();
        return Settings ? Settings->GetMusicGain() : .6f;
    }
}

bool UNCAudioSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNCAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const TCHAR* Command = FCommandLine::Get();
    bEnabled = !IsRunningCommandlet()
        && !FParse::Param(Command, TEXT("NeonCoastSmokeTest"))
        && !FParse::Param(Command, TEXT("NullRHI"))
        && !FParse::Param(Command, TEXT("nosound"))
        && !FParse::Param(Command, TEXT("noaudio"))
        && !FParse::Param(Command, TEXT("nullaudio"));
}

bool UNCAudioSubsystem::IsTickable() const
{
    return bEnabled && !IsTemplate() && IsInitialized() && GetWorld()
        && GetWorld()->HasBegunPlay() && GetWorld()->GetNetMode() != NM_DedicatedServer;
}

TStatId UNCAudioSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UNCAudioSubsystem, STATGROUP_Tickables);
}

USoundBase* UNCAudioSubsystem::LoadSound(const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/Game/Audio/%s.%s"), Name, Name);
    USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path);
    if (!Sound) UE_LOG(LogTemp, Warning, TEXT("NC_AUDIO_MISSING %s"), *Path);
    return Sound;
}

UAudioComponent* UNCAudioSubsystem::StartLoop(USoundBase* Sound, float Volume)
{
    if (!Sound) return nullptr;
    UAudioComponent* Component = UGameplayStatics::CreateSound2D(this, Sound, Volume, 1.f, 0.f, nullptr, false, false);
    if (Component)
    {
        // Game sounds obey world pause; they are not UI sounds that continue over menus.
        Component->bIsUISound = false;
        Component->bAutoDestroy = false;
        Component->Play();
    }
    return Component;
}

bool UNCAudioSubsystem::EnsureStarted()
{
    if (!bEnabled || !GetWorld() || !GetWorld()->AllowAudioPlayback()
        || !GetWorld()->GetAudioDeviceRaw() || GetWorld()->GetNetMode() == NM_DedicatedServer) return false;
    if (bStarted) return true;
    if (Clock < RetryAt) return false;
    RetryAt = Clock + 1.f;
    RadioSounds = { LoadSound(TEXT("SW_Radio_NightDrive")), LoadSound(TEXT("SW_Radio_Coastline")) };
    EngineSound = LoadSound(TEXT("SW_EngineLoop"));
    AmbienceSound = LoadSound(TEXT("SW_CoastAmbience"));
    SirenSound = LoadSound(TEXT("SW_PoliceSiren"));
    HornSound = LoadSound(TEXT("SW_Horn"));
    GunshotSound = LoadSound(TEXT("SW_Gunshot"));
    ReloadSound = LoadSound(TEXT("SW_Reload"));
    FootstepSound = LoadSound(TEXT("SW_Footstep"));
    // These four persistent players are paused when inactive. Keep a loop alive
    // across a temporary inaudible distance/gain instead of losing its voice.
    const TArray<USoundBase*> LoopAssets = { RadioSounds[0], RadioSounds[1], EngineSound, AmbienceSound, SirenSound };
    for (USoundBase* LoopAsset : LoopAssets)
        if (LoopAsset) LoopAsset->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;

    EffectAttenuation = NewObject<USoundAttenuation>(this);
    EffectAttenuation->Attenuation.bAttenuate = true;
    EffectAttenuation->Attenuation.bSpatialize = true;
    EffectAttenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
    EffectAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
    EffectAttenuation->Attenuation.AttenuationShapeExtents = FVector(550, 0, 0);
    EffectAttenuation->Attenuation.FalloffDistance = 6500.f;
    SirenAttenuation = NewObject<USoundAttenuation>(this);
    SirenAttenuation->Attenuation = EffectAttenuation->Attenuation;
    SirenAttenuation->Attenuation.AttenuationShapeExtents = FVector(500, 0, 0);
    SirenAttenuation->Attenuation.FalloffDistance = 9500.f;

    RadioLoop = StartLoop(RadioSounds.IsValidIndex(Station) ? RadioSounds[Station].Get() : nullptr, .001f * MusicGain());
    EngineLoop = StartLoop(EngineSound, .001f * EffectsGain());
    AmbienceLoop = StartLoop(AmbienceSound, .12f * EffectsGain());
    if (RadioLoop) RadioLoop->SetPaused(true);
    if (EngineLoop) EngineLoop->SetPaused(true);
    if (SirenSound)
    {
        const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
        const FVector InitialLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
        SirenLoop = UGameplayStatics::SpawnSoundAtLocation(this, SirenSound, InitialLocation,
            FRotator::ZeroRotator, .001f * EffectsGain(), 1.f, 0.f, SirenAttenuation, nullptr, false);
        if (SirenLoop)
        {
            SirenLoop->bIsUISound = false;
            SirenLoop->bAutoDestroy = false;
            SirenLoop->SetPaused(true);
        }
    }
    bStarted = true;
    UE_LOG(LogTemp, Display, TEXT("NC_AUDIO_READY radioTracks=%d loops=%d"), RadioSounds.Num(),
        int32(IsValid(RadioLoop)) + int32(IsValid(EngineLoop)) + int32(IsValid(AmbienceLoop)) + int32(IsValid(SirenLoop)));
    return true;
}

void UNCAudioSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    Clock += DeltaTime;
    if (!EnsureStarted()) return;
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    const ANCCar* Car = Cast<ANCCar>(Pawn);
    const float SpeedFraction = Car ? FMath::Clamp(Car->GetSpeedKmh() / 180.f, 0.f, 1.f) : 0.f;
    const bool bEngineRunning = Car && Car->GetCondition() > 0.f;
    EngineGain = FMath::FInterpTo(EngineGain, bEngineRunning ? .085f + SpeedFraction * .10f : 0.f, DeltaTime, 5.f);
    EnginePitch = FMath::FInterpTo(EnginePitch, .75f + SpeedFraction * 1.65f, DeltaTime, 4.f);
    if (EngineLoop)
    {
        EngineLoop->SetPaused(!bEngineRunning && EngineGain < .002f);
        EngineLoop->SetVolumeMultiplier(FMath::Max(.001f, EngineGain) * EffectsGain());
        EngineLoop->SetPitchMultiplier(EnginePitch);
    }
    const bool bRadioPlaying = Car && Station < 2;
    RadioGain = FMath::FInterpTo(RadioGain, bRadioPlaying ? .18f : 0.f, DeltaTime, 4.f);
    if (RadioLoop)
    {
        RadioLoop->SetPaused(!bRadioPlaying && RadioGain < .002f);
        RadioLoop->SetVolumeMultiplier(FMath::Max(.001f, RadioGain) * MusicGain());
    }
    if (AmbienceLoop) AmbienceLoop->SetVolumeMultiplier((Car ? .065f : .13f) * EffectsGain());
    UpdateSiren(DeltaTime, Pawn);
    UpdateFootsteps(Pawn);
}

void UNCAudioSubsystem::UpdateSiren(float DeltaTime, APawn* Pawn)
{
    if (!SirenLoop) return;
    const UNCGameplaySubsystem* Gameplay = GetWorld()->GetSubsystem<UNCGameplaySubsystem>();
    const int32 Wanted = Gameplay ? Gameplay->GetWantedLevel() : 0;
    if (Clock >= NextSirenScan)
    {
        NextSirenScan = Clock + .25f;
        NearestPolice.Reset();
        float NearestSquared = FMath::Square(10000.f);
        if (Wanted > 0 && Pawn)
            for (TActorIterator<ANCCar> It(GetWorld()); It; ++It)
            {
                if (!It->IsPoliceVehicle() || It->GetCondition() <= 0.f) continue;
                const float DistanceSquared = FVector::DistSquared2D(Pawn->GetActorLocation(), It->GetActorLocation());
                if (DistanceSquared < NearestSquared) { NearestSquared = DistanceSquared; NearestPolice = *It; }
            }
    }
    const bool bAudiblePursuit = Wanted > 0 && NearestPolice.IsValid();
    SirenGain = FMath::FInterpTo(SirenGain, bAudiblePursuit ? .22f + Wanted * .025f : 0.f, DeltaTime, 4.f);
    if (NearestPolice.IsValid()) SirenLoop->SetWorldLocation(NearestPolice->GetActorLocation());
    SirenLoop->SetPaused(!bAudiblePursuit && SirenGain < .002f);
    SirenLoop->SetVolumeMultiplier(FMath::Max(.001f, SirenGain) * EffectsGain());
}

void UNCAudioSubsystem::UpdateFootsteps(APawn* Pawn)
{
    const ANCHero* Hero = Cast<ANCHero>(Pawn);
    if (!Hero || !FootstepSound || Clock < StepReadyAt || Hero->GetHealth() <= 0.f) return;
    const UCharacterMovementComponent* Movement = Hero->GetCharacterMovement();
    const float Speed = Hero->GetVelocity().Size2D();
    if (!Movement || !Movement->IsMovingOnGround() || Speed < 100.f) return;
    const float Pitch = 1.f + FMath::Sin(Clock * 17.f) * .055f;
    UGameplayStatics::PlaySoundAtLocation(this, FootstepSound, Hero->GetActorLocation() - FVector(0, 0, 85),
        .13f * EffectsGain(), Pitch, 0.f, EffectAttenuation);
    StepReadyAt = Clock + FMath::Clamp(170.f / Speed, .19f, .48f);
}

void UNCAudioSubsystem::CycleRadio()
{
    Station = (Station + 1) % 3;
    if (!EnsureStarted()) return;
    if (RadioLoop && Station < 2 && RadioSounds.IsValidIndex(Station))
    {
        RadioLoop->Stop();
        RadioLoop->SetSound(RadioSounds[Station]);
        RadioGain = 0.f;
        RadioLoop->SetVolumeMultiplier(.001f * MusicGain());
        RadioLoop->Play();
        RadioLoop->SetPaused(!Cast<ANCCar>(UGameplayStatics::GetPlayerPawn(this, 0)));
    }
}

FString UNCAudioSubsystem::GetRadioLabel() const
{
    switch (Station)
    {
    case 0: return TEXT("NIGHT DRIVE 98.3");
    case 1: return TEXT("COASTLINE 104.7");
    default: return TEXT("RADIO OFF");
    }
}

void UNCAudioSubsystem::SoundHorn()
{
    const ANCCar* Car = Cast<ANCCar>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Car || Clock < HornReadyAt || !EnsureStarted() || !HornSound) return;
    UGameplayStatics::PlaySoundAtLocation(this, HornSound, Car->GetActorLocation(), .36f * EffectsGain(), 1.f, 0.f, EffectAttenuation);
    HornReadyAt = Clock + .5f;
}

void UNCAudioSubsystem::PlayGunshot(FVector Location)
{
    if (!EnsureStarted() || !GunshotSound) return;
    UGameplayStatics::PlaySoundAtLocation(this, GunshotSound, Location, .48f * EffectsGain(),
        .98f + FMath::Sin(Clock * 31.f) * .035f, 0.f, EffectAttenuation);
}

void UNCAudioSubsystem::PlayReload(FVector Location)
{
    if (!EnsureStarted() || !ReloadSound) return;
    UGameplayStatics::PlaySoundAtLocation(this, ReloadSound, Location, .20f * EffectsGain(), 1.f, 0.f, EffectAttenuation);
}

void UNCAudioSubsystem::ApplyUserVolumeSettings()
{
    const float Effects = EffectsGain();
    const bool bDriving = GetWorld() && Cast<ANCCar>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (EngineLoop) EngineLoop->SetVolumeMultiplier(FMath::Max(.001f, EngineGain) * Effects);
    if (RadioLoop) RadioLoop->SetVolumeMultiplier(FMath::Max(.001f, RadioGain) * MusicGain());
    if (SirenLoop) SirenLoop->SetVolumeMultiplier(FMath::Max(.001f, SirenGain) * Effects);
    if (AmbienceLoop) AmbienceLoop->SetVolumeMultiplier((bDriving ? .065f : .13f) * Effects);
}

void UNCAudioSubsystem::StopLoop(TObjectPtr<UAudioComponent>& Component)
{
    if (IsValid(Component))
    {
        Component->Stop();
        Component->DestroyComponent();
    }
    Component = nullptr;
}

void UNCAudioSubsystem::Deinitialize()
{
    StopLoop(RadioLoop);
    StopLoop(EngineLoop);
    StopLoop(AmbienceLoop);
    StopLoop(SirenLoop);
    RadioSounds.Reset();
    bStarted = false;
    bEnabled = false;
    Super::Deinitialize();
}
