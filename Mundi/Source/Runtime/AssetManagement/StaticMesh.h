#pragma once
#include "MeshBase.h"
#include "Enums.h"

class UStaticMeshComponent;
class FMeshBVH;
class UStaticMesh : public UMeshBase
{
public:
    DECLARE_CLASS(UStaticMesh, UMeshBase)

    UStaticMesh() = default;
    virtual ~UStaticMesh() override = default;

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

	FStaticMesh* GetStaticMeshAsset() const;
    void SetStaticMeshAsset(FStaticMesh* InStaticMesh);

    const TArray<FGroupInfo>& GetMeshGroupInfo() const;
    bool HasMaterial() const;

    uint64 GetMeshGroupCount() const;

private:
	// CPU 리소스
    FStaticMesh* StaticMeshAsset = nullptr;

    // 메시 단위 BVH (ResourceManager에서 캐싱, 소유)
    // 초기화되지 않는 멤버변수 (참조도 ResourceManager에서만 이루어짐) 
    // FMeshBVH* MeshBVH = nullptr;
};

