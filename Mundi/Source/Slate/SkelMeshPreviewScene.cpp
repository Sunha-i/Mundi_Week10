#include "pch.h"
#include "SkelMeshPreviewScene.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"

void FSkeletalMeshPreviewScene::SetSkelMeshActor(const FString& SkelMeshName)
{
    if (SkeletalMeshActor)
    {
        UE_LOG("[FSkeletalMeshPreviewScene::SetSkelMeshActor] SkeletalMeshActor is already set. Please release it before setting new actor.");
        return;
    }
        
    ASkeletalMeshActor* SkelMeshActor = NewObject<ASkeletalMeshActor>();
    SkelMeshActor->GetSkeletalMeshComponent()->SetSkeletalMesh(SkelMeshName);
    SetActor(SkelMeshActor);

    SkeletalMeshActor = SkelMeshActor;
}

ASkeletalMeshActor* FSkeletalMeshPreviewScene::GetSkelMeshActor()
{
    return SkeletalMeshActor;
}