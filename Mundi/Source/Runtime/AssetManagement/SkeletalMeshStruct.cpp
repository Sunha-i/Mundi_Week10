#include "pch.h"
#include "Bone.h"
#include "SkeletalMeshStruct.h"

FSkeletalMesh::FSkeletalMesh(const FSkeletalMesh& Other) : FMesh(static_cast<const FMesh&>(Other))
{
    // 1. Skeleton 깊은 복사
    if (Other.Skeleton)
    {
        Skeleton = Other.Skeleton->Duplicate();

        // CPU Skinning 최적화: 복사된 Skeleton의 WorldBindPose와 InverseBindPoseMatrix 캐싱
        Skeleton->CacheAllWorldBindPoses();
    }

    // 2. Flesh 배열 복사 (이제 FGroupInfo만 가지므로 간단히 복사)
    Fleshes = Other.Fleshes;

    // 3. SkinnedVertices 복사 및 BonePointers 재매핑
    // 원본과 복사된 Skeleton의 Bone 포인터 매핑을 생성
    if (Skeleton && Other.Skeleton)
    {
        // 원본 Bone 포인터 -> 새 Bone 포인터 매핑 (이름 기반)
        TMap<UBone*, UBone*> BoneRemapping;

        // 원본 Bone 이름 -> 원본 Bone 포인터
        TMap<FString, UBone*> OriginalBoneMap;
        Other.Skeleton->ForEachBone([&](UBone* Bone) {
            if (Bone)
            {
                OriginalBoneMap.Add(Bone->GetName().ToString(), Bone);
            }
        });

        // 새 Bone 이름 -> 새 Bone 포인터
        TMap<FString, UBone*> NewBoneMap;
        Skeleton->ForEachBone([&](UBone* Bone) {
            if (Bone)
            {
                NewBoneMap.Add(Bone->GetName().ToString(), Bone);
            }
        });

        // 매핑 생성 (원본 Bone -> 새 Bone)
        for (const auto& Pair : OriginalBoneMap)
        {
            const FString& BoneName = Pair.first;
            UBone* OriginalBone = Pair.second;

            if (UBone** NewBonePtr = NewBoneMap.Find(BoneName))
            {
                BoneRemapping.Add(OriginalBone, *NewBonePtr);
            }
        }

        // SkinnedVertices 복사 및 BonePointers 재매핑
        SkinnedVertices.resize(Other.SkinnedVertices.Num());
        for (int i = 0; i < Other.SkinnedVertices.Num(); i++)
        {
            SkinnedVertices[i] = Other.SkinnedVertices[i];

            // BonePointers 재매핑 (원본 Skeleton의 Bone -> 새 Skeleton의 Bone)
            for (int j = 0; j < 4; j++)
            {
                UBone* OriginalBone = Other.SkinnedVertices[i].BonePointers[j];
                if (OriginalBone)
                {
                    if (UBone** NewBonePtr = BoneRemapping.Find(OriginalBone))
                    {
                        SkinnedVertices[i].BonePointers[j] = *NewBonePtr;
                    }
                }
            }
        }
    }
    else
    {
        // Skeleton이 없으면 그냥 복사
        SkinnedVertices = Other.SkinnedVertices;
    }
}

FSkeletalMesh::~FSkeletalMesh()
{
    if (Skeleton)
    {
        ObjectFactory::DeleteObject(Skeleton);
    }
}