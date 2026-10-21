#pragma once
#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"

// Lightweight stand-ins until character/vehicle art assets are authored.
namespace NCPrimitives
{
inline UStaticMeshComponent* Part(AActor* Owner, USceneComponent* Parent, const TCHAR* Name,
    const TCHAR* Shape, FVector Location, FVector Scale, FLinearColor Color)
{
    auto* Part = NewObject<UStaticMeshComponent>(Owner, FName(Name));
    Part->SetupAttachment(Parent);
    Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
    Part->SetRelativeLocation(Location);
    Part->SetRelativeScale3D(Scale);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false);
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Coast.M_Coast"));
    if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Base)
    {
        auto* Material = UMaterialInstanceDynamic::Create(Base, Owner);
        Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Material->SetScalarParameterValue(TEXT("Roughness"), .55f);
        Part->SetMaterial(0, Material);
    }
    Part->RegisterComponent();
    Owner->AddInstanceComponent(Part);
    return Part;
}
}
