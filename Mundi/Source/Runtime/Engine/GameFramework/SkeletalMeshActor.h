#pragma once
#include "Actor.h"

class USkeletalMeshComponent;
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

    // ───── 복사 관련 ────────────────────────────
    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(ASkeletalMeshActor)

    // Serialize
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

protected:
    USkeletalMeshComponent* SkeletalMeshComponent;
};

