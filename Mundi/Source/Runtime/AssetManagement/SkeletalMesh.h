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
    // Sections (material groups) for multi-material support
    TArray<FGroupInfo> Sections;
    bool bHasMaterial = false;

    friend FArchive& operator<<(FArchive& Ar, FSkeletalMesh& Mesh)
    {
        if (Ar.IsSaving())
        {
            Serialization::WriteString(Ar, Mesh.PathFileName);
            Serialization::WriteArray(Ar, Mesh.Vertices);
            Serialization::WriteArray(Ar, Mesh.Indices);
            Serialization::WriteArray(Ar, Mesh.Bones);
            // Write sections
            uint32_t secCount = (uint32_t)Mesh.Sections.size();
            Ar << secCount;
            for (auto& s : Mesh.Sections) Ar << s;
            Ar << Mesh.bHasMaterial;
        }
        else if (Ar.IsLoading())
        {
            Serialization::ReadString(Ar, Mesh.PathFileName);
            Serialization::ReadArray(Ar, Mesh.Vertices);
            Serialization::ReadArray(Ar, Mesh.Indices);
            Serialization::ReadArray(Ar, Mesh.Bones);
            // Read sections
            uint32_t secCount = 0;
            Ar << secCount;
            Mesh.Sections.resize(secCount);
            for (auto& s : Mesh.Sections) Ar << s;
            Ar << Mesh.bHasMaterial;
        }
        return Ar;
    }
};

class USkeletalMesh : public UResourceBase
{
public:
    DECLARE_CLASS(USkeletalMesh, UResourceBase)

    USkeletalMesh() = default;
    virtual ~USkeletalMesh() override { ReleaseResources(); }

    void Load(const FString& InFilePath, ID3D11Device* InDevice);

    void SetSkeletalMeshAsset(FSkeletalMesh* In) { SkeletalMeshAsset = In; }
    FSkeletalMesh* GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }
    const FString& GetCacheFilePath() const { static FString Empty; return SkeletalMeshAsset ? SkeletalMeshAsset->CacheFilePath : Empty; }
    const FString& GetAssetPathFileName() const { return SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : FilePath; }

    // (미래) GPU 리소스 연동을 위한 접근자
    ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer; }
    ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }
    uint32 GetVertexCount() const { return VertexCount; }
    uint32 GetIndexCount() const { return IndexCount; }
    FAABB GetLocalBound() const { return LocalBound; }

private:
    FSkeletalMesh* SkeletalMeshAsset = nullptr;
    ID3D11Buffer* VertexBuffer = nullptr;
    ID3D11Buffer* IndexBuffer = nullptr;
    uint32 VertexCount = 0;
    uint32 IndexCount = 0;
    FAABB LocalBound;

private:
    void CreateVertexBuffer(const FSkeletalMesh* InMesh, ID3D11Device* InDevice);
    void CreateIndexBuffer(const FSkeletalMesh* InMesh, ID3D11Device* InDevice);
    void ReleaseResources();
};

