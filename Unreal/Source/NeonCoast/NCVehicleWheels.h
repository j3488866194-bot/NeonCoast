#pragma once
#include "CoreMinimal.h"
#include "ChaosVehicleWheel.h"
#include "NCVehicleWheels.generated.h"

UCLASS()
class NEONCOAST_API UNCFrontWheel : public UChaosVehicleWheel
{
    GENERATED_BODY()
public:
    UNCFrontWheel();
};

UCLASS()
class NEONCOAST_API UNCRearWheel : public UChaosVehicleWheel
{
    GENERATED_BODY()
public:
    UNCRearWheel();
};
