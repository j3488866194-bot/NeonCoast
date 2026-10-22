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
#include "HAL/PlatformTime.h"
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

UAudioComponent* UNCAudioSubsystem::StartLoop(USoundBase* Sound, float Volume, bool bUI)
{
    if (!Sound) return nullptr;
    UAudioComponent* Component = UGameplayStatics::CreateSound2D(this, Sound, Volume, 1.f, 0.f, nullptr, false, false);
    if (Component)
    {
        Component->bIsUISound = bUI;
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
    const double Now = FPlatformTime::Seconds();
    if (Now < RetryAt) return false;
    RetryAt = Now + 1.0;
    if (!bAssetsLoaded)
    {
        RadioSounds = { LoadSound(TEXT("SW_Radio_NightDrive")), LoadSound(TEXT("SW_Radio_Coastline")) };
        EngineSound = LoadSound(TEXT("SW_EngineLoop"));
        AmbienceSound = LoadSound(TEXT("SW_CoastAmbience"));
        SirenSound = LoadSound(TEXT("SW_PoliceSiren"));
        HornSound = LoadSound(TEXT("SW_Horn"));
        GunshotSound = LoadSound(TEXT("SW_Gunshot"));
        ReloadSound = LoadSound(TEXT("SW_Reload"));
        FootstepSound = LoadSound(TEXT("SW_Footstep"));
        // Persistent players survive temporary silence, including genuine mute.
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
        bAssetsLoaded = true;
    }

    if (!RadioLoop) RadioLoop = StartLoop(RadioSounds.IsValidIndex(Station) ? RadioSounds[Station].Get() : RadioSounds[0].Get(), 0.f);
    if (!EngineLoop) EngineLoop = StartLoop(EngineSound, 0.f);
    if (!AmbienceLoop) AmbienceLoop = StartLoop(AmbienceSound, bFrontEndActive ? 0.f : .24f * EffectsGain());
    if (!MenuMusicLoop) MenuMusicLoop = StartLoop(RadioSounds.IsValidIndex(1) ? RadioSounds[1].Get() : nullptr, 0.f, true);
    if (!MenuAmbienceLoop) MenuAmbienceLoop = StartLoop(AmbienceSound, 0.f, true);
    if (RadioLoop) RadioLoop->SetPaused(true);
    if (EngineLoop) EngineLoop->SetPaused(true);
    if (!SirenLoop && SirenSound)
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
    bStarted = RadioLoop && EngineLoop && AmbienceLoop && SirenLoop && MenuMusicLoop && MenuAmbienceLoop;
    UpdateMenuAudio();
    UE_LOG(LogTemp, Display, TEXT("NC_AUDIO_COMPONENTS ready=%d radioTracks=%d loops=%d menuLoops=%d"), bStarted, RadioSounds.Num(),
        int32(IsValid(RadioLoop)) + int32(IsValid(EngineLoop)) + int32(IsValid(AmbienceLoop)) + int32(IsValid(SirenLoop)),
        int32(IsValid(MenuMusicLoop)) + int32(IsValid(MenuAmbienceLoop)));
    return bStarted;
}

void UNCAudioSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!EnsureStarted()) return;
    UpdateMenuAudio();
    // Only the menu mix runs while paused; gameplay timers and effects stay frozen.
    if (bFrontEndActive || UGameplayStatics::IsGamePaused(this)) return;
    Clock += DeltaTime;
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    const ANCCar* Car = Cast<ANCCar>(Pawn);
    const float SpeedFraction = Car ? FMath::Clamp(Car->GetSpeedKmh() / 180.f, 0.f, 1.f) : 0.f;
    const bool bEngineRunning = Car && Car->GetCondition() > 0.f;
    EngineGain = FMath::FInterpTo(EngineGain, bEngineRunning ? .18f + SpeedFraction * .16f : 0.f, DeltaTime, 5.f);
    EnginePitch = FMath::FInterpTo(EnginePitch, .75f + SpeedFraction * 1.65f, DeltaTime, 4.f);
    if (EngineLoop)
    {
        EngineLoop->SetPaused(!bEngineRunning && EngineGain < .002f);
        EngineLoop->SetVolumeMultiplier(EngineGain * EffectsGain());
        if (bEngineRunning && EngineLoop->GetPlayState() == EAudioComponentPlayState::Stopped) EngineLoop->Play();
        EngineLoop->SetPitchMultiplier(EnginePitch);
    }
    const bool bRadioPlaying = Car && Station < 2;
    RadioGain = FMath::FInterpTo(RadioGain, bRadioPlaying ? .36f : 0.f, DeltaTime, 4.f);
    if (RadioLoop)
    {
        RadioLoop->SetPaused(!bRadioPlaying && RadioGain < .002f);
        RadioLoop->SetVolumeMultiplier(RadioGain * MusicGain());
        if (bRadioPlaying && RadioLoop->GetPlayState() == EAudioComponentPlayState::Stopped) RadioLoop->Play();
    }
    if (AmbienceLoop)
    {
        AmbienceLoop->SetVolumeMultiplier((Car ? .12f : .24f) * EffectsGain());
        if (AmbienceLoop->GetPlayState() == EAudioComponentPlayState::Stopped) AmbienceLoop->Play();
    }
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
        .28f * EffectsGain(), Pitch, 0.f, EffectAttenuation);
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
    if (EngineLoop) EngineLoop->SetVolumeMultiplier(EngineGain * Effects);
    if (RadioLoop) RadioLoop->SetVolumeMultiplier(RadioGain * MusicGain());
    if (SirenLoop) SirenLoop->SetVolumeMultiplier(SirenGain * Effects);
    if (AmbienceLoop) AmbienceLoop->SetVolumeMultiplier((bDriving ? .12f : .24f) * Effects);
    if (PreviewSound) PreviewSound->SetVolumeMultiplier(.45f * Effects);
    UpdateMenuAudio();
}

void UNCAudioSubsystem::SetFrontEndActive(bool bActive)
{
    bFrontEndActive = bActive;
    if (!EnsureStarted()) return; // Tick retries even if a menu has already paused the world.
    UpdateMenuAudio();
    if (RadioLoop) RadioLoop->SetPaused(bActive || !Cast<ANCCar>(UGameplayStatics::GetPlayerPawn(this, 0)) || Station >= 2);
    if (EngineLoop) EngineLoop->SetPaused(bActive || !Cast<ANCCar>(UGameplayStatics::GetPlayerPawn(this, 0)));
    if (SirenLoop) SirenLoop->SetPaused(bActive || !NearestPolice.IsValid());
    if (AmbienceLoop) AmbienceLoop->SetPaused(bActive);
    if (!bActive) StopLoop(PreviewSound);
}

void UNCAudioSubsystem::UpdateMenuAudio()
{
    if (MenuMusicLoop)
    {
        MenuMusicLoop->SetVolumeMultiplier(bFrontEndActive ? .38f * MusicGain() : 0.f);
        if (bFrontEndActive && MenuMusicLoop->GetPlayState() == EAudioComponentPlayState::Stopped) MenuMusicLoop->Play();
        MenuMusicLoop->SetPaused(!bFrontEndActive);
    }
    if (MenuAmbienceLoop)
    {
        MenuAmbienceLoop->SetVolumeMultiplier(bFrontEndActive ? .16f * EffectsGain() : 0.f);
        if (bFrontEndActive && MenuAmbienceLoop->GetPlayState() == EAudioComponentPlayState::Stopped) MenuAmbienceLoop->Play();
        MenuAmbienceLoop->SetPaused(!bFrontEndActive);
    }
}

bool UNCAudioSubsystem::PlaySettingsPreview()
{
    if (!EnsureStarted() || !HornSound) return false;
    StopLoop(PreviewSound);
    // The original two-tone horn also makes an unmistakable, short output check.
    // No minimum volume is imposed: 0 on either master or effects remains silent.
    PreviewSound = UGameplayStatics::CreateSound2D(this, HornSound, .45f * EffectsGain(), 1.25f, 0.f, nullptr, false, false);
    if (!PreviewSound) return false;
    PreviewSound->bIsUISound = true;
    PreviewSound->bAutoDestroy = false;
    PreviewSound->Play();
    return true;
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
    StopLoop(MenuMusicLoop);
    StopLoop(MenuAmbienceLoop);
    StopLoop(PreviewSound);
    RadioSounds.Reset();
    bStarted = false;
    bEnabled = false;
    Super::Deinitialize();
}
