#include "pch.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"
#include "ObjectFactory.h"
#include "BillboardComponent.h"
#include "ShapeComponent.h"
#include "LineComponent.h"
#include "World.h"
#include "Skeleton.h"
#include "Bone.h"
#include "SkeletalMesh.h"

IMPLEMENT_CLASS(ASkeletalMeshActor)
BEGIN_PROPERTIES(ASkeletalMeshActor)
    MARK_AS_SPAWNABLE("스켈레탈 메시", "스켈레탈 메시를 렌더링하는 액터입니다.")
END_PROPERTIES()

ASkeletalMeshActor::ASkeletalMeshActor()
{
    ObjectName = "Skeletal Mesh Actor";
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
    
    // 루트 교체
    RootComponent = SkeletalMeshComponent;
}
 
void ASkeletalMeshActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

ASkeletalMeshActor::~ASkeletalMeshActor()
{
    if (SkeletonOverlay)
    {
        SkeletonOverlay->DestroyComponent();
        SkeletonOverlay = nullptr;
    }
}

FAABB ASkeletalMeshActor::GetBounds() const
{
    // 멤버 변수를 직접 사용하지 않고, 현재의 RootComponent를 확인하도록 수정
    // 기본 컴포넌트가 제거되는 도중에 어떤 로직에 의해 GetBounds() 함수가 호출
    // 이 시점에 ASkeletalMeshActor의 멤버변수인 USkeletalMeshComponent는 아직 새로운 컴포넌트
    // ID 751로 업데이트되기 전. 제거된 기본 컴포넌트를 여전히 가리키고 있음. 
    // 유효하지 않은 SkeletalMeshcomponent 포인터의 getworldaabb 함수를 호출 시도.

    USkeletalMeshComponent* CurrentSMC = Cast<USkeletalMeshComponent>(RootComponent);
    if (CurrentSMC)
    {
        return CurrentSMC->GetWorldAABB();
    }

    return FAABB();
}

void ASkeletalMeshActor::SetSkeletalMeshComponent(USkeletalMeshComponent* InSkeletalMeshComponent)
{
    SkeletalMeshComponent = InSkeletalMeshComponent;
}

void ASkeletalMeshActor::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
    for (UActorComponent* Component : OwnedComponents)
    {
        if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component))
        {
            SkeletalMeshComponent = SkeletalMeshComp;
            break;
        }
    }
}

void ASkeletalMeshActor::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RootComponent);
    }
}

ULineComponent* ASkeletalMeshActor::EnsureSkeletonOverlay(UWorld* World)
{
    if (SkeletonOverlay)
        return SkeletonOverlay;

    if (!SkeletalMeshComponent)
        return nullptr;

    ULineComponent* NewLine = NewObject<ULineComponent>();
    if (!NewLine)
        return nullptr;

    NewLine->SetupAttachment(SkeletalMeshComponent);
    NewLine->SetRequiresGridShowFlag(false);
    NewLine->SetAlwaysOnTop(true);

    AddOwnedComponent(NewLine);
    if (World)
    {
        NewLine->RegisterComponent(World);
    }

    SkeletonOverlay = NewLine;
    return SkeletonOverlay;
}

void ASkeletalMeshActor::ClearSkeletonOverlay(bool bDestroyComponent)
{
    if (!SkeletonOverlay)
        return;

    SkeletonOverlay->ClearLines();
    SkeletonOverlay->SetLineVisible(false);

    if (bDestroyComponent)
    {
        SkeletonOverlay->DestroyComponent();
        SkeletonOverlay = nullptr;
    }
}

void ASkeletalMeshActor::BuildSkeletonOverlay(UBone* SelectedBone)
{
    if (!SkeletalMeshComponent)
        return;

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    USkeleton* Skeleton = MeshAsset ? MeshAsset->Skeleton : nullptr;
    if (!Skeleton || !Skeleton->GetRoot())
        return;

    if (!SkeletonOverlay)
        return;

    // OPTIMIZATION: Reuse existing lines instead of clearing and recreating
    // Only clear if this is the first time or structure changed
    const TArray<ULine*>& ExistingLines = SkeletonOverlay->GetLines();

    // Build into temporary array first to check if we need to recreate
    static TArray<FLineData> TempLineData;
    TempLineData.clear();

    const FTransform ComponentInverse = SkeletalMeshComponent->GetWorldTransform().Inverse();
    BuildSkeletonLinesRecursive_Temp(Skeleton->GetRoot(), ComponentInverse, TempLineData, SelectedBone);

    // If line count matches, update existing lines (FAST PATH)
    // 개수 체크 
    if (ExistingLines.size() == TempLineData.size())
    {
        for (size_t i = 0; i < ExistingLines.size(); ++i)
        {
            if (ExistingLines[i])
            {
                ExistingLines[i]->SetLine(TempLineData[i].Start, TempLineData[i].End);
                ExistingLines[i]->SetColor(TempLineData[i].Color);
            }
        }
        SkeletonOverlay->MarkWorldDataDirty();  // Mark cache dirty to recalculate world coords
    }
    else
    {
        // Structure changed, need to recreate (SLOW PATH)
        SkeletonOverlay->ClearLines();
        for (const FLineData& LineData : TempLineData)
        {
            SkeletonOverlay->AddLine(LineData.Start, LineData.End, LineData.Color);
        }
    }

    SkeletonOverlay->SetLineVisible(true);
}

void ASkeletalMeshActor::BuildSkeletonLinesRecursive(UBone* Bone, const FTransform& ComponentWorldInverse, ULineComponent* SkeletonLineComponent, UBone* SelectedBone)
{
    if (!Bone || !SkeletonLineComponent)
        return;

    // 본의 월드 트랜스폼 (회전 포함)
    const FTransform BoneWorld = Bone->GetWorldTransform();

    // 로컬 공간으로 변환 (직접 멤버 접근)
    const FVector ParentLocal = ComponentWorldInverse.TransformPosition(BoneWorld.Translation);
    const FQuat RotationLocal = ComponentWorldInverse.Rotation.Inverse() * BoneWorld.Rotation;

    // 색상 결정: 선택된 본이면 초록색, 아니면 흰색
    const bool bIsSelected = (Bone == SelectedBone);
    const FVector4 JointColor = bIsSelected ? FVector4(0.2f, 1.0f, 0.2f, 1.0f) : FVector4(1.f, 1.f, 1.f, 1.f);

    // 관절 구체 추가
    AddJointSphereOriented(ParentLocal, RotationLocal, SkeletonLineComponent, JointColor);

    // 자식 본 처리
    for (UBone* Child : Bone->GetChildren())
    {
        if (!Child)
            continue;

        // 자식 본의 로컬 위치 계산
        const FVector ChildLocal = ComponentWorldInverse.TransformPosition(Child->GetWorldLocation());

        // 피라미드 색상 결정: 선택된 본에서 자식으로 가는 라인이면 초록색, 부모에서 선택된 본으로 들어오면 주황색
        FVector4 PyramidColor = FVector4(1.f, 1.f, 1.f, 1.f); // 기본 흰색
        if (bIsSelected)
        {
            // 선택된 본에서 자식으로 가는 피라미드는 초록색
            PyramidColor = FVector4(0.2f, 1.0f, 0.2f, 1.0f);
        }
        else if (Child == SelectedBone)
        {
            // 부모 본에서 선택된 본으로 들어오는 피라미드는 주황색
            PyramidColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
        }

        // 부모→자식 방향으로 본 피라미드 생성
        AddBonePyramid(ParentLocal, ChildLocal, SkeletonLineComponent, PyramidColor);

        // 재귀 호출 (자식 뼈대도 처리)
        BuildSkeletonLinesRecursive(Child, ComponentWorldInverse, SkeletonLineComponent, SelectedBone);
    }
}

void ASkeletalMeshActor::AddJointSphereOriented(const FVector& CenterLocal, const FQuat& RotationLocal, ULineComponent* SkeletonLineComponent, const FVector4& Color)
{
    if (!SkeletonLineComponent)
        return;

    const float Radius = 0.015f;
    const int32 Segments = 8;
    const float DeltaAngle = 2.0f * 3.1415926535f / Segments;

    // 기본 축 3개 (회전 적용 전)
    const FVector Axes[3] = {
        FVector(1.f, 0.f, 0.f),
        FVector(0.f, 1.f, 0.f),
        FVector(0.f, 0.f, 1.f)
    };

    for (int AxisIdx = 0; AxisIdx < 3; ++AxisIdx)
    {
        FVector Axis1 = RotationLocal.RotateVector(Axes[AxisIdx]);
        FVector Axis2 = RotationLocal.RotateVector(Axes[(AxisIdx + 1) % 3]);

        FVector PrevPoint = CenterLocal + Axis1 * Radius;

        for (int i = 1; i <= Segments; ++i)
        {
            const float Angle = i * DeltaAngle;
            const float CosA = cosf(Angle);
            const float SinA = sinf(Angle);

            FVector CurrPoint = CenterLocal + (Axis1 * CosA + Axis2 * SinA) * Radius;
            SkeletonLineComponent->AddLine(PrevPoint, CurrPoint, Color);
            PrevPoint = CurrPoint;
        }
    }
}

void ASkeletalMeshActor::BuildSkeletonLinesRecursive_Temp(UBone* Bone, const FTransform& ComponentWorldInverse, TArray<FLineData>& OutLines, UBone* SelectedBone)
{
    if (!Bone)
        return;

    const FTransform BoneWorld = Bone->GetWorldTransform();
    const FVector ParentLocal = ComponentWorldInverse.TransformPosition(BoneWorld.Translation);
    const FQuat RotationLocal = ComponentWorldInverse.Rotation.Inverse() * BoneWorld.Rotation;

    const bool bIsSelected = (Bone == SelectedBone);
    const FVector4 JointColor = bIsSelected ? FVector4(0.2f, 1.0f, 0.2f, 1.0f) : FVector4(1.f, 1.f, 1.f, 1.f);

    AddJointSphereOriented_Temp(ParentLocal, RotationLocal, OutLines, JointColor);

    for (UBone* Child : Bone->GetChildren())
    {
        if (!Child)
            continue;

        const FVector ChildLocal = ComponentWorldInverse.TransformPosition(Child->GetWorldLocation());

        FVector4 PyramidColor = FVector4(1.f, 1.f, 1.f, 1.f);
        if (bIsSelected)
        {
            PyramidColor = FVector4(0.2f, 1.0f, 0.2f, 1.0f);
        }
        else if (Child == SelectedBone)
        {
            PyramidColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
        }

        AddBonePyramid_Temp(ParentLocal, ChildLocal, OutLines, PyramidColor);
        BuildSkeletonLinesRecursive_Temp(Child, ComponentWorldInverse, OutLines, SelectedBone);
    }
}

void ASkeletalMeshActor::AddJointSphereOriented_Temp(const FVector& CenterLocal, const FQuat& RotationLocal, TArray<FLineData>& OutLines, const FVector4& Color)
{
    const float Radius = 0.015f;
    const int32 Segments = 8;
    const float DeltaAngle = 2.0f * 3.1415926535f / Segments;

    const FVector Axes[3] = {
        FVector(1.f, 0.f, 0.f),
        FVector(0.f, 1.f, 0.f),
        FVector(0.f, 0.f, 1.f)
    };

    for (int AxisIdx = 0; AxisIdx < 3; ++AxisIdx)
    {
        FVector Axis1 = RotationLocal.RotateVector(Axes[AxisIdx]);
        FVector Axis2 = RotationLocal.RotateVector(Axes[(AxisIdx + 1) % 3]);

        FVector PrevPoint = CenterLocal + Axis1 * Radius;

        for (int i = 1; i <= Segments; ++i)
        {
            const float Angle = i * DeltaAngle;
            const float CosA = cosf(Angle);
            const float SinA = sinf(Angle);

            FVector CurrPoint = CenterLocal + (Axis1 * CosA + Axis2 * SinA) * Radius;
            OutLines.push_back(FLineData(PrevPoint, CurrPoint, Color));
            PrevPoint = CurrPoint;
        }
    }
}

void ASkeletalMeshActor::AddBonePyramid_Temp(const FVector& ParentLocal, const FVector& ChildLocal, TArray<FLineData>& OutLines, const FVector4& Color)
{
    FVector Dir = ChildLocal - ParentLocal;
    float Length = Dir.Size();
    if (Length < KINDA_SMALL_NUMBER)
        return;

    FVector Forward = Dir / Length;

    FVector Up(0.f, 0.f, 1.f);
    if (fabsf(FVector::Dot(Up, Forward)) > 0.99f)
    {
        Up = FVector(0.f, 1.f, 0.f);
    }

    FVector Right = FVector::Cross(Up, Forward).GetSafeNormal();
    FVector TrueUp = FVector::Cross(Forward, Right).GetSafeNormal();

    float BaseRadius = std::min(Length * 0.05f, 0.01f);

    FVector BaseA = ParentLocal + (Right * BaseRadius) + (TrueUp * BaseRadius);
    FVector BaseB = ParentLocal - (Right * BaseRadius) + (TrueUp * BaseRadius);
    FVector BaseC = ParentLocal - (TrueUp * BaseRadius * 1.5f);

    FVector Apex = ChildLocal;

    // Base triangle
    OutLines.push_back(FLineData(BaseA, BaseB, Color));
    OutLines.push_back(FLineData(BaseB, BaseC, Color));
    OutLines.push_back(FLineData(BaseC, BaseA, Color));

    // Apex connections
    OutLines.push_back(FLineData(BaseA, Apex, Color));
    OutLines.push_back(FLineData(BaseB, Apex, Color));
    OutLines.push_back(FLineData(BaseC, Apex, Color));
}

void ASkeletalMeshActor::AddBonePyramid(const FVector& ParentLocal, const FVector& ChildLocal, ULineComponent* SkeletonLineComponent, const FVector4& Color)
{
    if (!SkeletonLineComponent)
        return;

    FVector Dir = ChildLocal - ParentLocal;
    float Length = Dir.Size();
    if (Length < KINDA_SMALL_NUMBER)
        return;

    FVector Forward = Dir / Length;

    // 보조축 계산 (Forward 방향과 거의 평행하지 않게)
    FVector Up(0.f, 0.f, 1.f);
    if (fabsf(FVector::Dot(Up, Forward)) > 0.99f)
    {
        Up = FVector(0.f, 1.f, 0.f); // 축이 겹칠 경우 다른 Up 사용
    }

    FVector Right = FVector::Cross(Up, Forward).GetSafeNormal();
    FVector TrueUp = FVector::Cross(Forward, Right).GetSafeNormal();

    // 피라미드 밑면 크기 (비례하되 최대값 제한)
    float BaseRadius = std::min(Length * 0.05f, 0.01f);

    // 부모 관절 기준 밑면 세 점 (삼각형)
    FVector BaseA = ParentLocal + (Right * BaseRadius) + (TrueUp * BaseRadius);
    FVector BaseB = ParentLocal - (Right * BaseRadius) + (TrueUp * BaseRadius);
    FVector BaseC = ParentLocal - (TrueUp * BaseRadius * 1.5f);

    // 자식 관절이 피라미드의 Apex
    FVector Apex = ChildLocal;

    // 밑면 삼각형 윤곽선
    SkeletonLineComponent->AddLine(BaseA, BaseB, Color);
    SkeletonLineComponent->AddLine(BaseB, BaseC, Color);
    SkeletonLineComponent->AddLine(BaseC, BaseA, Color);

    // 각 밑면 점과 꼭짓점 연결
    SkeletonLineComponent->AddLine(BaseA, Apex, Color);
    SkeletonLineComponent->AddLine(BaseB, Apex, Color);
    SkeletonLineComponent->AddLine(BaseC, Apex, Color);
}
