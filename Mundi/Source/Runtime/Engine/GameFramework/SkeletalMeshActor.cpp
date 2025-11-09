#include "pch.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"

IMPLEMENT_CLASS(ASkeletalMeshActor)
BEGIN_PROPERTIES(ASkeletalMeshActor)
    MARK_AS_SPAWNABLE("스켈레탈 메시", "스켈레탈 메시를 렌더링하는 액터입니다.")
END_PROPERTIES()

ASkeletalMeshActor::ASkeletalMeshActor()
{
    ObjectName = "Skeletal Mesh Actor";
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
    RootComponent = SkeletalMeshComponent;
}

ASkeletalMeshActor::~ASkeletalMeshActor()
{
}

void ASkeletalMeshActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

FAABB ASkeletalMeshActor::GetBounds() const
{
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