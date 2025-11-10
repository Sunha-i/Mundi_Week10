#pragma once
#include "Flesh.h"
#include "Skeleton.h"

// Raw Data(좀 정리가 되어 있는)
struct FSkeletalMesh : public FMesh
{
    FSkeletalMesh() = default;
    FSkeletalMesh(const FSkeletalMesh& Other);
    ~FSkeletalMesh();
    
    TArray<FFlesh> Fleshes;
    USkeleton* Skeleton{};
};