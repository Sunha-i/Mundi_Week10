#include "pch.h"
#include "StaticMesh.h"
#include "StaticMeshComponent.h"
#include "ObjManager.h"
#include "ResourceManager.h"

IMPLEMENT_CLASS(UStaticMesh)

void UStaticMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    SetVertexType(InVertexType);

    StaticMeshAsset = FObjManager::LoadObjStaticMeshAsset(InFilePath);

    // 빈 버텍스, 인덱스로 버퍼 생성 방지
    if (StaticMeshAsset && 0 < StaticMeshAsset->Vertices.size() && 0 < StaticMeshAsset->Indices.size())
    {
        CacheFilePath = StaticMeshAsset->CacheFilePath;
        CreateVertexBuffer(StaticMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(StaticMeshAsset, InDevice);
        CreateLocalBound(StaticMeshAsset);
        VertexCount = static_cast<uint32>(StaticMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(StaticMeshAsset->Indices.size());
    }
}

void UStaticMesh::Load(FMeshData* InData, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
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

const FString& UStaticMesh::GetAssetPathFileName() const
{
    return StaticMeshAsset ? StaticMeshAsset->PathFileName : FilePath;
}

void UStaticMesh::SetStaticMeshAsset(FStaticMesh* InStaticMesh)
{
    StaticMeshAsset = InStaticMesh;
}

FStaticMesh* UStaticMesh::GetStaticMeshAsset() const
{
    return StaticMeshAsset;
}

const TArray<FGroupInfo>& UStaticMesh::GetMeshGroupInfo() const
{
    return StaticMeshAsset->GroupInfos;
}

bool UStaticMesh::HasMaterial() const
{
    return StaticMeshAsset->bHasMaterial;
}

uint64 UStaticMesh::GetMeshGroupCount() const
{
    return StaticMeshAsset->GroupInfos.size();
}
