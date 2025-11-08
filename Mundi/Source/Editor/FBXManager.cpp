#include "pch.h"
#include "FBXManager.h"

#include "FBXImporter.h"
#include "PathUtils.h"
#include "SkeletalMesh.h"

FFBXManager& FFBXManager::Get()
{
    static FFBXManager Instance;
    return Instance;
}

void FFBXManager::Clear()
{
    for (auto& Pair : FBXSkeletalMeshMap)
    {
        delete Pair.second;
    }
    FBXSkeletalMeshMap.Empty();
}

USkeletalMesh* FFBXManager::LoadFBXSkeletalMesh(const FString& PathFileName)
{
    if (PathFileName.empty())
    {
        return nullptr;
    }

    FString NormalizedPath = NormalizePath(PathFileName);

    if (USkeletalMesh** Found = FBXSkeletalMeshMap.Find(NormalizedPath))
    {
        return *Found;
    }

    FFBXImporter Importer;
    if (!Importer.LoadFBX(NormalizedPath))
    {
        UE_LOG("FFBXManager: Failed to load FBX '%s'", NormalizedPath.c_str());
        return nullptr;
    }

    USkeletalMesh* NewSkeletal = NewObject<USkeletalMesh>();
    NewSkeletal->SetFilePath(NormalizedPath);
    NewSkeletal->SetData(Importer.SkinnedVertices, Importer.Bones);

    FBXSkeletalMeshMap.Add(NormalizedPath, NewSkeletal);
    return NewSkeletal;
}

