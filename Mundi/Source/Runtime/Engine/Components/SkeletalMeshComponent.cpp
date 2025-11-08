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

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
    MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링합니다.")
    ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
    // Set default skeletal mesh (T-pose) using relative data dir
    extern const FString GDataDir;
    SetSkeletalMesh(GDataDir + "/Model/Ch46_nonPBR.fbx");
}

USkeletalMeshComponent::~USkeletalMeshComponent()
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
        Batch.VertexBuffer = Mesh->GetVertexBuffer();
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
