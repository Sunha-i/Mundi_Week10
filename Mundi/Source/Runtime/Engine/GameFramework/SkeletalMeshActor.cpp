#include "pch.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"
#include "ObjectFactory.h"
#include "BillboardComponent.h"
#include "ShapeComponent.h"

IMPLEMENT_CLASS(ASkeletalMeshActor)

BEGIN_PROPERTIES(ASkeletalMeshActor)
    MARK_AS_SPAWNABLE("스켈레탈 메시", "스켈레탈 메시를 렌더링하는 액터입니다.")
END_PROPERTIES()

ASkeletalMeshActor::ASkeletalMeshActor()
{
    ObjectName = "Static Mesh Actor";
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
