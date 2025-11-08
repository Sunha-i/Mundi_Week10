#pragma once

#include "UEContainer.h"

class USkeletalMesh;

// FBX skeletal mesh manager: caches USkeletalMesh per path in memory
class FFBXManager
{
private:
    TMap<FString, USkeletalMesh*> FBXSkeletalMeshMap;

public:
    // Singleton accessor for convenience
    static FFBXManager& Get();

    void Clear();
    USkeletalMesh* LoadFBXSkeletalMesh(const FString& PathFileName);
};

