#include "pch.h"
#include "Bone.h"
#include "SkeletalMeshStruct.h"

FSkeletalMesh::FSkeletalMesh(const FSkeletalMesh& Other) : FMesh(static_cast<const FMesh&>(Other))
{
    // 1. Skeleton 깊은 복사
    if (Other.Skeleton)
    {
        Skeleton = Other.Skeleton->Duplicate();
    }

    // 2. Flesh 배열 복사 (이제 FGroupInfo만 가지므로 간단히 복사)
    Fleshes = Other.Fleshes;

    // 3. SkinnedVertices 복사 및 BoneIndices 재매핑
    // 원본과 복사된 Skeleton의 Bone 순서 매핑을 생성
    if (Skeleton && Other.Skeleton)
    {
        // 원본 Skeleton의 Bone 순서 (이름 -> 인덱스)
        TMap<FString, int32> OriginalBoneNameToIndex;
        int32 OriginalIndex = 0;
        Other.Skeleton->ForEachBone([&](UBone* Bone) {
            if (Bone)
            {
                OriginalBoneNameToIndex.Add(Bone->GetName().ToString(), OriginalIndex++);
            }
        });

        // 복사된 Skeleton의 Bone 순서 (이름 -> 인덱스)
        TMap<FString, int32> NewBoneNameToIndex;
        int32 NewIndex = 0;
        Skeleton->ForEachBone([&](UBone* Bone) {
            if (Bone)
            {
                NewBoneNameToIndex.Add(Bone->GetName().ToString(), NewIndex++);
            }
        });

        // 인덱스 재매핑 테이블 생성 (원본 인덱스 -> 새 인덱스)
        TArray<int32> IndexRemapping;
        IndexRemapping.resize(OriginalBoneNameToIndex.Num(), -1);

        for (const auto& Pair : OriginalBoneNameToIndex)
        {
            const FString& BoneName = Pair.first;
            int32 OldIndex = Pair.second;

            if (const int32* NewIndexPtr = NewBoneNameToIndex.Find(BoneName))
            {
                IndexRemapping[OldIndex] = *NewIndexPtr;
            }
        }

        // SkinnedVertices 복사 및 BoneIndices 재매핑
        SkinnedVertices.resize(Other.SkinnedVertices.Num());
        for (int i = 0; i < Other.SkinnedVertices.Num(); i++)
        {
            SkinnedVertices[i] = Other.SkinnedVertices[i];

            // BoneIndices 재매핑
            for (int j = 0; j < 4; j++)
            {
                uint32 OldBoneIndex = Other.SkinnedVertices[i].BoneIndices[j];
                if (OldBoneIndex < (uint32)IndexRemapping.Num())
                {
                    int32 NewBoneIndex = IndexRemapping[OldBoneIndex];
                    if (NewBoneIndex >= 0)
                    {
                        SkinnedVertices[i].BoneIndices[j] = (uint32)NewBoneIndex;
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