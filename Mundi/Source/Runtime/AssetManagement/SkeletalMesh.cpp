#include "pch.h"
#include "SkeletalMesh.h"
#include "FBXManager.h"
#include "PathUtils.h"
#include "AABB.h"


void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    FString Normalized = NormalizePath(InFilePath);
    FSkeletalMesh* Asset = FFBXManager::Get().LoadFBXSkeletalMeshAsset(Normalized);
    SetSkeletalMeshAsset(Asset);

    if (SkeletalMeshAsset && !SkeletalMeshAsset->Vertices.empty() && !SkeletalMeshAsset->Indices.empty())
    {
        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());

        // Local AABB from cooked vertices
        const auto& V = SkeletalMeshAsset->Vertices;
        FVector minV = V[0].pos;
        FVector maxV = V[0].pos;
        for (const auto& sv : V)
        {
            minV = minV.ComponentMin(sv.pos);
            maxV = maxV.ComponentMax(sv.pos);
        }
        LocalBound = FAABB(minV, maxV);
    }
}
