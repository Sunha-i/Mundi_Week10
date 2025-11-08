#include "pch.h"
#include "MeshBase.h"

IMPLEMENT_CLASS(UMeshBase)

UMeshBase::~UMeshBase()
{
    ReleaseResources();
}

void UMeshBase::SetVertexType(EVertexLayoutType InVertexType)
{
    VertexType = InVertexType;

    uint32 Stride = 0;
    switch (InVertexType)
    {
    case EVertexLayoutType::PositionColor:
        Stride = sizeof(FVertexSimple);
        break;
    case EVertexLayoutType::PositionColorTexturNormal:
        Stride = sizeof(FVertexDynamic);
        break;
    case EVertexLayoutType::PositionTextBillBoard:
        Stride = sizeof(FBillboardVertexInfo_GPU);
        break;
    case EVertexLayoutType::PositionBillBoard:
        Stride = sizeof(FBillboardVertex);
        break;
    default:
        assert(false && "Unknown vertex type!");
    }

    VertexStride = Stride;
}

void UMeshBase::CreateVertexBuffer(FMeshData* InMeshData, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    HRESULT hr;
    hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(InDevice, *InMeshData, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void UMeshBase::CreateVertexBuffer(FMesh* InMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    HRESULT hr;
    hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(InDevice, InMesh->Vertices, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void UMeshBase::CreateIndexBuffer(FMeshData* InMeshData, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InMeshData, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

void UMeshBase::CreateIndexBuffer(FMesh* InMesh, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InMesh, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

void UMeshBase::CreateLocalBound(const FMeshData* InMeshData)
{
    TArray<FVector> Verts = InMeshData->Vertices;
    FVector Min = Verts[0];
    FVector Max = Verts[0];
    for (FVector Vertex : Verts)
    {
        Min = Min.ComponentMin(Vertex);
        Max = Max.ComponentMax(Vertex);
    }
    LocalBound = FAABB(Min, Max);
}

void UMeshBase::CreateLocalBound(const FMesh* InMesh)
{
    TArray<FNormalVertex> Verts = InMesh->Vertices;
    FVector Min = Verts[0].pos;
    FVector Max = Verts[0].pos;
    for (FNormalVertex Vertex : Verts)
    {
        FVector Pos = Vertex.pos;
        Min = Min.ComponentMin(Pos);
        Max = Max.ComponentMax(Pos);
    }
    LocalBound = FAABB(Min, Max);
}

void UMeshBase::ReleaseResources()
{
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
}