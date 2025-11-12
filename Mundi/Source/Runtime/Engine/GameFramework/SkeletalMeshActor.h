#pragma once
#include "Actor.h"

class USkeletalMeshComponent;
class ULineComponent;
class UWorld;

// Temporary line data for skeleton overlay optimization
struct FLineData
{
    FVector Start;
    FVector End;
    FVector4 Color;

    FLineData() = default;
    FLineData(const FVector& InStart, const FVector& InEnd, const FVector4& InColor)
        : Start(InStart), End(InEnd), Color(InColor) {}
};

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
    void BuildSkeletonLinesRecursive_Temp(class UBone* Bone, const FTransform& ComponentWorldInverse, TArray<FLineData>& OutLines, class UBone* SelectedBone);
    void AddJointSphereOriented(const FVector& CenterLocal, const FQuat& RotationLocal, ULineComponent* Line, const FVector4& Color);
    void AddJointSphereOriented_Temp(const FVector& CenterLocal, const FQuat& RotationLocal, TArray<FLineData>& OutLines, const FVector4& Color);
    void AddBonePyramid(const FVector& ParentLocal, const FVector& ChildLocal, ULineComponent* Line, const FVector4& Color);
    void AddBonePyramid_Temp(const FVector& ParentLocal, const FVector& ChildLocal, TArray<FLineData>& OutLines, const FVector4& Color);

protected:
    USkeletalMeshComponent* SkeletalMeshComponent;
    ULineComponent* SkeletonOverlay = nullptr;
};

