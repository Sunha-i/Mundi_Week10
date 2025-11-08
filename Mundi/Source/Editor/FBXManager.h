#pragma once

#include "UEContainer.h"

struct FSkeletalMesh;

struct VKey {
    uint32 cp; int nx, ny, nz; int ux, uy; uint8 bi[4]; uint8 bw[4];
    bool operator==(const VKey& o) const {
        if (cp != o.cp || nx != o.nx || ny != o.ny || nz != o.nz || ux != o.ux || uy != o.uy) return false;
        for (int i = 0;i < 4;++i) { if (bi[i] != o.bi[i] || bw[i] != o.bw[i]) return false; }
        return true;
    }
};


struct VKeyHash {
    size_t operator()(const VKey& k) const {
        size_t h = k.cp;
        auto mix = [&](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix((size_t)k.nx); mix((size_t)k.ny); mix((size_t)k.nz); mix((size_t)k.ux); mix((size_t)k.uy);
        for (int i = 0;i < 4;++i) { mix(k.bi[i]); mix(k.bw[i]); }
        return h;
    }
};


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


