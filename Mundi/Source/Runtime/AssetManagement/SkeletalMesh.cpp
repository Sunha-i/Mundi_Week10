#include "pch.h"
#include "SkeletalMesh.h"
#include "FBXManager.h"
#include "PathUtils.h"
#include "AABB.h"
#include "D3D11RHI.h"
#include "VertexData.h"

IMPLEMENT_CLASS(USkeletalMesh)

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

    // Convert to FNormalVertex so we can reuse the existing FVertexDynamic pipeline (Uber shader)
    std::vector<FNormalVertex> temp;
    temp.reserve(InMesh->Vertices.size());
    for (const FSkinnedVertex& sv : InMesh->Vertices)
    {
        FNormalVertex v{};
        v.pos = sv.pos;
        v.normal = sv.normal;
        v.tex = sv.uv;
        v.Tangent = FVector4(0,0,0,0);
        v.color = FVector4(1,1,1,1);
        temp.push_back(v);
    }

    HRESULT hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(InDevice, temp, &VertexBuffer);
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
