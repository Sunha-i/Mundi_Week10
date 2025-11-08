#pragma once

#include "ResourceBase.h"
#include "UEContainer.h"
#include "Vector.h"
#include "Enums.h"

// Cooked skeletal data stored in cache and used by USkeletalMesh
struct FSkeletalMesh
{
    FString PathFileName;
    FString CacheFilePath;

    TArray<FSkinnedVertex> Vertices;
    TArray<uint32> Indices;
    TArray<Bone> Bones;

    friend FArchive& operator<<(FArchive& Ar, FSkeletalMesh& Mesh)
    {
        if (Ar.IsSaving())
        {
            Serialization::WriteString(Ar, Mesh.PathFileName);
            Serialization::WriteArray(Ar, Mesh.Vertices);
            Serialization::WriteArray(Ar, Mesh.Indices);
            Serialization::WriteArray(Ar, Mesh.Bones);
        }
        else if (Ar.IsLoading())
        {
            Serialization::ReadString(Ar, Mesh.PathFileName);
            Serialization::ReadArray(Ar, Mesh.Vertices);
            Serialization::ReadArray(Ar, Mesh.Indices);
            Serialization::ReadArray(Ar, Mesh.Bones);
        }
        return Ar;
    }
};

class USkeletalMesh : public UResourceBase
{
public:
    DECLARE_CLASS(USkeletalMesh, UResourceBase)

    USkeletalMesh() = default;
    virtual ~USkeletalMesh() override = default;

    void Load(const FString& InFilePath, ID3D11Device* InDevice);

    void SetSkeletalMeshAsset(FSkeletalMesh* In) { SkeletalMeshAsset = In; }
    FSkeletalMesh* GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }

private:
    FSkeletalMesh* SkeletalMeshAsset = nullptr;
};

