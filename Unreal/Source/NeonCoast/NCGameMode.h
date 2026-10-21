#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NCGameMode.generated.h"
UCLASS()
class NEONCOAST_API ANCGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ANCGameMode();
    virtual void StartPlay() override;
};
