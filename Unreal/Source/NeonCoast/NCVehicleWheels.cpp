#include "NCVehicleWheels.h"

// Dimensions and driveline follow Epic's installed UE 5.8 SportsCar template.
UNCFrontWheel::UNCFrontWheel()
{
    AxleType=EAxleType::Front;
    bAffectedBySteering=true;
    bAffectedByEngine=false;
    MaxSteerAngle=40.f;
    WheelRadius=39.f;
    WheelWidth=35.f;
    FrictionForceMultiplier=3.f;
    MaxBrakeTorque=4500.f;
    MaxHandBrakeTorque=6000.f;
    SuspensionDampingRatio=.7f;
    bABSEnabled=true;
}

UNCRearWheel::UNCRearWheel()
{
    AxleType=EAxleType::Rear;
    bAffectedByEngine=true;
    bAffectedByHandbrake=true;
    bAffectedBySteering=false;
    MaxSteerAngle=0.f;
    WheelRadius=40.f;
    WheelWidth=40.f;
    FrictionForceMultiplier=4.f;
    SlipThreshold=100.f;
    SkidThreshold=100.f;
    MaxHandBrakeTorque=6000.f;
    SuspensionDampingRatio=.7f;
    bTractionControlEnabled=true;
    bABSEnabled=true;
}
