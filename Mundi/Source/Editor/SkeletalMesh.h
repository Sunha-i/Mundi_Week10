#pragma once
#include "ResourceBase.h"
#include "Skeleton.h"
#include "SkeletalMeshStruct.h"

class USkeletalMesh : public UMeshBase
{
    DECLARE_CLASS(USkeletalMesh, UMeshBase)
public:
    USkeletalMesh() = default;
    virtual ~USkeletalMesh() override = default;

    void Load(
        const FString& InFilePath,
        ID3D11Device* InDevice,
        EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTexturNormal
    ) override;
    virtual void Load(
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
private:
    FSkeletalMesh* SkeletalMeshAsset{};
};