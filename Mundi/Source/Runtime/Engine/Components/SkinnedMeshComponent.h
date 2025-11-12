#pragma once
#include "MeshComponent.h"
#include "AABB.h"

class USkeletalMesh;
class UShader;
class UTexture;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FSceneCompData;

class USkinnedMeshComponent : public UMeshComponent
{
    DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
    GENERATED_REFLECTION_BODY()
public:
    USkinnedMeshComponent();

protected:
    ~USkinnedMeshComponent() override;
    void ClearDynamicMaterials();

public :
    // void OnStaticMeshReleased(UStaticMesh* ReleasedMesh);

    void CollectMeshBatches(
        TArray<FMeshBatchElement>& OutMeshBatchElements,
        const FSceneView* View
    ) override;

    void Serialize(
        const bool bInIsLoading,
        JSON& InOutHandle
    ) override;

    void SetSkeletalMesh(const FString& PathFileName);

    USkeletalMesh* GetSkeletalMesh() const;

    UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
    void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial) override;
    
    UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex);
    
    const TArray<UMaterialInterface*> GetMaterialSlots() const;
    
    void SetMaterialTextureByUser(
        const uint32 InMaterialSlotIndex,
        EMaterialTextureSlot Slot,
        UTexture* Texture
    );
    void SetMaterialColorByUser(
        const uint32 InMaterialSlotIndex,
        const FString& ParameterName,
        const FLinearColor& Value
    );
    void SetMaterialScalarByUser(
        const uint32 InMaterialSlotIndex,
        const FString& ParameterName,
        float Value
    );

    FAABB GetWorldAABB() const override;

    DECLARE_DUPLICATE(USkinnedMeshComponent)
    void DuplicateSubObjects() override;

protected:
    void OnTransformUpdated() override;
    void MarkWorldPartitionDirty();

protected:
    USkeletalMesh* SkeletalMesh = nullptr;
    TArray<UMaterialInterface*> MaterialSlots;
    TArray<UMaterialInstanceDynamic*> DynamicMaterialInstances;

    // CPU 스키닝용 동적 버텍스 버퍼
    ID3D11Buffer* DynamicVertexBuffer = nullptr;
    size_t DynamicVertexBufferSize = 0;
};