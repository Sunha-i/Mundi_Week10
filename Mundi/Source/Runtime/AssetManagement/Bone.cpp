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

// World BindPos Getter
FVector UBone::GetWorldBindPoseLocation() const
{
    if (!Parent)
        return GetRelativeBindPoseLocation();
    FTransform ParentWorldBindPose = Parent->GetWorldBindPose();
    return ParentWorldBindPose.GetWorldTransform(BindPose).Translation;
}

FQuat UBone::GetWorldBindPoseRotation() const
{
    if (!Parent)
        return GetRelativeBindPoseRotation();
    FTransform ParentWorldBindPose = Parent->GetWorldBindPose();
    return ParentWorldBindPose.GetWorldTransform(BindPose).Rotation;
}

FVector UBone::GetWorldBindPoseScale() const
{
    if (!Parent)
        return GetRelativeBindPoseScale();
    FTransform ParentWorldBindPose = Parent->GetWorldBindPose();
    return ParentWorldBindPose.GetWorldTransform(BindPose).Scale3D;
}

FTransform UBone::GetWorldBindPose() const
{
    if (!Parent)
        return GetRelativeBindPose();
    FTransform ParentWorldBindPose = Parent->GetWorldBindPose();
    return ParentWorldBindPose.GetWorldTransform(BindPose);
}

// BindPos와 현 Transform의 차이를 반환
FTransform UBone::GetBoneOffset()
{
    // 올바른 Skinning Transform 계산:
    // SkinningMatrix = CurrentWorldMatrix * Inverse(BindPoseWorldMatrix)

    // FTransform에는 직접 곱셈이 없으므로 행렬로 계산
    FTransform WorldTransform = GetWorldTransform();
    FTransform WorldBindPose = GetWorldBindPose();

    FMatrix CurrentWorldMatrix = WorldTransform.ToMatrix();
    FMatrix InverseBindPoseMatrix = WorldBindPose.ToMatrix().InverseAffine();

    FMatrix BoneOffsetMatrix = CurrentWorldMatrix * InverseBindPoseMatrix;

    // 다시 FTransform으로 변환할 필요 없이 행렬 자체를 반환하면 좋겠지만
    // 반환 타입이 FTransform이므로 임시로 identity 반환
    // (실제로는 GetSkinningMatrix를 직접 사용하는 것이 더 효율적)
    return FTransform();
}

FMatrix UBone::GetSkinningMatrix()
{
    // Skinning Matrix = CurrentWorldMatrix * Inverse(BindPoseWorldMatrix)
    FTransform WorldTransform = GetWorldTransform();
    FTransform WorldBindPose = GetWorldBindPose();

    FMatrix CurrentWorldMatrix = WorldTransform.ToMatrix();
    FMatrix InverseBindPoseMatrix = WorldBindPose.ToMatrix().InverseAffine();

    #ifdef _DEBUG
    // 초기 상태 검증: Relative == BindPose면 Skinning Matrix는 Identity여야 함
    static bool bFirstCheck = true;
    if (bFirstCheck && !Parent)  // Root Bone만 체크
    {
        bFirstCheck = false;
        FTransform Diff = RelativeTransform.GetRelativeTransform(BindPose);
        float LocDist = Diff.Translation.Size();
        if (LocDist < 0.01f)
        {
            // Bind Pose와 같으면 Identity 확인
            FMatrix Result = CurrentWorldMatrix * InverseBindPoseMatrix;
            FMatrix Identity = FMatrix::Identity();
            bool bIsIdentity =
                FMath::Abs(Result.M[0][0] - 1.0f) < 0.01f &&
                FMath::Abs(Result.M[1][1] - 1.0f) < 0.01f &&
                FMath::Abs(Result.M[2][2] - 1.0f) < 0.01f &&
                FMath::Abs(Result.M[3][3] - 1.0f) < 0.01f;
            if (!bIsIdentity)
            {
                OutputDebugStringA("WARNING: Skinning Matrix is NOT Identity at Bind Pose!\n");
            }
        }
    }
    #endif

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

    // Parent는 복제하지 않음 (무한 재귀 방지)
    // Skeleton이 Root부터 자식 방향으로 복제를 진행함

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
}