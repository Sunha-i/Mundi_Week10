#pragma once
#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
    GENERATED_REFLECTION_BODY()
    DECLARE_DUPLICATE(USkeletalMeshComponent)
    void DuplicateSubObjects() override;
public:
    USkeletalMeshComponent() = default;

protected:
    ~USkeletalMeshComponent() override = default;
};