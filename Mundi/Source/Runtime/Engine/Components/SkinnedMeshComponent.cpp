#include "pch.h"
#include "SkinnedMeshComponent.h"

#include "SkeletalMesh.h"
#include "../GameFramework/World.h"
#include "../../AssetManagement/ResourceManager.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

void USkinnedMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
    SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(PathFileName);
}
