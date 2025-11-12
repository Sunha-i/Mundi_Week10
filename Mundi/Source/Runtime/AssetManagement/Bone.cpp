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

FVector UBone::GetWorldLocation() const
{
    if (!Parent)
        return GetRelativeLocation();
    FTransform ParentWorldTransform = Parent->GetWorldTransform();
    return ParentWorldTransform.GetWorldTransform(RelativeTransform).Translation;
}

FQuat UBone::GetWorldRotation() const
{
    if (!Parent)
        return GetRelativeRotation();
    FTransform ParentWorldTransform = Parent->GetWorldTransform();
    return ParentWorldTransform.GetWorldTransform(RelativeTransform).Rotation;
}

FVector UBone::GetWorldScale() const
{
    if (!Parent)
        return GetRelativeScale();
    FTransform ParentWorldTransform = Parent->GetWorldTransform();
    return ParentWorldTransform.GetWorldTransform(RelativeTransform).Scale3D;
}

FTransform UBone::GetWorldTransform() const
{
    if (!Parent)
        return RelativeTransform;
    FTransform ParentWorldTransform = Parent->GetWorldTransform();
    return ParentWorldTransform.GetWorldTransform(RelativeTransform);
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

// World BindPos Getter (캐시된 값 반환)
FVector UBone::GetWorldBindPoseLocation() const
{
    return WorldBindPose.Translation;
}

FQuat UBone::GetWorldBindPoseRotation() const
{
    return WorldBindPose.Rotation;
}

FVector UBone::GetWorldBindPoseScale() const
{
    return WorldBindPose.Scale3D;
}

const FTransform& UBone::GetWorldBindPose() const
{
    return WorldBindPose;
}

// CPU Skinning 최적화: WorldBindPose와 InverseBindPoseMatrix 캐싱
const FMatrix& UBone::GetInverseBindPoseMatrix() const
{
    return InverseBindPoseMatrix;
}

void UBone::CacheWorldBindPose()
{
    // WorldBindPose 계산 (부모가 있으면 부모의 WorldBindPose와 합성)
    if (!Parent)
    {
        WorldBindPose = BindPose;
    }
    else
    {
        const FTransform& ParentWorldBindPose = Parent->GetWorldBindPose();
        WorldBindPose = ParentWorldBindPose.GetWorldTransform(BindPose);
    }

    // InverseBindPoseMatrix 계산
    InverseBindPoseMatrix = WorldBindPose.ToMatrix().InverseAffine();
}

// BindPos와 현 Transform의 차이를 반환
FTransform UBone::GetBoneOffset()
{
    // 올바른 Skinning Transform 계산:
    // SkinningMatrix = CurrentWorldMatrix * Inverse(BindPoseWorldMatrix)

    // FTransform에는 직접 곱셈이 없으므로 행렬로 계산
    FTransform WorldTransform = GetWorldTransform();
    FTransform WorldBindPose = GetWorldBindPose();

    return WorldBindPose.GetRelativeTransform(WorldTransform);
}

FMatrix UBone::GetSkinningMatrix()
{
    // Skinning Matrix = CurrentWorldMatrix * Inverse(BindPoseWorldMatrix)
    FTransform WorldTransform = GetWorldTransform();
    FTransform WorldBindPose = GetWorldBindPose();

    FMatrix CurrentWorldMatrix = WorldTransform.ToMatrix();
    FMatrix InverseBindPoseMatrix = WorldBindPose.ToMatrix().InverseAffine();

    return CurrentWorldMatrix * InverseBindPoseMatrix;
}

void UBone::SetParent(UBone* InParent)
{
    Parent = InParent;
}

UBone* UBone::GetParent() const
{
    return Parent;
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

    // 얕은 복사로 인해 Parent가 원본 Bone을 가리키므로 nullptr로 초기화
    // (Root가 아닌 Bone들은 부모에서 SetParent로 재설정될 것임)
    //Parent = nullptr;

    // Children 복제 및 Parent 재설정
    for (int32 i = 0; i < Children.Num(); i++)
    {
        UBone* OriginalChild = Children[i];
        UBone* DuplicatedChild = OriginalChild->Duplicate();
        Children[i] = DuplicatedChild;

        // 복제된 자식의 Parent를 현재 복제된 Bone으로 재설정
        DuplicatedChild->SetParent(this);
    }
}

void UBone::PostDuplicate()
{
    Super::PostDuplicate();

    // 복제된 Children의 Parent 포인터를 재설정
    for (UBone* Child : Children)
    {
        if (Child)
        {
            Child->SetParent(this);
        }
    }
}