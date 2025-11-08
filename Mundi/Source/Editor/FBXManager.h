#pragma once

#include "UEContainer.h"

struct FSkeletalMesh;

// FBX skeletal mesh manager: caches USkeletalMesh per path in memory
class FFBXManager
{
private:
    TMap<FString, FSkeletalMesh*> FBXSkeletalAssetMap;

public:
    // Singleton accessor for convenience
    static FFBXManager& Get();

    void Clear();
    FSkeletalMesh* LoadFBXSkeletalMeshAsset(const FString& PathFileName);
};


