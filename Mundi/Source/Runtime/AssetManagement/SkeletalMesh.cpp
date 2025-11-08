#include "pch.h"
#include "SkeletalMesh.h"
#include "FBXManager.h"
#include "PathUtils.h"


void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    FString Normalized = NormalizePath(InFilePath);
    FSkeletalMesh* Asset = FFBXManager::Get().LoadFBXSkeletalMeshAsset(Normalized);
    SetSkeletalMeshAsset(Asset);
}

