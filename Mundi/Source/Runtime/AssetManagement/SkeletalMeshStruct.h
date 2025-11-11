#pragma once
#include "Flesh.h"
#include "Skeleton.h"
#include "VertexData.h"

// Raw Data(좀 정리가 되어 있는)
struct FSkeletalMesh : public FMesh
{
    FSkeletalMesh() = default;
    FSkeletalMesh(const FSkeletalMesh& Other);
    ~FSkeletalMesh();

    TArray<FFlesh> Fleshes;
    USkeleton* Skeleton{};

    // CPU Skinning용 원본 정점 데이터 (BindPose + BoneIndices/Weights)
    // FMesh::Vertices는 FNormalVertex (직렬화용)
    // SkinnedVertices는 FSkinnedVertex (CPU Skinning 계산용)
    TArray<FSkinnedVertex> SkinnedVertices;
};