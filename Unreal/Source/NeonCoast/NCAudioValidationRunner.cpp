#include "NCAudioValidationRunner.h"
#include "NCAudioSubsystem.h"
#include "NCGameUserSettings.h"
#include "NCPlayerController.h"
#include "NCCar.h"
#include "Audio.h"
#include "AudioDevice.h"
#include "ISubmixBufferListener.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

class FNCAudioCapture final : public ISubmixBufferListener
{
public:
    virtual void OnNewSubmixBuffer(const USoundSubmix*, float* Data, int32 Count, int32 Channels, const int32 Rate, double) override
    {
        FScopeLock Guard(&Mutex);
        if (!bRecording || !Data || Count <= 0) return;
        NumChannels = Channels;
        SampleRate = Rate;
        // Bound evidence memory even if the game thread stalls during a test.
        if (Samples.Num() + Count <= 48000 * 16 * 6) Samples.Append(Data, Count);
    }
    virtual bool IsRenderingAudio() const override { return true; }
    virtual const FString& GetListenerName() const override
    {
        static const FString Name(TEXT("NeonCoastAudioVerification"));
        return Name;
    }
    void Begin()
    {
        FScopeLock Guard(&Mutex);
        Samples.Reset();
        bRecording = true;
    }
    TArray<float> End(int32& OutChannels, int32& OutRate)
    {
        FScopeLock Guard(&Mutex);
        bRecording = false;
        OutChannels = NumChannels;
        OutRate = SampleRate;
        return MoveTemp(Samples);
    }
private:
    FCriticalSection Mutex;
    TArray<float> Samples;
    bool bRecording = false;
    int32 NumChannels = 0, SampleRate = 0;
};

namespace
{
FString Quote(FString Text)
{
    Text.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Text.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Text.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    return TEXT("\"") + Text + TEXT("\"");
}
const TCHAR* StageNames[] = {
    TEXT("paused-menu-mix"), TEXT("paused-menu-music"), TEXT("paused-menu-ambience"),
    TEXT("paused-master-muted"), TEXT("paused-categories-muted"), TEXT("paused-settings-preview"),
    TEXT("walking-ambience"), TEXT("driving-engine"), TEXT("driving-radio"),
    TEXT("driving-master-muted"), TEXT("return-to-menu")
};
}

ANCAudioValidationRunner::ANCAudioValidationRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void ANCAudioValidationRunner::Check(bool bPassed, const FString& Name)
{
    ++Checks;
    if (!bPassed) ++Failures;
    Results.Add(FString::Printf(TEXT("{\"name\":%s,\"passed\":%s}"), *Quote(Name), bPassed ? TEXT("true") : TEXT("false")));
    UE_LOG(LogTemp, Display, TEXT("NC_AUDIO_CHECK %s %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), *Name);
}

void ANCAudioValidationRunner::SetVolumes(float Master, float Music, float Effects)
{
    UNCGameUserSettings* Settings = UNCGameUserSettings::Get();
    Settings->SetMasterVolume(Master);
    Settings->SetMusicVolume(Music);
    Settings->SetEffectsVolume(Effects);
    Audio->ApplyUserVolumeSettings();
}

void ANCAudioValidationRunner::BeginPhase(int32 Next)
{
    Phase = Next;
    PhaseStarted = FPlatformTime::Seconds();
    bCapturing = false;
    switch (Phase)
    {
    case 0: SetVolumes(1.f, .6f, 1.f); break;
    case 1: SetVolumes(1.f, 1.f, 0.f); break;
    case 2: SetVolumes(1.f, 0.f, 1.f); break;
    case 3: SetVolumes(0.f, 1.f, 1.f); break;
    case 4: SetVolumes(1.f, 0.f, 0.f); break;
    case 5:
        Audio->SetFrontEndActive(false);
        SetVolumes(1.f, 0.f, 1.f);
        break;
    case 6:
        Controller->StartOrResumeGame();
        SetVolumes(1.f, 0.f, 1.f);
        Check(!Controller->IsFrontEndOpen() && !UGameplayStatics::IsGamePaused(this), TEXT("menu resume restores active world audio"));
        break;
    case 7:
    {
        ANCCar* Car = nullptr;
        for (TActorIterator<ANCCar> It(GetWorld()); It; ++It)
            if (!It->IsPoliceVehicle()) { Car = *It; break; }
        Check(Car != nullptr, TEXT("driving audio uses a real possessed car"));
        if (Car) Controller->Possess(Car);
        SetVolumes(1.f, 0.f, 1.f);
        break;
    }
    case 8: SetVolumes(1.f, 1.f, 0.f); break;
    case 9: SetVolumes(0.f, 1.f, 1.f); break;
    case 10:
        Controller->ReturnToMainMenu();
        SetVolumes(1.f, .6f, 1.f);
        break;
    default: Finish(); break;
    }
}

void ANCAudioValidationRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished) return;
    const double Now = FPlatformTime::Seconds();
    if (Started == 0.0)
    {
        Started = Now;
        FParse::Value(FCommandLine::Get(), TEXT("NeonCoastAudioReport="), ReportPath);
        FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDir);
        ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
        FString ActualSettings;
        if (GConfig)
            if (const FConfigBranch* Branch = GConfig->FindBranch(TEXT("GameUserSettings"), GGameUserSettingsIni))
                ActualSettings = FPaths::ConvertRelativePathToFull(Branch->IniPath);
        FString UserRoot = FPaths::ConvertRelativePathToFull(UserDir);
        FPaths::NormalizeDirectoryName(UserRoot);
        Check(!UserDir.IsEmpty() && ActualSettings.StartsWith(UserRoot + TEXT("/"), ESearchCase::IgnoreCase), TEXT("audio verification uses isolated settings"));
        if (Failures) { Finish(); return; }
    }
    if (Now - Started > 90.0)
    {
        Check(false, TEXT("audio verification completed within 90 seconds"));
        Finish();
        return;
    }
    if (Phase < 0)
    {
        if (Now - Started < 2.0) return;
        Controller = Cast<ANCPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
        Audio = GetWorld()->GetSubsystem<UNCAudioSubsystem>();
        FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw();
        Check(Controller && Audio && Device, TEXT("real world audio device and subsystem exist"));
        Check(Controller && Controller->IsFrontEndOpen() && UGameplayStatics::IsGamePaused(this), TEXT("initial menu is genuinely paused"));
        if (Failures) { Finish(); return; }
        auto* Settings = UNCGameUserSettings::Get();
        OriginalMaster = Settings->GetMasterVolume();
        OriginalMusic = Settings->GetMusicVolume();
        OriginalEffects = Settings->GetEffectsVolume();
        Capture = MakeShared<FNCAudioCapture, ESPMode::ThreadSafe>();
        Device->RegisterSubmixBufferListener(Capture.ToSharedRef(), Device->GetMainSubmixObject());
        BeginPhase(0);
        return;
    }
    if (!bCapturing && Now - PhaseStarted >= .65)
    {
        Capture->Begin();
        bCapturing = true;
        if (Phase == 5) Check(Audio->PlaySettingsPreview(), TEXT("settings preview creates a pause-independent sound"));
    }
    if (Now - PhaseStarted >= 3.15)
    {
        EvaluatePhase();
        BeginPhase(Phase + 1);
    }
}

void ANCAudioValidationRunner::EvaluatePhase()
{
    int32 Channels = 0, Rate = 0;
    const TArray<float> Samples = Capture->End(Channels, Rate);
    double Squares = 0.0;
    float Peak = 0.f;
    bool bFinite = true;
    TArray<int16> PCM;
    PCM.Reserve(Samples.Num());
    for (float Sample : Samples)
    {
        bFinite &= FMath::IsFinite(Sample);
        Squares += double(Sample) * Sample;
        Peak = FMath::Max(Peak, FMath::Abs(Sample));
        PCM.Add(int16(FMath::RoundToInt(FMath::Clamp(Sample, -1.f, 1.f) * 32767.f)));
    }
    const double RMS = Samples.IsEmpty() ? 0.0 : FMath::Sqrt(Squares / Samples.Num());
    const double Seconds = Rate > 0 && Channels > 0 ? double(Samples.Num()) / (Rate * Channels) : 0.0;
    const FString Name(StageNames[Phase]);
    const bool bExpectSilent = Phase == 3 || Phase == 4 || Phase == 9;
    Check(bFinite && Seconds > 1.5 && Channels >= 2 && Rate >= 32000, Name + TEXT(" has finite real-time PCM frames"));
    Check(bExpectSilent ? RMS < 0.0000001 : RMS > 0.0001, Name + (bExpectSilent ? TEXT(" is truly silent") : TEXT(" contains measurable audio output")));
    Check(Peak < .99f, Name + TEXT(" stays below digital clipping"));
    TArray<uint8> Wave;
    const FString WavePath = FPaths::Combine(FPaths::GetPath(ReportPath), Name + TEXT(".wav"));
    if (Channels > 0 && Rate > 0) SerializeWaveFile(Wave, reinterpret_cast<const uint8*>(PCM.GetData()), PCM.Num() * sizeof(int16), Channels, Rate);
    Check(!Wave.IsEmpty() && FFileHelper::SaveArrayToFile(Wave, *WavePath), Name + TEXT(" writes inspectable PCM evidence"));
    Measurements.Add(FString::Printf(TEXT("{\"stage\":%s,\"sample_rate\":%d,\"channels\":%d,\"frames\":%d,\"seconds\":%.6f,\"rms\":%.9f,\"peak\":%.9f,\"expected_silent\":%s,\"wave\":%s}"),
        *Quote(Name), Rate, Channels, Channels ? Samples.Num() / Channels : 0, Seconds, RMS, Peak, bExpectSilent ? TEXT("true") : TEXT("false"), *Quote(WavePath)));
    UE_LOG(LogTemp, Display, TEXT("NC_AUDIO_PCM stage=%s seconds=%.3f channels=%d rate=%d rms=%.9f peak=%.6f"), *Name, Seconds, Channels, Rate, RMS, Peak);
}

void ANCAudioValidationRunner::Finish()
{
    if (bFinished) return;
    bFinished = true;
    if (Audio) SetVolumes(OriginalMaster, OriginalMusic, OriginalEffects);
    const FString Report = FString::Printf(TEXT("{\"status\":\"%s\",\"checks\":%d,\"failures\":%d,\"user_dir\":%s,\"scope\":\"Engine master-submix PCM before Windows endpoint routing; does not prove physical speaker audibility.\",\"measurements\":[%s],\"results\":[%s]}\n"),
        Failures ? TEXT("failed") : TEXT("passed"), Checks, Failures, *Quote(UserDir), *FString::Join(Measurements, TEXT(",\n")), *FString::Join(Results, TEXT(",\n")));
    if (!FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) ++Failures;
    UE_LOG(LogTemp, Display, TEXT("NEON_COAST_AUDIO_%s checks=%d failures=%d report=%s"), Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures, *ReportPath);
    FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ANCAudioValidationRunner::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Capture && GetWorld())
        if (FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw())
            Device->UnregisterSubmixBufferListener(Capture.ToSharedRef(), Device->GetMainSubmixObject());
    Capture.Reset();
    Super::EndPlay(Reason);
}
