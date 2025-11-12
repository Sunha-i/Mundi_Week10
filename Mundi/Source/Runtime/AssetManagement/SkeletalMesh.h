#pragma once
#include "ResourceBase.h"
#include "Skeleton.h"
#include "SkeletalMeshStruct.h"

class USkeletalMesh : public UMeshBase
{
    DECLARE_CLASS(USkeletalMesh, UMeshBase)
public:
    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void Load(
        const FString& InFilePath,
        ID3D11Device* InDevice,
        EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTexturNormal
    ) override;
    void Load(
        FMeshData* InData,
        ID3D11Device* InDevice,
        EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTexturNormal
    ) override;

    const FString& GetAssetPathFileName() const override;

    FSkeletalMesh* GetSkeletalMeshAsset() const;
    void SetSkeletalMeshAsset(FSkeletalMesh* InSkeletalMesh);

    const TArray<FFlesh>& GetFleshesInfo() const;
    bool HasMaterial() const;

    uint64 GetFleshesCount() const;

    // CPU Skinning - 매 프레임 정점 변환
    void UpdateCPUSkinning(ID3D11DeviceContext* DeviceContext);

    // Serialization & Duplication
    virtual void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
    DECLARE_DUPLICATE(USkeletalMesh)
    void DuplicateSubObjects() override;

private:
    FSkeletalMesh* SkeletalMeshAsset{};

    // CPU Skinning용 변환된 정점 버퍼 (FVertexDynamic 형식으로 GPU 전송)
    TArray<FVertexDynamic> TransformedVertices;

    // Dynamic Vertex Buffer 생성 (CPU 쓰기 가능)
    void CreateDynamicVertexBuffer(ID3D11Device* Device, int VertexCount);
};