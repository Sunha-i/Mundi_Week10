#pragma once
#include "Actor.h"

class USkeletalMeshComponent;
class ULineComponent;
class UWorld;
class ASkeletalMeshActor : public AActor
{
public:
    DECLARE_CLASS(ASkeletalMeshActor, AActor)
    GENERATED_REFLECTION_BODY()

    ASkeletalMeshActor(); 
    virtual void Tick(float DeltaTime) override;
protected:
    ~ASkeletalMeshActor() override;

public:
    virtual FAABB GetBounds() const override;
    USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }
    void SetSkeletalMeshComponent(USkeletalMeshComponent* InSkeletalMeshComponent);

    // Duplicate
    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(ASkeletalMeshActor)

    // Serialize
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

    // Editor Debug Skeleton Overlay (owned by actor)
    ULineComponent* GetSkeletonOverlay() const { return SkeletonOverlay; }
    ULineComponent* EnsureSkeletonOverlay(UWorld* World);
    void ClearSkeletonOverlay(bool bDestroyComponent);
    void BuildSkeletonOverlay(class UBone* SelectedBone = nullptr);
private:
    void BuildSkeletonLinesRecursive(class UBone* Bone, const FTransform& ComponentWorldInverse, ULineComponent* Line, class UBone* SelectedBone);
    void AddJointSphereOriented(const FVector& CenterLocal, const FQuat& RotationLocal, ULineComponent* Line, const FVector4& Color);
    void AddBonePyramid(const FVector& ParentLocal, const FVector& ChildLocal, ULineComponent* Line, const FVector4& Color);

protected:
    USkeletalMeshComponent* SkeletalMeshComponent;
    ULineComponent* SkeletonOverlay = nullptr;
};

