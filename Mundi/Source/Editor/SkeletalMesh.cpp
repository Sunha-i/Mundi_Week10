#include "pch.h"
#include "SkeletalMesh.h"

#include "FbxManager.h"

IMPLEMENT_CLASS(USkeletalMesh)

void USkeletalMesh::Load(
    const FString& InFilePath,
    ID3D11Device* InDevice,
    EVertexLayoutType InVertexType
)
{
    assert(InDevice);

    SetVertexType(InVertexType);

    FFbxManager& FbxManager = FFbxManager::GetInstance();

    FSkeletalMesh* LoadedMesh = FbxManager.LoadFbxSkeletalMeshAsset(InFilePath);

    // 빈 버텍스, 인덱스로 버퍼 생성 방지
    if (LoadedMesh &&
        0 < LoadedMesh->Vertices.size() &&
        0 < LoadedMesh->Indices.size()
    )
    {
        // 복사 생성자로 Bone, Flesh의 정보를 깊은 복사하여야 함.
        SkeletalMeshAsset = new FSkeletalMesh(*LoadedMesh);
        CacheFilePath = SkeletalMeshAsset->CacheFilePath;
        CreateVertexBuffer(SkeletalMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(SkeletalMeshAsset, InDevice);
        CreateLocalBound(SkeletalMeshAsset);
        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());
    }
}
void USkeletalMesh::Load(
    FMeshData* InData,
    ID3D11Device* InDevice,
    EVertexLayoutType InVertexType
)
{
    SetVertexType(InVertexType);

    if (VertexBuffer)
    {
        VertexBuffer->Release();
        VertexBuffer = nullptr;
    }
    if (IndexBuffer)
    {
        IndexBuffer->Release();
        IndexBuffer = nullptr;
    }

    CreateVertexBuffer(InData, InDevice, InVertexType);
    CreateIndexBuffer(InData, InDevice);
    CreateLocalBound(InData);

    VertexCount = static_cast<uint32>(InData->Vertices.size());
    IndexCount = static_cast<uint32>(InData->Indices.size());
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
    return SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : FilePath;
}

FSkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const
{
    return SkeletalMeshAsset;
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InSkeletalMesh)
{
    SkeletalMeshAsset = InSkeletalMesh;
}

const TArray<FFlesh>& USkeletalMesh::GetFleshesInfo() const
{
    if (!SkeletalMeshAsset)
    {
        MessageBoxA(
            nullptr,
            "USkeletalMesh::GetFleshesInfo : SkeletalMesh is nullptr",
            "Error",
            1
        );

        exit(1);
    }

    return SkeletalMeshAsset->Fleshes;
}
bool USkeletalMesh::HasMaterial() const
{
    return SkeletalMeshAsset->bHasMaterial;
}

uint64 USkeletalMesh::GetFleshesCount() const
{
    if (!SkeletalMeshAsset)
    {
        MessageBoxA(
            nullptr,
            "USkeletalMesh::GetFleshesInfo : SkeletalMesh is nullptr",
            "Error",
            1
        );

        exit(1);
    }
    
    return SkeletalMeshAsset->Fleshes.Num();
}