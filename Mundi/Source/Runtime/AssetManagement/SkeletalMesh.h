#pragma once

#include "ResourceBase.h"
#include "UEContainer.h"
#include "Vector.h"
#include "Enums.h"

// Minimal skeletal mesh resource storing skinned vertices and bones.
class USkeletalMesh : public UResourceBase
{
public:
    DECLARE_CLASS(USkeletalMesh, UResourceBase)

    USkeletalMesh() = default;
    virtual ~USkeletalMesh() override = default;

    // Set data from importer results
    void SetData(const TArray<FSkinnedVertex>& InVertices, const TArray<Bone>& InBones)
    {
        SkinnedVertices = InVertices;
        Bones = InBones;
    }

    const TArray<FSkinnedVertex>& GetVertices() const { return SkinnedVertices; }
    const TArray<Bone>& GetBones() const { return Bones; }

private:
    TArray<FSkinnedVertex> SkinnedVertices;
    TArray<Bone> Bones;
};

