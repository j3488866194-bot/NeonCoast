#if WITH_DEV_AUTOMATION_TESTS

#include "NCSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCSaveRoundTripTest, "NeonCoast.Progress.SaveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCSaveRoundTripTest::RunTest(const FString& Parameters)
{
    UNCSaveGame* Original = NewObject<UNCSaveGame>();
    Original->Cash = 1875;
    Original->CollectedMask = 137;
    Original->CompletedDeliveries = 2;
    Original->RaceWins = 3;
    Original->BestRaceSeconds = 54.25f;
    Original->MissionStage = 2;
    Original->WantedHeat = 1.4f;
    Original->LastCrimeLocation = FVector(14400, 7200, 72);
    TArray<uint8> Bytes;
    TestTrue(TEXT("progress serializes without touching any user slot"), UGameplayStatics::SaveGameToMemory(Original, Bytes));
    UNCSaveGame* Restored = Cast<UNCSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("save class survives serialization"), Restored)) return false;
    TestTrue(TEXT("restored progress validates"), Restored->IsSane());
    TestEqual(TEXT("cash restored"), Restored->Cash, 1875);
    TestEqual(TEXT("token mask restored"), Restored->CollectedMask, 137);
    TestEqual(TEXT("courier checkpoint restored"), Restored->MissionStage, 2);
    TestEqual(TEXT("wanted heat restored"), Restored->WantedHeat, 1.4f);
    TestEqual(TEXT("race record restored"), Restored->BestRaceSeconds, 54.25f);
    TestEqual(TEXT("search location restored"), Restored->LastCrimeLocation, FVector(14400, 7200, 72));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCSaveRejectInvalidTest, "NeonCoast.Progress.RejectInvalidSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNCSaveRejectInvalidTest::RunTest(const FString& Parameters)
{
    UNCSaveGame* Save = NewObject<UNCSaveGame>();
    TestTrue(TEXT("default progress is usable"), Save->IsSane());
    Save->Version = 99;
    TestFalse(TEXT("future save version is not silently interpreted"), Save->IsSane());
    Save->Version = 1;
    Save->CollectedMask = 256;
    TestFalse(TEXT("unknown token bits are rejected"), Save->IsSane());
    Save->CollectedMask = 0;
    Save->Cash = -1;
    TestFalse(TEXT("negative cash is rejected"), Save->IsSane());
    Save->Cash = 250;
    Save->MissionStage = 4;
    TestFalse(TEXT("unknown mission checkpoint is rejected"), Save->IsSane());
    Save->MissionStage = 0;
    Save->WantedHeat = 3.1f;
    TestFalse(TEXT("invalid pursuit state is rejected"), Save->IsSane());
    Save->WantedHeat = 0.f;
    Save->BestRaceSeconds = 100.f;
    TestFalse(TEXT("impossible successful race record is rejected"), Save->IsSane());
    return true;
}

#endif
