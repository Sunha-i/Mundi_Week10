#pragma once

#include "SkinnedMeshComponent.h"
#include "Material.h"
#include "Color.h"

struct FMeshBatchElement;
class FSceneView;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
    GENERATED_REFLECTION_BODY()
    USkeletalMeshComponent();
    ~USkeletalMeshComponent();

public:
    void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

    // Material interface parity with StaticMeshComponent
    UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
    void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial) override;

private:
    TArray<UMaterialInterface*> MaterialSlots;
    TArray<UMaterialInstanceDynamic*> DynamicMaterialInstances;

public:
    // Parity functions for Property UI editing
    UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex);
    void SetMaterialTextureByUser(const uint32 InMaterialSlotIndex, EMaterialTextureSlot Slot, UTexture* Texture);
    void SetMaterialColorByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, const FLinearColor& Value);
    void SetMaterialScalarByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, float Value);

    // Persist materials like StaticMeshComponent
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
    virtual void DuplicateSubObjects() override;

private:
    void ClearDynamicMaterials();
};
