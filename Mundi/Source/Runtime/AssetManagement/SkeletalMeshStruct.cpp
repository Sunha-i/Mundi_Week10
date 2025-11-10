#include "pch.h"
#include "Bone.h"
#include "SkeletalMeshStruct.h"

FSkeletalMesh::FSkeletalMesh(const FSkeletalMesh& Other) : FMesh(static_cast<const FMesh&>(Other))
{
    // Skeleton 복사 (있을 경우에만)
    TMap<FString, UBone*> BoneMap;

    if (Other.Skeleton)
    {
        Skeleton = Other.Skeleton->Duplicate();

        // BoneMap 생성: 복사된 Skeleton의 모든 Bone을 이름으로 매핑
        Skeleton->ForEachBone([&BoneMap](UBone* Bone) {
            if (Bone)
            {
                FString BoneName = Bone->GetName().ToString();
                BoneMap.Add(BoneName, Bone);
            }
        });
    }

    // Flesh 배열 복사 (Skeleton 유무와 관계없이 항상 복사)
    for (const FFlesh& OtherFlesh : Other.Fleshes)
    {
        FFlesh NewFlesh;

        // FGroupInfo 멤버 복사 (StartIndex, IndexCount, InitialMaterialName)
        NewFlesh.StartIndex = OtherFlesh.StartIndex;
        NewFlesh.IndexCount = OtherFlesh.IndexCount;
        NewFlesh.InitialMaterialName = OtherFlesh.InitialMaterialName;

        // Bones 배열 복사 (Skeleton이 있을 경우에만)
        if (Skeleton)
        {
            for (int i = 0; i < OtherFlesh.Bones.Num(); i++)
            {
                UBone* OtherBone = OtherFlesh.Bones[i];
                if (OtherBone)
                {
                    FString BoneName = OtherBone->GetName().ToString();
                    UBone* const* FoundBone = BoneMap.Find(BoneName);

                    if (FoundBone && *FoundBone)
                    {
                        NewFlesh.Bones.Add(*FoundBone);
                    }
                }
            }
        }

        // Weights 배열 복사 (값 타입이므로 단순 복사)
        NewFlesh.Weights = OtherFlesh.Weights;
        NewFlesh.WeightsTotal = OtherFlesh.WeightsTotal;

        Fleshes.Add(NewFlesh);
    }
}

FSkeletalMesh::~FSkeletalMesh()
{
    if (Skeleton)
    {
        ObjectFactory::DeleteObject(Skeleton);
    }
}