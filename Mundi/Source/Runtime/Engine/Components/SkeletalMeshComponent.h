#pragma once

#include "SkinnedMeshComponent.h"
#include "Material.h"
#include "Color.h"
#include "Enums.h"
struct ID3D11Buffer;
struct FSkeletalMesh;

struct FMeshBatchElement;
class FSceneView;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
    GENERATED_REFLECTION_BODY()
    USkeletalMeshComponent();
    ~USkeletalMeshComponent();

public:
    void TickComponent(float DeltaTime) override;
    void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

    // Material interface parity with StaticMeshComponent
    UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
    void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial) override;

    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
    void DuplicateSubObjects() override;

private:
    void ClearDynamicMaterials();
    TArray<UMaterialInterface*> MaterialSlots;
    TArray<UMaterialInstanceDynamic*> DynamicMaterialInstances;
    // CPU skinning dynamic vertex buffer (component-owned)
    ID3D11Buffer* SkinnedVertexBuffer = nullptr;
    uint32 SkinnedVertexCount = 0;
    void UpdateCpuSkinnedVertexBuffer();

public:
    // Parity functions for Property UI editing
    UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex);
    void SetMaterialTextureByUser(const uint32 InMaterialSlotIndex, EMaterialTextureSlot Slot, UTexture* Texture);
    void SetMaterialColorByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, const FLinearColor& Value);
    void SetMaterialScalarByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, float Value);

    // Bone pose editing hooks (local/component space updates before CPU skinning)
    void SetBoneLocalTransform(int32 BoneIndex, const Matrix4x4& InLocalTransform);
    void ResetBoneLocalTransforms();

private:
    void EnsureBonePoseCache(FSkeletalMesh* Asset);
    void InitializeBonePoseCache(FSkeletalMesh* Asset);
    void RebuildBonePose(FSkeletalMesh* Asset);
    Matrix4x4 BuildComponentSpaceBone(FSkeletalMesh* Asset, int32 BoneIndex);
    const Matrix4x4& GetSkinningMatrixForIndex(FSkeletalMesh* Asset, int32 BoneIndex) const;

private:
    FSkeletalMesh* CachedPoseAsset = nullptr;
    bool bBonePoseDirty = true;
    TArray<Matrix4x4> ReferenceLocalPose;
    TArray<Matrix4x4> BoneLocalPose;
    TArray<Matrix4x4> BoneComponentPose;
    TArray<Matrix4x4> BoneSkinningPose;
    mutable TArray<uint8> BoneEvaluationState;
};
