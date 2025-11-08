#include "pch.h"
#include "SkeletalMesh.h"
#include "FbxManager.h"

IMPLEMENT_CLASS(USkeletalMesh)

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    SkeletalMeshAsset = FFbxManager::LoadFbxSkeletalMeshAsset(InFilePath);

    if (SkeletalMeshAsset && 0 < SkeletalMeshAsset->Vertices.size() && 0 < SkeletalMeshAsset->Indices.size())
    {
        CacheFilePath = SkeletalMeshAsset->CacheFilePath;
        CreateVertexBuffer(SkeletalMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(SkeletalMeshAsset, InDevice);
        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());
    }
}

void USkeletalMesh::CreateVertexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    HRESULT hr = D3D11RHI::CreateVertexBuffer<FVertexSkinned>(InDevice, InSkeletalMesh->Vertices, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InSkeletalMesh, &IndexBuffer);
    assert(SUCCEEDED(hr));
}
