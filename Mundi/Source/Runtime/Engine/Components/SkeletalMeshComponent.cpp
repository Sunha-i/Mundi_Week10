#include "pch.h"
#include "SkeletalMeshComponent.h"

#include "SkeletalMesh.h"
#include "ResourceManager.h"
#include "Material.h"
#include "Texture.h"
#include "Shader.h"
#include "JsonSerializer.h"
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
    ClearDynamicMaterials();
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

// 컴포넌트가 소유한 모든 UMaterialInstanceDynamic을 삭제하고, 관련 배열을 비웁니다.
void USkeletalMeshComponent::ClearDynamicMaterials()
{
    for (UMaterialInstanceDynamic* MID : DynamicMaterialInstances)
    {
        delete MID;
    }
    DynamicMaterialInstances.Empty();

    // MID 해제 후 슬롯 비우기 (안전)
    MaterialSlots.Empty();
}

void USkeletalMeshComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    // StaticMeshComponent와 동일한 MID 복제 정책
    TMap<UMaterialInstanceDynamic*, UMaterialInstanceDynamic*> OldToNewMIDMap;

    // 복사본의 소유 리스트 초기화 (메모리 해제 아님)
    DynamicMaterialInstances.Empty();

    for (int32 i = 0; i < MaterialSlots.Num(); ++i)
    {
        UMaterialInterface* CurrentSlot = MaterialSlots[i];
        UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(CurrentSlot);
        if (!OldMID)
            continue;

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
            NewMID->CopyParametersFrom(OldMID);
            DynamicMaterialInstances.Add(NewMID);
            OldToNewMIDMap.Add(OldMID, NewMID);
        }

        MaterialSlots[i] = NewMID;
    }
}

void USkeletalMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    const FString MaterialSlotsKey = "MaterialSlots";

    if (bInIsLoading)
    {
        // 기존 동적 인스턴스 정리 후 로드
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
