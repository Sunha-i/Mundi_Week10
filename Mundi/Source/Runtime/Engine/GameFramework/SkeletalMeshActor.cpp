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

void ASkeletalMeshActor::BuildSkeletonOverlay()
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

    SkeletonOverlay->ClearLines();

    const FTransform ComponentInverse = SkeletalMeshComponent->GetWorldTransform().Inverse();
    BuildSkeletonLinesRecursive(Skeleton->GetRoot(), ComponentInverse, SkeletonOverlay);
    SkeletonOverlay->SetLineVisible(true);
}

void ASkeletalMeshActor::BuildSkeletonLinesRecursive(UBone* Bone, const FTransform& ComponentWorldInverse, ULineComponent* SkeletonLineComponent)
{
    if (!Bone || !SkeletonLineComponent)
        return;

    // 본의 월드 트랜스폼 (회전 포함)
    const FTransform BoneWorld = Bone->GetWorldTransform();

    // 로컬 공간으로 변환 (직접 멤버 접근)
    const FVector ParentLocal = ComponentWorldInverse.TransformPosition(BoneWorld.Translation);
    const FQuat RotationLocal = ComponentWorldInverse.Rotation.Inverse() * BoneWorld.Rotation;

    // 관절 구체 추가
    AddJointSphereOriented(ParentLocal, RotationLocal, SkeletonLineComponent);

    // 자식 본 처리
    for (UBone* Child : Bone->GetChildren())
    {
        if (!Child)
            continue;

        // 자식 본의 로컬 위치 계산
        const FVector ChildLocal = ComponentWorldInverse.TransformPosition(Child->GetWorldLocation());

        // 부모→자식 방향으로 본 피라미드 생성
        AddBonePyramid(ParentLocal, ChildLocal, SkeletonLineComponent);

        // 재귀 호출 (자식 뼈대도 처리)
        BuildSkeletonLinesRecursive(Child, ComponentWorldInverse, SkeletonLineComponent);
    }
}

void ASkeletalMeshActor::AddJointSphereOriented(const FVector& CenterLocal, const FQuat& RotationLocal, ULineComponent* SkeletonLineComponent)
{
    if (!SkeletonLineComponent)
        return;

    const float Radius = 0.015f;
    const int32 Segments = 16;
    const float DeltaAngle = 2.0f * 3.1415926535f / Segments;

    // 기본 축 3개 (회전 적용 전)
    const FVector Axes[3] = {
        FVector(1.f, 0.f, 0.f),
        FVector(0.f, 1.f, 0.f),
        FVector(0.f, 0.f, 1.f)
    };

    const FVector4 Color = FVector4(1.f, 1.f, 1.f, 1.f);

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

void ASkeletalMeshActor::AddBonePyramid(const FVector& ParentLocal, const FVector& ChildLocal, ULineComponent* SkeletonLineComponent)
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

    // 피라미드 밑면 크기
    float BaseRadius = Length * 0.05f;

    // 부모 관절 기준 밑면 세 점 (삼각형)
    FVector BaseA = ParentLocal + (Right * BaseRadius) + (TrueUp * BaseRadius);
    FVector BaseB = ParentLocal - (Right * BaseRadius) + (TrueUp * BaseRadius);
    FVector BaseC = ParentLocal - (TrueUp * BaseRadius * 1.5f);

    // 자식 관절이 피라미드의 Apex
    FVector Apex = ChildLocal;

    const FVector4 BoneColor = FVector4(1.f, 1.f, 1.f, 1.f);

    // 밑면 삼각형 윤곽선
    SkeletonLineComponent->AddLine(BaseA, BaseB, BoneColor);
    SkeletonLineComponent->AddLine(BaseB, BaseC, BoneColor);
    SkeletonLineComponent->AddLine(BaseC, BaseA, BoneColor);

    // 각 밑면 점과 꼭짓점 연결
    SkeletonLineComponent->AddLine(BaseA, Apex, BoneColor);
    SkeletonLineComponent->AddLine(BaseB, Apex, BoneColor);
    SkeletonLineComponent->AddLine(BaseC, Apex, BoneColor);
}
