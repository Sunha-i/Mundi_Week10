#include "pch.h"
#include "Bone.h"

IMPLEMENT_CLASS(UBone)

BEGIN_PROPERTIES(UBone)
    ADD_PROPERTY(FName, Name, "[데이터]", true, "뼈의 이름입니다")
    ADD_PROPERTY(FTransform, RelativeTransform, "[데이터]", true, "뼈의 로컬 트랜스폼입니다")
    ADD_PROPERTY(FTransform, BindPose, "[데이터]", true, "뼈의 초기 트랜스폼입니다")
END_PROPERTIES()

UBone::UBone(const FName& InName, const FTransform& InitialTransform) :
    Name(InName),
    RelativeTransform(InitialTransform),
    BindPose(InitialTransform) {}

UBone::~UBone()
{
    for (UBone* Child : Children)
    {
        if (Child)
        {
            ObjectFactory::DeleteObject(Child);
        }
    }
}

const FName& UBone::GetName()
{
    return Name;
}
void UBone::SetName(const FName& InName)
{
    Name = InName;
}

const FVector& UBone::GetRelativeLocation() const
{
    return RelativeTransform.Translation;
}

const FQuat& UBone::GetRelativeRotation() const
{
    return RelativeTransform.Rotation;
}

const FVector& UBone::GetRelativeScale() const
{
    return RelativeTransform.Scale3D;
}

const FTransform& UBone::GetRelativeTransform() const
{
    return RelativeTransform;
}

void UBone::SetRelativeLocation(const FVector& InLocation)
{
    RelativeTransform.Translation = InLocation;
}

void UBone::SetRelativeRotation(const FQuat& InRotatation)
{
    RelativeTransform.Rotation = InRotatation;
}

void UBone::SetRelativeScale(const FVector& InScale)
{
    RelativeTransform.Scale3D = InScale;
}

void UBone::SetRelativeTransform(const FTransform& InRelativeTransform)
{
    RelativeTransform = InRelativeTransform;
}

const FVector& UBone::GetWorldLocation() const
{
    if (!Parent)
        return GetRelativeLocation();
    return RelativeTransform.GetWorldTransform(Parent->RelativeTransform).Translation;
}

const FQuat& UBone::GetWorldRotation() const
{
    if (!Parent)
        return GetRelativeRotation();
    return RelativeTransform.GetWorldTransform(Parent->RelativeTransform).Rotation;
}

const FVector& UBone::GetWorldScale() const
{
    if (!Parent)
        return GetRelativeScale();
    return RelativeTransform.GetWorldTransform(Parent->RelativeTransform).Scale3D;
}

const FTransform& UBone::GetWorldTransform() const
{
    if (!Parent)
        return RelativeTransform;
    return RelativeTransform.GetWorldTransform(Parent->RelativeTransform);
}

// Local BindPos Getter Setter
const FVector& UBone::GetRelativeBindPoseLocation() const
{
    return BindPose.Translation;
}

const FQuat& UBone::GetRelativeBindPoseRotation() const
{
    return BindPose.Rotation;
}

const FVector& UBone::GetRelativeBindPoseScale() const
{
    return BindPose.Scale3D;
}

const FTransform& UBone::GetRelativeBindPose() const
{
    return BindPose;
}

void UBone::SetRelativeBindPoseLocation(const FVector& InLocation)
{
    BindPose.Translation = InLocation;
}

void UBone::SetRelativeBindPoseRotation(const FQuat& InRotatation)
{
    BindPose.Rotation = InRotatation;
}

void UBone::SetRelativeBindPoseScale(const FVector& InScale)
{
    BindPose.Scale3D = InScale;
}

void UBone::SetRelativeBindPoseTransform(const FTransform& InBindPoseTransform)
{
    BindPose = InBindPoseTransform;
}

// World BindPos Getter
const FVector& UBone::GetWorldBindPoseLocation() const
{
    if (!Parent)
        return GetRelativeBindPoseLocation();
    return Parent->BindPose.GetWorldTransform(BindPose).Translation;
}

const FQuat& UBone::GetWorldBindPoseRotation() const
{
    if (!Parent)
        return GetRelativeBindPoseRotation();
    return Parent->BindPose.GetWorldTransform(BindPose).Rotation;
}

const FVector& UBone::GetWorldBindPoseScale() const
{
    if (!Parent)
        return GetRelativeBindPoseScale();
    return Parent->BindPose.GetWorldTransform(BindPose).Scale3D;
}

const FTransform& UBone::GetWorldBindPose() const
{
    if (!Parent)
        return GetRelativeBindPose();
    return Parent->BindPose.GetWorldTransform(BindPose);
}

// BindPos와 현 Transform의 차이를 반환
FTransform UBone::GetBoneOffset()
{
    // 올바른 Skinning Transform 계산:
    // SkinningMatrix = CurrentWorldMatrix * Inverse(BindPoseWorldMatrix)

    FTransform WorldTransform = GetWorldTransform();
    FTransform WorldBindPose = GetWorldBindPose();

    // FTransform의 Relative 변환 기능 사용
    // GetRelativeTransform(Parent)는 this = result * Parent 를 만족하는 result를 반환
    // 즉, this * Inverse(Parent) = result
    FTransform BoneOffset = WorldTransform.GetRelativeTransform(WorldBindPose);

    return BoneOffset;
}

FMatrix UBone::GetSkinningMatrix()
{
    return GetBoneOffset().GetModelingMatrix();
}

void UBone::SetParent(UBone* InParent)
{
    Parent = InParent;
}

void UBone::AddChild(UBone* InChild)
{
    if (!InChild)
        return;
    Children.push_back(InChild);
}

void UBone::RemoveChild(UBone* InChild)
{
    if (!InChild)
        return;
    bool WasValid = Children.Remove(InChild);

    if (!WasValid)
        UE_LOG(
            "[Warning UBone::RemoveChild] 존재하지 않는 Child %p를 지우려 시도했습니다.",
            InChild
        );
}

const TArray<UBone*>& UBone::GetChildren() const
{
    return Children;
}

void UBone::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    // Parent는 복제하지 않음 (무한 재귀 방지)
    // Skeleton이 Root부터 자식 방향으로 복제를 진행함

    for (int32 i = 0; i < Children.Num(); i++)
    {
        Children[i] = Children[i]->Duplicate();
    }
}

void UBone::PostDuplicate()
{
    Super::PostDuplicate();
}