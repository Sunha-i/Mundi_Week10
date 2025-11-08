#include "pch.h"
#include "SkinnedMeshComponent.h"

#include "SkeletalMesh.h"
#include "../GameFramework/World.h"
#include "../../AssetManagement/ResourceManager.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

BEGIN_PROPERTIES(USkinnedMeshComponent)
    ADD_PROPERTY_SKELETALMESH(USkeletalMesh*, SkeletalMesh, "Mesh", true)
END_PROPERTIES()

void USkinnedMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
    SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(PathFileName);
}
