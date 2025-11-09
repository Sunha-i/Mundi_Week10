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

protected:
	void MarkWorldPartitionDirty();

	USkeletalMesh* SkeletalMesh = nullptr;
};
