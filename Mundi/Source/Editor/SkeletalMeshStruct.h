#pragma once
#include "Flesh.h"
#include "Skeleton.h"

// Cooked Data
struct FSkeletalMesh : public FMesh
{
    FSkeletalMesh() = default;
    FSkeletalMesh(const FSkeletalMesh& Other);
    ~FSkeletalMesh();
    
    TArray<FFlesh> Fleshes;
    USkeleton* Skeleton{};
};