#pragma once

#include "MeshComponent.h"

class USkeletalMesh;

class USkinnedMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
    GENERATED_REFLECTION_BODY()

    USkinnedMeshComponent() = default;
    ~USkinnedMeshComponent() override = default;

    // Set from cached/loaded FBX 
    void SetSkeletalMesh(const FString& PathFileName);
    // Directly assign an instance
    void SetSkeletalMesh(USkeletalMesh* InMesh) { SkeletalMesh = InMesh; }
    USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

private:
    USkeletalMesh* SkeletalMesh = nullptr;
};
