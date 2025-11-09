#include "pch.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "WorldPartitionManager.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)
BEGIN_PROPERTIES(USkeletalMeshComponent)
	MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링하는 컴포넌트입니다.")
	ADD_PROPERTY_SKELETALMESH(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
	USkeletalMesh* NewSkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(PathFileName);
	if (NewSkeletalMesh && NewSkeletalMesh->GetSkeletalMeshAsset())
	{
		if (SkeletalMesh != NewSkeletalMesh)
		{
			SkeletalMesh = NewSkeletalMesh;
			MarkWorldPartitionDirty();
		}
	}
	else
	{
		SkeletalMesh = nullptr;
		MarkWorldPartitionDirty();
	}
}

void USkeletalMeshComponent::MarkWorldPartitionDirty()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldPartitionManager* Partition = World->GetPartitionManager())
		{
			Partition->MarkDirty(this);
		}
	}
}