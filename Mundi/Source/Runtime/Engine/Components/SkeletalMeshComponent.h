#pragma once

#include "SkinnedMeshComponent.h"

struct FMeshBatchElement;
class FSceneView;
class UMaterialInterface;

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
};
