#include "pch.h"
#include "SkeletalMeshComponent.h"

#include "SkeletalMesh.h"
#include "ResourceManager.h"
#include "Material.h"
#include "Texture.h"
#include "Shader.h"
#include "SceneView.h"
#include "MeshBatchElement.h"
#include "VertexData.h"
#include "D3D11RHI.h"
#include <d3d11.h>
#include <cmath>
#include <algorithm>
//#include "../AssetManagement/ResourceManager.h"
#include "JsonSerializer.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
    MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링합니다.")
    ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

namespace
{
    Matrix4x4 MakeIdentityMatrix4x4()
    {
        Matrix4x4 M{};
        for (int32 i = 0; i < 4; ++i)
        {
            for (int32 j = 0; j < 4; ++j)
            {
                M.m[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
        return M;
    }

    const Matrix4x4& IdentityMatrix4x4()
    {
        static const Matrix4x4 Identity = MakeIdentityMatrix4x4();
        return Identity;
    }

    Matrix4x4 MatrixMultiply4x4(const Matrix4x4& A, const Matrix4x4& B)
    {
        Matrix4x4 Out{};
        for (int32 r = 0; r < 4; ++r)
        {
            for (int32 c = 0; c < 4; ++c)
            {
                float Value = 0.0f;
                for (int32 k = 0; k < 4; ++k)
                {
                    Value += A.m[r][k] * B.m[k][c];
                }
                Out.m[r][c] = Value;
            }
        }
        return Out;
    }

    Matrix4x4 MatrixInverse4x4(const Matrix4x4& In)
    {
        Matrix4x4 Temp = In;
        Matrix4x4 Inv = IdentityMatrix4x4();

        for (int32 Pivot = 0; Pivot < 4; ++Pivot)
        {
            int32 PivotRow = Pivot;
            float MaxAbs = std::fabs(Temp.m[Pivot][Pivot]);
            for (int32 Row = Pivot + 1; Row < 4; ++Row)
            {
                float Val = std::fabs(Temp.m[Row][Pivot]);
                if (Val > MaxAbs)
                {
                    MaxAbs = Val;
                    PivotRow = Row;
                }
            }

            if (MaxAbs < 1e-8f)
            {
                return IdentityMatrix4x4();
            }

            if (PivotRow != Pivot)
            {
                for (int32 Col = 0; Col < 4; ++Col)
                {
                    std::swap(Temp.m[Pivot][Col], Temp.m[PivotRow][Col]);
                    std::swap(Inv.m[Pivot][Col], Inv.m[PivotRow][Col]);
                }
            }

            const float Diag = Temp.m[Pivot][Pivot];
            const float InvDiag = 1.0f / Diag;
            for (int32 Col = 0; Col < 4; ++Col)
            {
                Temp.m[Pivot][Col] *= InvDiag;
                Inv.m[Pivot][Col] *= InvDiag;
            }

            for (int32 Row = 0; Row < 4; ++Row)
            {
                if (Row == Pivot)
                    continue;
                const float Factor = Temp.m[Row][Pivot];
                if (std::fabs(Factor) < 1e-8f)
                    continue;
                for (int32 Col = 0; Col < 4; ++Col)
                {
                    Temp.m[Row][Col] -= Factor * Temp.m[Pivot][Col];
                    Inv.m[Row][Col] -= Factor * Inv.m[Pivot][Col];
                }
            }
        }

        return Inv;
    }
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
    bCanEverTick = true;
    // Set default skeletal mesh (T-pose) using relative data dir
    extern const FString GDataDir;
    SetSkeletalMesh(GDataDir + "/Model/Ch46_nonPBR.fbx");
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    ClearDynamicMaterials();
    if (SkinnedVertexBuffer) { SkinnedVertexBuffer->Release(); SkinnedVertexBuffer = nullptr; }
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!IsActive() || !IsTickEnabled())
    {
        return;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
        return;

    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset)
        return;

    EnsureBonePoseCache(Asset);
    if (bBonePoseDirty)
    {
        RebuildBonePose(Asset);
    }
}

void USkeletalMeshComponent::ClearDynamicMaterials()
{
    for (UMaterialInstanceDynamic* MID : DynamicMaterialInstances)
    {
        delete MID;
    }
    DynamicMaterialInstances.Empty();
    MaterialSlots.Empty();
}

void USkeletalMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh || !Mesh->GetSkeletalMeshAsset())
        return;

    const FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();

    const bool bHasSections = !Asset->Sections.IsEmpty();
    const uint32 NumSections = bHasSections ? (uint32)Asset->Sections.size() : 1;

    // Ensure material slots size matches sections
    if (bHasSections && MaterialSlots.Num() != NumSections)
    {
        MaterialSlots.resize(NumSections);
        for (uint32 i = 0; i < NumSections; ++i)
        {
            if (!MaterialSlots[i])
                MaterialSlots[i] = nullptr; 
        }
    }
    auto DetermineMaterialAndShader = [&](uint32 SectionIndex) -> TPair<UMaterialInterface*, UShader*>
    {
        UMaterialInterface* Material = GetMaterial(SectionIndex);
        UShader* Shader = nullptr;

        if (Material && Material->GetShader())
        {
            Shader = Material->GetShader();
        }
        else
        {
            Material = UResourceManager::GetInstance().GetDefaultMaterial();
            if (Material) Shader = Material->GetShader();
            if (!Material || !Shader)
                return { nullptr, nullptr };
        }
        return { Material, Shader };
    };

    for (uint32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
    {
        uint32 IndexCount = 0;
        uint32 StartIndex = 0;

        if (bHasSections)
        {
            const FGroupInfo& Sec = Asset->Sections[SectionIndex];
            IndexCount = Sec.IndexCount;
            StartIndex = Sec.StartIndex;
        }
        else
        {
            IndexCount = Mesh->GetIndexCount();
            StartIndex = 0;
        }

        if (IndexCount == 0)
            continue;

        auto [MaterialToUse, ShaderToUse] = DetermineMaterialAndShader(SectionIndex);
        if (!MaterialToUse || !ShaderToUse)
            continue;

        TArray<FShaderMacro> ShaderMacros = View->ViewShaderMacros;
        if (0 < MaterialToUse->GetShaderMacros().Num())
            ShaderMacros.Append(MaterialToUse->GetShaderMacros());

        FShaderVariant* Variant = ShaderToUse->GetOrCompileShaderVariant(ShaderMacros);

        FMeshBatchElement Batch{};
        if (Variant)
        {
            Batch.VertexShader = Variant->VertexShader;
            Batch.PixelShader = Variant->PixelShader;
            Batch.InputLayout = Variant->InputLayout;
        }

        Batch.Material = MaterialToUse;
        // Ensure CPU-skinned dynamic VB is updated and use it for rendering
        UpdateCpuSkinnedVertexBuffer();
        Batch.VertexBuffer = SkinnedVertexBuffer ? SkinnedVertexBuffer : Mesh->GetVertexBuffer();
        Batch.IndexBuffer = Mesh->GetIndexBuffer();
        Batch.VertexStride = sizeof(FVertexDynamic);
        Batch.IndexCount = IndexCount;
        Batch.StartIndex = StartIndex;
        Batch.BaseVertexIndex = 0;
        Batch.WorldMatrix = GetWorldMatrix();
        Batch.ObjectID = InternalIndex;
        Batch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        OutMeshBatchElements.Add(Batch);
    }
}

UMaterialInterface* USkeletalMeshComponent::GetMaterial(uint32 InSectionIndex) const
{
    if ((int32)InSectionIndex < 0 || InSectionIndex >= (uint32)MaterialSlots.Num())
        return nullptr;
    return MaterialSlots[(int32)InSectionIndex];
}

void USkeletalMeshComponent::SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
{
    if ((int32)InElementIndex < 0)
        return;
    if (InElementIndex >= (uint32)MaterialSlots.Num())
    {
        if (InElementIndex < 64)
        {
            MaterialSlots.resize((size_t)InElementIndex + 1);
        }
        else
        {
            return;
        }
    }

    // If replacing an existing MID we own, delete it
    if (UMaterialInterface* OldMaterial = MaterialSlots[(int32)InElementIndex])
    {
        if (UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(OldMaterial))
        {
            int32 Removed = DynamicMaterialInstances.Remove(OldMID);
            if (Removed > 0)
                delete OldMID;
        }
    }

    MaterialSlots[(int32)InElementIndex] = InNewMaterial;
}

UMaterialInstanceDynamic* USkeletalMeshComponent::CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(ElementIndex);
    if (!CurrentMaterial)
        return nullptr;

    if (UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(CurrentMaterial))
        return ExistingMID;

    UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(CurrentMaterial);
    if (NewMID)
    {
        DynamicMaterialInstances.Add(NewMID);
        SetMaterial(ElementIndex, NewMID);
        NewMID->SetFilePath("(Instance) " + CurrentMaterial->GetFilePath());
        return NewMID;
    }
    return nullptr;
}

void USkeletalMeshComponent::SetMaterialTextureByUser(const uint32 InMaterialSlotIndex, EMaterialTextureSlot Slot, UTexture* Texture)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
    if (MID == nullptr)
    {
        MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
    }
    if (MID)
        MID->SetTextureParameterValue(Slot, Texture);
}

void USkeletalMeshComponent::SetMaterialColorByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, const FLinearColor& Value)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
    if (MID == nullptr)
    {
        MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
    }
    if (MID)
        MID->SetColorParameterValue(ParameterName, Value);
}

void USkeletalMeshComponent::SetMaterialScalarByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, float Value)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
    if (MID == nullptr)
    {
        MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
    }
    if (MID)
        MID->SetScalarParameterValue(ParameterName, Value);
}

static inline Vector3 TransformVectorNoTranslation(const Matrix4x4& M, const Vector3& v)
{
    return {
        M.m[0][0] * v.x + M.m[0][1] * v.y + M.m[0][2] * v.z,
        M.m[1][0] * v.x + M.m[1][1] * v.y + M.m[1][2] * v.z,
        M.m[2][0] * v.x + M.m[2][1] * v.y + M.m[2][2] * v.z
    };
}

void USkeletalMeshComponent::UpdateCpuSkinnedVertexBuffer()
{
    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh) return;
    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset) return;

    EnsureBonePoseCache(Asset);
    if (bBonePoseDirty)
    {
        RebuildBonePose(Asset);
    }

    const size_t VertexCount = Asset->Vertices.size();
    if (VertexCount == 0) return;

    // Prepare skinned vertices in CPU memory
    // We will either create the dynamic VB once, or update it each frame via Map/Unmap
    bool bNeedRecreate = (SkinnedVertexBuffer == nullptr) || (SkinnedVertexCount != (uint32)VertexCount);

    ID3D11Device* Device = UResourceManager::GetInstance().GetDevice();
    ID3D11DeviceContext* Context = UResourceManager::GetInstance().GetDeviceContext();
    if (!Device || !Context) return;

    // If we are creating for the first time, leverage the existing helper that converts FNormalVertex->FVertexDynamic
    if (bNeedRecreate)
    {
        if (SkinnedVertexBuffer) { SkinnedVertexBuffer->Release(); SkinnedVertexBuffer = nullptr; }

        std::vector<FNormalVertex> Temp;
        Temp.resize(VertexCount);

        for (size_t i = 0; i < VertexCount; ++i)
        {
            const FSkinnedVertex& SV = Asset->Vertices[i];

            // Position skinning
            Vector3 p{ SV.pos.X, SV.pos.Y, SV.pos.Z };
            Vector3 skinnedP{ 0, 0, 0 };

            // Normal skinning
            Vector3 n{ SV.normal.X, SV.normal.Y, SV.normal.Z };
            Vector3 skinnedN{ 0, 0, 0 };

            for (int s = 0; s < 4; ++s)
            {
                const float w = SV.boneWeights[s];
                const int bi = SV.boneIndices[s];
                if (w <= 0.0f) continue;
                if (bi < 0 || bi >= (int)Asset->Bones.size()) continue;

                const Matrix4x4& SkinMatrix = GetSkinningMatrixForIndex(Asset, bi);
                const Vector3 tp = SkinMatrix.TransformPosition(p);
                const Vector3 tn = TransformVectorNoTranslation(SkinMatrix, n);
                skinnedP = skinnedP + (tp * w);
                skinnedN = skinnedN + (tn * w);
            }

            // Normalize normal
            float len = std::sqrt(skinnedN.x * skinnedN.x + skinnedN.y * skinnedN.y + skinnedN.z * skinnedN.z);
            if (len > 1e-6f) { skinnedN.x /= len; skinnedN.y /= len; skinnedN.z /= len; }
            else { skinnedN = { 0,0,1 }; }

            FNormalVertex v{};
            v.pos = FVector(skinnedP.x, skinnedP.y, skinnedP.z);
            v.normal = FVector(skinnedN.x, skinnedN.y, skinnedN.z);
            v.tex = SV.uv;
            v.Tangent = FVector4(0, 0, 0, 0);
            v.color = FVector4(1, 1, 1, 1);
            Temp[i] = v;
        }

        // Create dynamic vertex buffer with CPU write access
        HRESULT hr = D3D11RHI::CreateVertexBufferImpl<FVertexDynamic>(Device, Temp, &SkinnedVertexBuffer, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        if (SUCCEEDED(hr))
        {
            SkinnedVertexCount = static_cast<uint32>(VertexCount);
        }
        else
        {
            // Fallback: leave buffer null and let renderer use bind-pose VB
            if (SkinnedVertexBuffer) { SkinnedVertexBuffer->Release(); SkinnedVertexBuffer = nullptr; }
            SkinnedVertexCount = 0;
        }
        return;
    }

    // Update path: map and write FVertexDynamic directly
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(Context->Map(SkinnedVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) && mapped.pData)
    {
        FVertexDynamic* out = reinterpret_cast<FVertexDynamic*>(mapped.pData);
        for (size_t i = 0; i < VertexCount; ++i)
        {
            const FSkinnedVertex& SV = Asset->Vertices[i];
            Vector3 p{ SV.pos.X, SV.pos.Y, SV.pos.Z };
            Vector3 skinnedP{ 0, 0, 0 };
            Vector3 n{ SV.normal.X, SV.normal.Y, SV.normal.Z };
            Vector3 skinnedN{ 0, 0, 0 };
            for (int s = 0; s < 4; ++s)
            {
                const float w = SV.boneWeights[s];
                const int bi = SV.boneIndices[s];
                if (w <= 0.0f) continue;
                if (bi < 0 || bi >= (int)Asset->Bones.size()) continue;
                const Matrix4x4& SkinMatrix = GetSkinningMatrixForIndex(Asset, bi);
                const Vector3 tp = SkinMatrix.TransformPosition(p);
                const Vector3 tn = TransformVectorNoTranslation(SkinMatrix, n);
                skinnedP = skinnedP + (tp * w);
                skinnedN = skinnedN + (tn * w);
            }

            float len = std::sqrt(skinnedN.x * skinnedN.x + skinnedN.y * skinnedN.y + skinnedN.z * skinnedN.z);
            if (len > 1e-6f) { skinnedN.x /= len; skinnedN.y /= len; skinnedN.z /= len; }
            else { skinnedN = { 0,0,1 }; }

            out[i].Position = FVector(skinnedP.x, skinnedP.y, skinnedP.z);
            out[i].Normal = FVector(skinnedN.x, skinnedN.y, skinnedN.z);
            out[i].UV = SV.uv;
            out[i].Tangent = FVector4(0, 0, 0, 0);
            out[i].Color = FVector4(1, 1, 1, 1);
        }
        Context->Unmap(SkinnedVertexBuffer, 0);
    }
}

void USkeletalMeshComponent::EnsureBonePoseCache(FSkeletalMesh* Asset)
{
    if (CachedPoseAsset == Asset)
    {
        return;
    }
    CachedPoseAsset = Asset;
    InitializeBonePoseCache(Asset);
}

void USkeletalMeshComponent::InitializeBonePoseCache(FSkeletalMesh* Asset)
{
    ReferenceLocalPose.Empty();
    BoneLocalPose.Empty();
    BoneComponentPose.Empty();
    BoneSkinningPose.Empty();
    BoneEvaluationState.Empty();
    bBonePoseDirty = true;

    if (!Asset)
    {
        return;
    }

    const int32 BoneCount = Asset->Bones.Num();
    ReferenceLocalPose.SetNum(BoneCount);
    BoneLocalPose.SetNum(BoneCount);
    BoneComponentPose.SetNum(BoneCount);
    BoneSkinningPose.SetNum(BoneCount);
    BoneEvaluationState.SetNum(BoneCount);
    std::fill(BoneEvaluationState.begin(), BoneEvaluationState.end(), 0);

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const Bone& SrcBone = Asset->Bones[BoneIndex];
        Matrix4x4 Local = SrcBone.BoneTransform;
        const int32 ParentIndex = SrcBone.ParentIndex;
        if (ParentIndex >= 0 && ParentIndex < BoneCount)
        {
            const Matrix4x4& ParentGlobal = Asset->Bones[ParentIndex].BoneTransform;
            Matrix4x4 ParentInverse = MatrixInverse4x4(ParentGlobal);
            Local = MatrixMultiply4x4(SrcBone.BoneTransform, ParentInverse);
        }

        ReferenceLocalPose[BoneIndex] = Local;
        BoneLocalPose[BoneIndex] = Local;
        BoneComponentPose[BoneIndex] = SrcBone.BoneTransform;
        BoneSkinningPose[BoneIndex] = MatrixMultiply4x4(Asset->Bones[BoneIndex].InverseBindPose, SrcBone.BoneTransform);
    }
}

void USkeletalMeshComponent::RebuildBonePose(FSkeletalMesh* Asset)
{
    if (!Asset || BoneLocalPose.IsEmpty())
    {
        return;
    }

    const int32 BoneCount = BoneLocalPose.Num();
    if (BoneEvaluationState.Num() != BoneCount)
    {
        BoneEvaluationState.SetNum(BoneCount);
    }
    std::fill(BoneEvaluationState.begin(), BoneEvaluationState.end(), 0);

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        BuildComponentSpaceBone(Asset, BoneIndex);
    }

    bBonePoseDirty = false;
}

Matrix4x4 USkeletalMeshComponent::BuildComponentSpaceBone(FSkeletalMesh* Asset, int32 BoneIndex)
{
    if (!Asset || BoneIndex < 0 || BoneIndex >= BoneLocalPose.Num())
    {
        return IdentityMatrix4x4();
    }

    if (!BoneEvaluationState.IsEmpty())
    {
        if (BoneEvaluationState[BoneIndex] == 2)
        {
            return BoneComponentPose[BoneIndex];
        }

        if (BoneEvaluationState[BoneIndex] == 1)
        {
            return BoneComponentPose[BoneIndex];
        }

        BoneEvaluationState[BoneIndex] = 1;
    }

    Matrix4x4 Global = BoneLocalPose[BoneIndex];
    const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
    if (ParentIndex >= 0 && ParentIndex < BoneLocalPose.Num())
    {
        const Matrix4x4 ParentGlobal = BuildComponentSpaceBone(Asset, ParentIndex);
        Global = MatrixMultiply4x4(Global, ParentGlobal);
    }

    BoneComponentPose[BoneIndex] = Global;
    BoneSkinningPose[BoneIndex] = MatrixMultiply4x4(Asset->Bones[BoneIndex].InverseBindPose, Global);
    if (!BoneEvaluationState.IsEmpty())
    {
        BoneEvaluationState[BoneIndex] = 2;
    }
    return Global;
}

const Matrix4x4& USkeletalMeshComponent::GetSkinningMatrixForIndex(FSkeletalMesh* Asset, int32 BoneIndex) const
{
    if (!BoneSkinningPose.IsEmpty() && BoneIndex >= 0 && BoneIndex < BoneSkinningPose.Num())
    {
        return BoneSkinningPose[BoneIndex];
    }

    if (Asset && BoneIndex >= 0 && BoneIndex < Asset->Bones.Num())
    {
        const Bone& SrcBone = Asset->Bones[BoneIndex];
        static thread_local Matrix4x4 TempSkinMatrix;
        TempSkinMatrix = MatrixMultiply4x4(SrcBone.InverseBindPose, SrcBone.BoneTransform);
        return TempSkinMatrix;
    }

    return IdentityMatrix4x4();
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const Matrix4x4& InLocalTransform)
{
    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
        return;

    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset)
        return;

    EnsureBonePoseCache(Asset);
    if (BoneIndex < 0 || BoneIndex >= BoneLocalPose.Num())
        return;

    BoneLocalPose[BoneIndex] = InLocalTransform;
    bBonePoseDirty = true;
}

void USkeletalMeshComponent::ResetBoneLocalTransforms()
{
    if (ReferenceLocalPose.IsEmpty())
    {
        return;
    }
    BoneLocalPose = ReferenceLocalPose;
    bBonePoseDirty = true;
}

void USkeletalMeshComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    TMap<UMaterialInstanceDynamic*, UMaterialInstanceDynamic*> OldToNewMIDMap;
    DynamicMaterialInstances.Empty();

    for (int32 i = 0; i < MaterialSlots.Num(); ++i)
    {
        UMaterialInterface* CurrentSlot = MaterialSlots[i];
        if (UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(CurrentSlot))
        {
            UMaterialInstanceDynamic* NewMID = nullptr;
            if (OldToNewMIDMap.Contains(OldMID))
            {
                NewMID = OldToNewMIDMap[OldMID];
            }
            else
            {
                UMaterialInterface* Parent = OldMID->GetParentMaterial();
                if (!Parent)
                {
                    MaterialSlots[i] = nullptr;
                    continue;
                }
                NewMID = UMaterialInstanceDynamic::Create(Parent);
                if (NewMID)
                {
                    NewMID->CopyParametersFrom(OldMID);
                    DynamicMaterialInstances.Add(NewMID);
                    OldToNewMIDMap.Add(OldMID, NewMID);
                }
            }
            MaterialSlots[i] = NewMID;
        }
    }
}

void USkeletalMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    const FString MaterialSlotsKey = "MaterialSlots";

    if (bInIsLoading)
    {
        ClearDynamicMaterials();

        JSON SlotsArrayJson;
        if (FJsonSerializer::ReadArray(InOutHandle, MaterialSlotsKey, SlotsArrayJson, JSON::Make(JSON::Class::Array), false))
        {
            MaterialSlots.resize(SlotsArrayJson.size());

            for (int i = 0; i < SlotsArrayJson.size(); ++i)
            {
                JSON& SlotJson = SlotsArrayJson.at(i);
                if (SlotJson.IsNull())
                {
                    MaterialSlots[i] = nullptr;
                    continue;
                }

                FString ClassName;
                FJsonSerializer::ReadString(SlotJson, "Type", ClassName, "None", false);

                UMaterialInterface* LoadedMaterial = nullptr;
                if (ClassName == UMaterialInstanceDynamic::StaticClass()->Name)
                {
                    UMaterialInstanceDynamic* NewMID = new UMaterialInstanceDynamic();
                    NewMID->Serialize(true, SlotJson);
                    DynamicMaterialInstances.Add(NewMID);
                    LoadedMaterial = NewMID;
                }
                else
                {
                    FString AssetPath;
                    FJsonSerializer::ReadString(SlotJson, "AssetPath", AssetPath, "", false);
                    if (!AssetPath.empty())
                    {
                        LoadedMaterial = UResourceManager::GetInstance().Load<UMaterial>(AssetPath);
                    }
                    else
                    {
                        LoadedMaterial = nullptr;
                    }
                }
                MaterialSlots[i] = LoadedMaterial;
            }
        }
    }
    else
    {
        JSON SlotsArrayJson = JSON::Make(JSON::Class::Array);
        for (UMaterialInterface* Mtl : MaterialSlots)
        {
            JSON SlotJson = JSON::Make(JSON::Class::Object);
            if (Mtl == nullptr)
            {
                SlotJson["Type"] = "None";
            }
            else
            {
                SlotJson["Type"] = Mtl->GetClass()->Name;
                Mtl->Serialize(false, SlotJson);
            }
            SlotsArrayJson.append(SlotJson);
        }
        InOutHandle[MaterialSlotsKey] = SlotsArrayJson;
    }
}
