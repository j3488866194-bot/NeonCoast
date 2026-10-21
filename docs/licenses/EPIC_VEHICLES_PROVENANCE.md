# Epic vehicle template assets

Copied on 2026-10-21 from the locally installed Unreal Engine 5.8.4 distribution, changelist 58902052 (`Engine/Build/Build.version`).

Source directory:

`D:\Work_software\UnrealEngine\UE_5.8\Templates\TemplateResources\Standard\Vehicles\Content`

Project destination:

`Unreal/Content/Vehicles/`

This is the complete shared Vehicles content directory: 59 files, 41,586,003 bytes, including the SportsCar and OffroadCar meshes, skeletal/physics assets, animation blueprints, control rigs, materials, textures and supplied FBX files. Every copied file was compared with its installed source using SHA-256; all 59 matched. The import preserves the original `/Game/Vehicles/` package paths. Runtime paint instances do not modify the copied source assets.

The wheel dimensions and baseline driveline configuration in `NCCar.cpp` and `NCVehicleWheels.cpp` were adapted from Epic's installed `Templates/TP_VehicleAdv/Source/TP_VehicleAdv/` sports-car example. Those source files carry this notice:

> Copyright Epic Games, Inc. All Rights Reserved.

The visible wheel attachment rotations were checked on 2026-10-21 by decoding the installed `Templates/TP_VehicleAdv/Content/VehicleTemplate/Blueprints/SportsCar/BP_SportsCar_Pawn.uasset` component exports: `Wheel_FL` and `Wheel_BL` use `(Pitch=0, Yaw=-90, Roll=0)`, while `Wheel_FR` and `Wheel_BR` use `(0, +90, 0)`. All attach to their corresponding `Phys_Wheel_*` bones without relative-location or scale overrides. `NCCar.cpp` reproduces these saved template rotations; the three-value order was checked against the engine's `Math/Rotator.h` serialization.

These third-party files retain Epic's ownership and applicable Unreal Engine licensing; they are not original Neon Coast assets or relicensed under a project-wide license. The [official Unreal Engine EULA](https://www.unrealengine.com/eula/unreal), inspected on 2026-10-21, identifies content in the installation's Samples and Templates folders as Examples in section 1 and addresses distribution of Examples in section 5(b). This provenance record does not replace that agreement or make a separate license grant.
