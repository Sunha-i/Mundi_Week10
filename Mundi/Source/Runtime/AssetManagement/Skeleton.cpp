#include "pch.h"
#include "Skeleton.h"
#include "Bone.h"

IMPLEMENT_CLASS(USkeleton)

BEGIN_PROPERTIES(USkeleton)
END_PROPERTIES()

USkeleton::USkeleton(UBone* InRoot)
{
    Root = InRoot;
}

USkeleton::~USkeleton()
{
    if (Root)
    {
        ObjectFactory::DeleteObject(Root);
    }
}

void USkeleton::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
    Root = Root->Duplicate();
}

void USkeleton::PostDuplicate() {}

UBone* USkeleton::GetRoot() const
{
    return Root;
}

void USkeleton::SetRoot(UBone* InRoot)
{
    Root = InRoot;
}

void USkeleton::CacheAllWorldBindPoses()
{
    ForEachBone([](UBone* Bone)
    {
        if (Bone)
        {
            Bone->CacheWorldBindPose();
        }
    });
}