#pragma once
#include "Bone.h"

class USkeleton : public UObject
{
    DECLARE_CLASS(USkeleton, UObject)
    GENERATED_REFLECTION_BODY()
public:
    USkeleton() = default;
    USkeleton(UBone* InRoot);
protected:
    ~USkeleton() override;
public:
    DECLARE_DUPLICATE(USkeleton)
    void DuplicateSubObjects() override;
    void PostDuplicate() override;
    UBone* GetRoot() const;
    void SetRoot(UBone* InRoot);

    // 모든 Bone 노드를 재귀적으로 순회하며 람다 적용
    template<typename Func>
    void ForEachBone(Func&& InFunc);

    // CPU Skinning 최적화: 모든 본의 WorldBindPose와 InverseBindPoseMatrix 캐싱
    void CacheAllWorldBindPoses();

private:
    UBone* Root{};

    // Helper: 재귀적으로 Bone 트리 순회
    template<typename Func>
    void ForEachBoneRecursive(UBone* InBone, Func&& InFunc);
};

// Template 함수 구현 (헤더에 포함해야 함)
template<typename Func>
void USkeleton::ForEachBone(Func&& InFunc)
{
    if (Root)
    {
        ForEachBoneRecursive(Root, std::forward<Func>(InFunc));
    }
}

template<typename Func>
void USkeleton::ForEachBoneRecursive(UBone* InBone, Func&& InFunc)
{
    if (!InBone)
        return;

    // 현재 Bone에 람다 적용
    InFunc(InBone);

    // 자식 Bone들에 재귀 적용
    const TArray<UBone*>& Children = InBone->GetChildren();
    for (UBone* Child : Children)
    {
        ForEachBoneRecursive(Child, std::forward<Func>(InFunc));
    }
}