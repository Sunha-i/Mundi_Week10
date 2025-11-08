#pragma once

#include "SkinnedMeshComponent.h"

struct FMeshBatchElement;
class FSceneView;

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
    GENERATED_REFLECTION_BODY()
    USkeletalMeshComponent();
    ~USkeletalMeshComponent();

public:
    void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;
};
