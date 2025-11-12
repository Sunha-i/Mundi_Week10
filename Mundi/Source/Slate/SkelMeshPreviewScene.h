#pragma once
#include "PreviewScene.h"
#include "SkeletalMeshActor.h"

class FSkeletalMeshPreviewScene : public FPreviewScene
{
public:
    void SetSkelMeshActor(const FString& SkelMeshName);
    ASkeletalMeshActor* GetSkelMeshActor();
private:
    ASkeletalMeshActor* SkeletalMeshActor{};
};
