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

        const auto& V = SkeletalMeshAsset->Vertices;
        FVector MinV = V[0].pos;
        FVector MaxV = V[0].pos;
        for (const auto& SkeletalVertex : V)
        {
            MinV = MinV.ComponentMin(SkeletalVertex.pos);
            MaxV = MaxV.ComponentMax(SkeletalVertex.pos);
        }
        LocalBound = FAABB(MinV, MaxV);

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
    std::vector<FNormalVertex> Temp;
    Temp.reserve(InMesh->Vertices.size());
    for (const FSkinnedVertex& SkinnedVertex : InMesh->Vertices)
    {
        FNormalVertex NormalVertex{};
        NormalVertex.pos = SkinnedVertex.pos;
        NormalVertex.normal = SkinnedVertex.normal;
        NormalVertex.tex = SkinnedVertex.uv;
        NormalVertex.Tangent = FVector4(0,0,0,0);
        NormalVertex.color = FVector4(1,1,1,1);
        Temp.push_back(NormalVertex);
    }

    HRESULT hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(InDevice, Temp, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::CreateIndexBuffer(const FSkeletalMesh* InMesh, ID3D11Device* InDevice)
{
    if (!InMesh || InMesh->Indices.empty()) return;
    if (IndexBuffer) { IndexBuffer->Release(); IndexBuffer = nullptr; }

    D3D11_BUFFER_DESC BufferDesc{};
    BufferDesc.Usage = D3D11_USAGE_DEFAULT;
    BufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    BufferDesc.CPUAccessFlags = 0;
    BufferDesc.ByteWidth = static_cast<UINT>(sizeof(uint32) * InMesh->Indices.size());

    D3D11_SUBRESOURCE_DATA Iinit{};
    Iinit.pSysMem = InMesh->Indices.data();

    HRESULT hr = InDevice->CreateBuffer(&BufferDesc, &Iinit, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::ReleaseResources()
{
    if (VertexBuffer) { VertexBuffer->Release(); VertexBuffer = nullptr; }
    if (IndexBuffer) { IndexBuffer->Release(); IndexBuffer = nullptr; }
}
