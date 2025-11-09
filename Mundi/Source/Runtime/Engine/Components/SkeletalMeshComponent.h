#pragma once
#include "SkinnedMeshComponent.h"

class USkeletalMesh;
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
	GENERATED_REFLECTION_BODY()

	USkeletalMeshComponent();

protected:
	~USkeletalMeshComponent() override;

public:
	void SetSkeletalMesh(const FString& PathFileName);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
	void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial) override;

protected:
	void MarkWorldPartitionDirty();

	USkeletalMesh* SkeletalMesh = nullptr;
	TArray<UMaterialInterface*> MaterialSlots;
};
