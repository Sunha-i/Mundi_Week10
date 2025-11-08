#include "pch.h"
#include "SkinnedMeshComponent.h"

#include "SkeletalMesh.h"
#include "../GameFramework/World.h"
#include "../../AssetManagement/ResourceManager.h"
#include "FBXManager.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

void USkinnedMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
    SkeletalMesh = FFBXManager::Get().LoadFBXSkeletalMesh(PathFileName);
}
