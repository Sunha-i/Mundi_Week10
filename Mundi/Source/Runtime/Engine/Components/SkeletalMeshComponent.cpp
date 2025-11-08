#include "pch.h"
#include "SkeletalMeshComponent.h"

#include "SkeletalMesh.h"
#include "ResourceManager.h"
#include "Material.h"
#include "Shader.h"
#include "SceneView.h"
#include "MeshBatchElement.h"
#include "VertexData.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
    MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링합니다.")
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
    // Set default skeletal mesh (T-pose) using relative data dir
    extern const FString GDataDir;
    SetSkeletalMesh(GDataDir + "/Model/Ch46_nonPBR.fbx");
}

USkeletalMeshComponent::~USkeletalMeshComponent() = default;

void USkeletalMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh || !Mesh->GetSkeletalMeshAsset())
        return;

    const FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();

    const bool bHasSections = !Asset->Sections.IsEmpty();
    const uint32 NumSections = bHasSections ? (uint32)Asset->Sections.size() : 1;

    auto GetMaterialAndShader = [&]() -> TPair<UMaterialInterface*, UShader*>
    {
        UMaterialInterface* Mat = UResourceManager::GetInstance().GetDefaultMaterial();
        if (!Mat) return { nullptr, nullptr };
        UShader* Sh = Mat->GetShader();
        if (!Sh) return { nullptr, nullptr };
        return { Mat, Sh };
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

        auto [MaterialToUse, ShaderToUse] = GetMaterialAndShader();
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
