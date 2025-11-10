#pragma once
#include "ResourceBase.h"
#include "MeshBVH.h"

class UMeshBase : public UResourceBase
{
    DECLARE_CLASS(UMeshBase, UResourceBase)
public:
    UMeshBase() = default;
    virtual ~UMeshBase() override;

    virtual void Load(
        const FString& InFilePath,
        ID3D11Device* InDevice,
        EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTexturNormal
    ) {}
    virtual void Load(
        FMeshData* InData,
        ID3D11Device* InDevice,
        EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTexturNormal
    ) {}

    virtual const FString& GetAssetPathFileName() const { return FString(); }

    ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer; }
    ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }
    uint32 GetVertexCount() const { return VertexCount; }
    uint32 GetIndexCount() const { return IndexCount; }
    void SetVertexType(EVertexLayoutType InVertexLayoutType);
    EVertexLayoutType GetVertexType() const { return VertexType; }
    void SetIndexCount(uint32 Cnt) { IndexCount = Cnt; }
    uint32 GetVertexStride() const { return VertexStride; };

    FAABB GetLocalBound() const {return LocalBound; }
    const FString& GetCacheFilePath() const { return CacheFilePath; }

protected:
    void CreateVertexBuffer(FMeshData* InMeshData, ID3D11Device* InDevice, EVertexLayoutType InVertexType);
    void CreateVertexBuffer(FMesh* InMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType);
    void CreateIndexBuffer(FMeshData* InMeshData, ID3D11Device* InDevice);
    void CreateIndexBuffer(FMesh* InMesh, ID3D11Device* InDevice);
    void CreateLocalBound(const FMeshData* InMeshData);
    void CreateLocalBound(const FMesh* InMesh);
    void ReleaseResources();
    
    FString CacheFilePath;  // 캐시된 소스 경로 (예: DerivedDataCache/cube.obj.bin)

    // GPU 리소스
    ID3D11Buffer* VertexBuffer = nullptr;
    ID3D11Buffer* IndexBuffer = nullptr;
    uint32 VertexCount = 0;     // 정점 개수
    uint32 IndexCount = 0;     // 버텍스 점의 개수 
    uint32 VertexStride = 0;
    EVertexLayoutType VertexType = EVertexLayoutType::PositionColorTexturNormal;  // Stride를 계산하기 위한 버텍스 타입

    // 로컬 AABB. (스태틱메시 액터 전체 경계 계산에 사용. StaticMeshAsset 로드할 때마다 갱신)
    FAABB LocalBound;
};