#include "pch.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "WorldPartitionManager.h"
#include "MeshBatchElement.h"
#include "SceneView.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)
BEGIN_PROPERTIES(USkeletalMeshComponent)
	MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링하는 컴포넌트입니다.")
	ADD_PROPERTY_SKELETALMESH(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
	ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
	USkeletalMesh* NewSkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(PathFileName);
	if (NewSkeletalMesh && NewSkeletalMesh->GetSkeletalMeshAsset())
	{
		if (SkeletalMesh != NewSkeletalMesh)
		{
			SkeletalMesh = NewSkeletalMesh;
			
			const TArray<FGroupInfo>& GroupInfos = SkeletalMesh->GetMeshGroupInfo();
			MaterialSlots.resize(GroupInfos.size());
			for (int i = 0; i < GroupInfos.size(); ++i)
			{
				SetMaterialByName(i, GroupInfos[i].InitialMaterialName);
			}
			MarkWorldPartitionDirty();
		}
	}
	else
	{
		SkeletalMesh = nullptr;
		MaterialSlots.Empty();
		MarkWorldPartitionDirty();
	}
}

void USkeletalMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	auto DetermineMaterialAndShader = [&](uint32 SectionIndex) ->TPair<UMaterialInterface*, UShader*>
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
			if (Material)
			{
				Shader = Material->GetShader();
			}
		}

		if (!Material || !Shader)
		{
			UE_LOG("USkeletalMeshComponent: 기본 머티리얼 또는 셰이더를 찾을 수 없습니다.");
			return { nullptr, nullptr };
		}
		return { Material, Shader };
	};

	const TArray<FGroupInfo>& MeshGroupInfos = SkeletalMesh->GetMeshGroupInfo();
	const bool bHasSections = !MeshGroupInfos.IsEmpty();
	const uint32 NumSectionsToProcess = bHasSections ? static_cast<uint32>(MeshGroupInfos.size()) : 1;

	for (uint32 SectionIndex = 0; SectionIndex < NumSectionsToProcess; ++SectionIndex)
	{
		uint32 IndexCount = 0;
		uint32 StartIndex = 0;

		if (bHasSections)
		{
			const FGroupInfo& Group = MeshGroupInfos[SectionIndex];
			IndexCount = Group.IndexCount;
			StartIndex = Group.StartIndex;
		}
		else
		{
			IndexCount = SkeletalMesh->GetIndexCount();
			StartIndex = 0;
		}

		if (IndexCount == 0)
		{
			continue;
		}

		auto [MaterialToUse, ShaderToUse] = DetermineMaterialAndShader(SectionIndex);
		if (!MaterialToUse || !ShaderToUse)
		{
			continue;
		}

		FMeshBatchElement BatchElement;
		TArray<FShaderMacro> ShaderMacros = View->ViewShaderMacros;
		ShaderMacros.Add(FShaderMacro("SKINNED", "1"));
		if (0 < MaterialToUse->GetShaderMacros().Num())
		{
			ShaderMacros.Append(MaterialToUse->GetShaderMacros());
		}
		FShaderVariant* ShaderVariant = ShaderToUse->GetOrCompileShaderVariant(ShaderMacros);
		
		if (ShaderVariant)
		{
			BatchElement.VertexShader = ShaderVariant->VertexShader;
			BatchElement.PixelShader = ShaderVariant->PixelShader;
			BatchElement.InputLayout = ShaderVariant->InputLayout;
		}
		else
		{
			UE_LOG("USkeletalMeshComponent: Cannot compile skinning shader variant");
			continue;
		}

		BatchElement.Material = MaterialToUse;
		BatchElement.VertexBuffer = SkeletalMesh->GetVertexBuffer();
		BatchElement.IndexBuffer = SkeletalMesh->GetIndexBuffer();
		BatchElement.VertexStride = SkeletalMesh->GetVertexStride();
		BatchElement.IndexCount = IndexCount;
		BatchElement.StartIndex = StartIndex;
		BatchElement.BaseVertexIndex = 0;
		BatchElement.WorldMatrix = GetWorldMatrix();
		BatchElement.ObjectID = InternalIndex;
		BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		BatchElement.bIsSkinned = true;

		// Set bone transform matrix for T-pose rendering
		const FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
		if (MeshAsset)
		{
			const int32 NumBones = MeshAsset->Bones.Num();
			if (NumBones > 0)
			{
				BatchElement.BoneTransforms.SetNum(NumBones);
				for (int32 i = 0; i < NumBones; ++i)
				{
					BatchElement.BoneTransforms[i] = FMatrix::Identity();
				}
			}
		}

		OutMeshBatchElements.Add(BatchElement);
	}
}

UMaterialInterface* USkeletalMeshComponent::GetMaterial(uint32 InSectionIndex) const
{
	if (MaterialSlots.size() <= InSectionIndex)
	{
		return nullptr;
	}
	return MaterialSlots[InSectionIndex];
}

void USkeletalMeshComponent::SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
{
	if (InElementIndex >= static_cast<uint32>(MaterialSlots.Num()))
	{
		return;
	}
	MaterialSlots[InElementIndex] = InNewMaterial;
}

void USkeletalMeshComponent::MarkWorldPartitionDirty()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldPartitionManager* Partition = World->GetPartitionManager())
		{
			Partition->MarkDirty(this);
		}
	}
}