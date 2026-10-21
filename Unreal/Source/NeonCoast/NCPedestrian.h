#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NCPedestrian.generated.h"
class UTextRenderComponent;
UCLASS()
class NEONCOAST_API ANCPedestrian : public ACharacter
{
    GENERATED_BODY()
public:
    ANCPedestrian();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void Greet(AActor* Speaker);
    int32 AppearanceSeed=0;
    FVector WalkAxis=FVector(0,1,0);
    float RouteHalfLength=750;
protected:
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Limbs;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Speech;
    UPROPERTY() TObjectPtr<AActor> ConversationPartner;
    FVector Origin;
    float Direction=1, PauseTime=0, PanicTime=0, ConversationTime=0, Gait=0, SenseClock=0;
    void NoticeVehicles();
};
