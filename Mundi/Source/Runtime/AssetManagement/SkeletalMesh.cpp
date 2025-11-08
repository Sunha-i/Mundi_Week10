#include "pch.h"
#include "SkeletalMesh.h"
#include "FBXManager.h"
#include "PathUtils.h"
#include "AABB.h"
#include <d3d11.h>


void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    FString Normalized = NormalizePath(InFilePath);
    FSkeletalMesh* Asset = FFBXManager::Get().LoadFBXSkeletalMeshAsset(Normalized);
    SetSkeletalMeshAsset(Asset);

    if (SkeletalMeshAsset && !SkeletalMeshAsset->Vertices.empty() && !SkeletalMeshAsset->Indices.empty())
    {
        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());

        // Local AABB from cooked vertices
        const auto& V = SkeletalMeshAsset->Vertices;
        FVector minV = V[0].pos;
        FVector maxV = V[0].pos;
        for (const auto& sv : V)
        {
            minV = minV.ComponentMin(sv.pos);
            maxV = maxV.ComponentMax(sv.pos);
        }
        LocalBound = FAABB(minV, maxV);

        // Create GPU buffers similar to UStaticMesh
        if (InDevice)
        {
            CreateVertexBuffer(SkeletalMeshAsset, InDevice);
            CreateIndexBuffer(SkeletalMeshAsset, InDevice);
        }
    }
}

void USkeletalMesh::CreateVertexBuffer(const FSkeletalMesh* InMesh, ID3D11Device* InDevice)
{
    if (!InMesh || InMesh->Vertices.empty()) return;
    if (VertexBuffer) { VertexBuffer->Release(); VertexBuffer = nullptr; }

    D3D11_BUFFER_DESC vbd{};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;
    vbd.ByteWidth = static_cast<UINT>(sizeof(FSkinnedVertex) * InMesh->Vertices.size());

    D3D11_SUBRESOURCE_DATA vinit{};
    vinit.pSysMem = InMesh->Vertices.data();

    HRESULT hr = InDevice->CreateBuffer(&vbd, &vinit, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::CreateIndexBuffer(const FSkeletalMesh* InMesh, ID3D11Device* InDevice)
{
    if (!InMesh || InMesh->Indices.empty()) return;
    if (IndexBuffer) { IndexBuffer->Release(); IndexBuffer = nullptr; }

    D3D11_BUFFER_DESC ibd{};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32) * InMesh->Indices.size());

    D3D11_SUBRESOURCE_DATA iinit{};
    iinit.pSysMem = InMesh->Indices.data();

    HRESULT hr = InDevice->CreateBuffer(&ibd, &iinit, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::ReleaseResources()
{
    if (VertexBuffer) { VertexBuffer->Release(); VertexBuffer = nullptr; }
    if (IndexBuffer) { IndexBuffer->Release(); IndexBuffer = nullptr; }
}
