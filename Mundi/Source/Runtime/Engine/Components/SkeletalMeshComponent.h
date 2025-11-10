#pragma once
#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
    GENERATED_REFLECTION_BODY()
public:
    USkeletalMeshComponent() = default;

protected:
    ~USkeletalMeshComponent() override = default;
};