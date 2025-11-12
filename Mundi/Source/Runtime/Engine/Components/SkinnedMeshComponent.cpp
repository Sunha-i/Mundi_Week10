#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"
#include "MeshBatchElement.h"
#include "SceneView.h"
#include "WorldPartitionManager.h"
#include "RHIDevice.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

BEGIN_PROPERTIES(USkinnedMeshComponent)
    // MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링하는 컴포넌트입니다.")
    ADD_PROPERTY_SKELETALMESH(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
    ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

USkinnedMeshComponent::USkinnedMeshComponent() {}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
    if (SkeletalMesh)
        ObjectFactory::DeleteObject(SkeletalMesh);
    ClearDynamicMaterials();

    // CPU 스키닝용 동적 버텍스 버퍼 해제
    if (DynamicVertexBuffer)
    {
        DynamicVertexBuffer->Release();
        DynamicVertexBuffer = nullptr;
    }
}

void USkinnedMeshComponent::ClearDynamicMaterials()
{
    // 1. 생성된 동적 머티리얼 인스턴스 해제
    for (UMaterialInstanceDynamic* MID : DynamicMaterialInstances)
    {
        ObjectFactory::DeleteObject(MID);
    }
    DynamicMaterialInstances.Empty();

    // 2. 머티리얼 슬롯 배열도 비웁니다.
    // (이 배열이 MID 포인터를 가리키고 있었을 수 있으므로
    //  delete 이후에 비워야 안전합니다.)
    MaterialSlots.Empty();
}

void USkinnedMeshComponent::CollectMeshBatches(
    TArray<FMeshBatchElement>& OutMeshBatchElements,
    const FSceneView* View
)
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return;
    }

    FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
    const TArray<FFlesh>& FleshesInfo = SkeletalMesh->GetFleshesInfo();

    // ====== CPU 스키닝: 각 정점마다 본 변환 적용 ======
    const TArray<FNormalVertex>& OriginalVertices = MeshAsset->Vertices;
    TArray<FNormalVertex> TransformedVertices = OriginalVertices; // 복사본 생성

    // Flesh의 Bones 리스트 사용 (첫 번째 섹션에 보통 모든 본이 있음)
    const TArray<UBone*>* BoneListPtr = nullptr;
    if (!FleshesInfo.IsEmpty() && FleshesInfo[0].Bones.size() > 0)
    {
        BoneListPtr = &FleshesInfo[0].Bones;
    }

    bool bDoSkinning = (BoneListPtr != nullptr);

    // 본 리스트가 없으면 스키닝 없이 원본 버텍스 사용
    if (!bDoSkinning)
    {
        UE_LOG("Warning: No Bones in Flesh, rendering without skinning");
    }
    else
    {
        UE_LOG("CPU Skinning: VertexCount=%d, BoneCount=%d", OriginalVertices.size(), BoneListPtr->size());
    }

    if (bDoSkinning)
    {
        for (size_t VertIdx = 0; VertIdx < TransformedVertices.size(); ++VertIdx)
        {
            FNormalVertex& Vertex = TransformedVertices[VertIdx];
            const FNormalVertex& OriginalVertex = OriginalVertices[VertIdx];

            // 스키닝 변환 적용 (최대 4개 본)
            FVector TransformedPos(0.0f, 0.0f, 0.0f);
            FVector TransformedNormal(0.0f, 0.0f, 0.0f);
            float TotalWeight = 0.0f;

        // 각 본의 영향을 가중치로 합산 (Row-major 행렬 곱셈)
        for (int i = 0; i < 4; ++i)
        {
            uint32 BoneIdx = OriginalVertex.BoneIndices[i];
            float Weight = OriginalVertex.BoneWeights[i];

            if (Weight < 0.0001f)
                continue;

            const TArray<UBone*>& BoneList = *BoneListPtr;
            if (BoneIdx >= BoneList.size())
            {
                UE_LOG("Warning: BoneIdx %d out of range (BoneList.size=%d)", BoneIdx, BoneList.size());
                continue;
            }

            UBone* Bone = BoneList[BoneIdx];
            if (!Bone)
            {
                UE_LOG("Warning: Bone at index %d is null", BoneIdx);
                continue;
            }

            // BoneMatrix = OffsetMatrix * CurrentBoneWorld (Row-major)
            FMatrix OffsetMatrix = Bone->GetOffsetMatrix();
            FMatrix BoneWorldMatrix = Bone->GetWorldTransform().ToMatrix();
           // FMatrix BoneMatrix = OffsetMatrix * BoneWorldMatrix;
            FMatrix BoneMatrix = BoneWorldMatrix * OffsetMatrix ;

            // 정점 변환 (Row-major): Vertex * Matrix
            // Position 변환
            FVector4 PosVec4(OriginalVertex.pos.X, OriginalVertex.pos.Y, OriginalVertex.pos.Z, 1.0f);
            FVector4 TransformedPosVec4(
                PosVec4.X * BoneMatrix.M[0][0] + PosVec4.Y * BoneMatrix.M[1][0] + PosVec4.Z * BoneMatrix.M[2][0] + PosVec4.W * BoneMatrix.M[3][0],
                PosVec4.X * BoneMatrix.M[0][1] + PosVec4.Y * BoneMatrix.M[1][1] + PosVec4.Z * BoneMatrix.M[2][1] + PosVec4.W * BoneMatrix.M[3][1],
                PosVec4.X * BoneMatrix.M[0][2] + PosVec4.Y * BoneMatrix.M[1][2] + PosVec4.Z * BoneMatrix.M[2][2] + PosVec4.W * BoneMatrix.M[3][2],
                PosVec4.X * BoneMatrix.M[0][3] + PosVec4.Y * BoneMatrix.M[1][3] + PosVec4.Z * BoneMatrix.M[2][3] + PosVec4.W * BoneMatrix.M[3][3]
            );

            TransformedPos += FVector(TransformedPosVec4.X, TransformedPosVec4.Y, TransformedPosVec4.Z) * Weight;

            // Normal 변환 (w=0, translation 무시)
            FVector4 NormalVec4(OriginalVertex.normal.X, OriginalVertex.normal.Y, OriginalVertex.normal.Z, 0.0f);
            FVector4 TransformedNormalVec4(
                NormalVec4.X * BoneMatrix.M[0][0] + NormalVec4.Y * BoneMatrix.M[1][0] + NormalVec4.Z * BoneMatrix.M[2][0],
                NormalVec4.X * BoneMatrix.M[0][1] + NormalVec4.Y * BoneMatrix.M[1][1] + NormalVec4.Z * BoneMatrix.M[2][1],
                NormalVec4.X * BoneMatrix.M[0][2] + NormalVec4.Y * BoneMatrix.M[1][2] + NormalVec4.Z * BoneMatrix.M[2][2],
                0.0f
            );

            TransformedNormal += FVector(TransformedNormalVec4.X, TransformedNormalVec4.Y, TransformedNormalVec4.Z) * Weight;
            TotalWeight += Weight;
        }

        // 가중치가 0이면 원본 위치 사용 (리지드 정점)
        if (TotalWeight < 0.0001f)
        {
            // 가중치가 없는 정점은 원본 그대로
            continue;
        }

        // 변환된 위치와 노말 저장
        Vertex.pos = TransformedPos;
        float NormalLengthSq = TransformedNormal.X * TransformedNormal.X +
                                TransformedNormal.Y * TransformedNormal.Y +
                                TransformedNormal.Z * TransformedNormal.Z;
        if (NormalLengthSq > 0.0001f)
        {
            TransformedNormal.Normalize();
            Vertex.normal = TransformedNormal;
        }

            // 첫 번째 정점 디버그 로그
            if (VertIdx == 0)
            {
                UE_LOG("First Vertex - Original: (%.3f, %.3f, %.3f), Transformed: (%.3f, %.3f, %.3f), TotalWeight: %.3f",
                    OriginalVertex.pos.X, OriginalVertex.pos.Y, OriginalVertex.pos.Z,
                    Vertex.pos.X, Vertex.pos.Y, Vertex.pos.Z,
                    TotalWeight);
            }
        }
    }

    // ====== Dynamic Vertex Buffer 생성/업데이트 ======
    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    ID3D11DeviceContext* Context = GEngine.GetRHIDevice()->GetDeviceContext();

    size_t RequiredSize = TransformedVertices.size() * sizeof(FNormalVertex);
    if (!DynamicVertexBuffer || DynamicVertexBufferSize < RequiredSize)
    {
        if (DynamicVertexBuffer)
        {
            DynamicVertexBuffer->Release();
            DynamicVertexBuffer = nullptr;
        }

        D3D11_BUFFER_DESC BufferDesc = {};
        BufferDesc.ByteWidth = (UINT)RequiredSize;
        BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA InitData = {};
        InitData.pSysMem = TransformedVertices.data();

        HRESULT hr = Device->CreateBuffer(&BufferDesc, &InitData, &DynamicVertexBuffer);
        if (FAILED(hr))
        {
            UE_LOG("Failed to create dynamic vertex buffer for skinned mesh");
            return;
        }

        DynamicVertexBufferSize = RequiredSize;
    }
    else
    {
        // 기존 버퍼 업데이트
        D3D11_MAPPED_SUBRESOURCE MappedResource;
        HRESULT hr = Context->Map(DynamicVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
        if (SUCCEEDED(hr))
        {
            memcpy(MappedResource.pData, TransformedVertices.data(), RequiredSize);
            Context->Unmap(DynamicVertexBuffer, 0);
        }
    }

    // ====== Mesh Batch 생성 ======
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
            if (Material)
            {
                Shader = Material->GetShader();
            }
            if (!Material || !Shader)
            {
                return { nullptr, nullptr };
            }
        }
        return { Material, Shader };
    };

    const bool bHasSections = !FleshesInfo.IsEmpty();
    const uint32 NumSectionsToProcess = bHasSections ? static_cast<uint32>(FleshesInfo.size()) : 1;

    for (uint32 SectionIndex = 0; SectionIndex < NumSectionsToProcess; ++SectionIndex)
    {
        uint32 IndexCount = 0;
        uint32 StartIndex = 0;

        if (bHasSections)
        {
            IndexCount = FleshesInfo[SectionIndex].IndexCount;
            StartIndex = FleshesInfo[SectionIndex].StartIndex;
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

        BatchElement.Material = MaterialToUse;
        BatchElement.VertexBuffer = DynamicVertexBuffer; // CPU 스키닝된 버퍼 사용
        BatchElement.IndexBuffer = SkeletalMesh->GetIndexBuffer();
        BatchElement.VertexStride = sizeof(FNormalVertex);
        BatchElement.IndexCount = IndexCount;
        BatchElement.StartIndex = StartIndex;
        BatchElement.BaseVertexIndex = 0;

        // 월드 행렬은 컴포넌트 월드 행렬만 사용 (스키닝은 이미 정점에 적용됨)
        BatchElement.WorldMatrix = GetWorldMatrix();
        BatchElement.ObjectID = InternalIndex;
        BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        OutMeshBatchElements.Add(BatchElement);
    }
}

void USkinnedMeshComponent::Serialize(
    const bool bInIsLoading,
    JSON& InOutHandle
)
{
    // Super::Serialize가 프로퍼티 시스템을 통해 SkeletalMesh를 자동으로 저장/로드함
    Super::Serialize(bInIsLoading, InOutHandle);

    const FString MaterialSlotsKey = "MaterialSlots";

    if (bInIsLoading) // --- 로드 ---
    {
        // 로드 전 기존 동적 인스턴스 모두 정리
        ClearDynamicMaterials();

        JSON SlotsArrayJson;
        if (FJsonSerializer::ReadArray(
            InOutHandle,
            MaterialSlotsKey,
            SlotsArrayJson,
            JSON::Make(JSON::Class::Array),
            false
        ))
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

                // 2. JSON에서 클래스 이름 읽기
                FString ClassName;
                FJsonSerializer::ReadString(SlotJson, "Type", ClassName, "None", false);

                UMaterialInterface* LoadedMaterial = nullptr;

                // 3. 클래스 이름에 따라 분기
                if (ClassName == UMaterialInstanceDynamic::StaticClass()->Name)
                {
                    // UMID는 인스턴스이므로, NewObject로 생성합니다.
                    UMaterialInstanceDynamic* NewMID = NewObject<UMaterialInstanceDynamic>();

                    // 4. 생성된 빈 객체에 Serialize(true)를 호출하여 데이터를 채웁니다.
                    NewMID->Serialize(true, SlotJson);

                    // 5. 소유권 추적 배열에 추가합니다.
                    DynamicMaterialInstances.Add(NewMID);
                    LoadedMaterial = NewMID;
                }
                else // if(ClassName == UMaterial::StaticClass()->Name)
                {
                    // UMaterial은 리소스이므로, AssetPath로 리소스 매니저에서 로드합니다.
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
    else // --- 저장 ---
    {
        // Super::Serialize가 프로퍼티 시스템을 통해 SkeletalMesh를 자동으로 저장함

        // MaterialSlots 저장
        JSON SlotsArrayJson = JSON::Make(JSON::Class::Array);
        for (UMaterialInterface* Mtl : MaterialSlots)
        {
            JSON SlotJson = JSON::Make(JSON::Class::Object);

            if (Mtl == nullptr)
            {
                SlotJson["Type"] = "None"; // null 슬롯 표시
            }
            else
            {
                // 1. 클래스 이름 저장 (로드 시 팩토리 구분을 위함)
                SlotJson["Type"] = Mtl->GetClass()->Name;

                // 2. 객체 스스로 데이터를 저장하도록 위임
                Mtl->Serialize(false, SlotJson);
            }

            SlotsArrayJson.append(SlotJson);
        }
        InOutHandle[MaterialSlotsKey] = SlotsArrayJson;
    }
}

void USkinnedMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
    // 새 메시를 설정하기 전에, 기존에 생성된 모든 MID와 슬롯 정보를 정리합니다.
    ClearDynamicMaterials();

    // PathFileName이 비어있거나 "None"이면 nullptr로 설정
    if (PathFileName.empty() || PathFileName == "None")
    {
        SkeletalMesh = nullptr;
        return;
    }

    // 새 메시를 로드합니다.
    USkeletalMesh* LoadedMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(PathFileName);

    if (LoadedMesh)
    {
        SkeletalMesh = LoadedMesh->Duplicate();
    }
    else
    {
        SkeletalMesh = nullptr;
    }

    if (SkeletalMesh && SkeletalMesh->GetSkeletalMeshAsset())
    {
        const TArray<FFlesh>& FleshesInfo = SkeletalMesh->GetFleshesInfo();

        // 새 메시 정보에 맞게 슬롯을 재설정합니다.
        MaterialSlots.resize(FleshesInfo.size());

        for (int i = 0; i < FleshesInfo.size(); ++i)
        {
            SetMaterialByName(i, FleshesInfo[i].InitialMaterialName);
        }
        MarkWorldPartitionDirty();
    }
    else
    {
        // 메시 로드에 실패한 경우, SkeletalMesh 포인터를 nullptr로 보장합니다.
        SkeletalMesh = nullptr;
    }
}

USkeletalMesh* USkinnedMeshComponent::GetSkeletalMesh() const
{
    return SkeletalMesh;
}

UMaterialInterface* USkinnedMeshComponent::GetMaterial(uint32 InSectionIndex) const
{
    if (MaterialSlots.size() <= InSectionIndex)
    {
        return nullptr;
    }

    UMaterialInterface* FoundMaterial = MaterialSlots[InSectionIndex];

    if (!FoundMaterial)
    {
        UE_LOG("GetMaterial: Failed to find material Section %d", InSectionIndex);
        return nullptr;
    }

    return FoundMaterial;
}

void USkinnedMeshComponent::SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
{
    if (InElementIndex >= static_cast<uint32>(MaterialSlots.Num()))
    {
        UE_LOG("out of range InMaterialSlotIndex: %d", InElementIndex);
        return;
    }

    // 1. 현재 슬롯에 할당된 머티리얼을 가져옵니다.
    UMaterialInterface* OldMaterial = MaterialSlots[InElementIndex];

    // 2. 교체될 새 머티리얼이 현재 머티리얼과 동일하면 아무것도 하지 않습니다.
    if (OldMaterial == InNewMaterial)
    {
        return;
    }

    // 3. 기존 머티리얼이 이 컴포넌트가 소유한 MID인지 확인합니다.
    if (OldMaterial != nullptr)
    {
        UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(OldMaterial);
        if (OldMID)
        {
            // 4. 소유권 리스트(DynamicMaterialInstances)에서 이 MID를 찾아 제거합니다.
            // TArray::Remove()는 첫 번째로 발견된 항목만 제거합니다.
            int32 RemovedCount = DynamicMaterialInstances.Remove(OldMID);

            if (RemovedCount > 0)
            {
                // 5. 소유권 리스트에서 제거된 것이 확인되면, 메모리에서 삭제합니다.
                ObjectFactory::DeleteObject(OldMID);
            }
            else
            {
                // 경고: MaterialSlots에는 MID가 있었으나, 소유권 리스트에 없는 경우입니다.
                // 이는 DuplicateSubObjects 등이 잘못 구현되었을 때 발생할 수 있습니다.
                UE_LOG("Warning: SetMaterial is replacing a MID that was not tracked by DynamicMaterialInstances.");
                // 이 경우 delete를 호출하면 다른 객체가 소유한 메모리를 해제할 수 있으므로
                // delete를 호출하지 않는 것이 더 안전할 수 있습니다. (혹은 delete 호출 후 크래시로 버그를 잡습니다.)
                // 여기서는 삭제를 시도합니다.
                ObjectFactory::DeleteObject(OldMID);
            }
        }
    }

    // 6. 새 머티리얼을 슬롯에 할당합니다.
    MaterialSlots[InElementIndex] = InNewMaterial;
}
    
UMaterialInstanceDynamic* USkinnedMeshComponent::CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(ElementIndex);
    if (!CurrentMaterial)
    {
        return nullptr;
    }

    // 이미 MID인 경우, 그대로 반환
    if (UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(CurrentMaterial))
    {
        return ExistingMID;
    }

    // 현재 머티리얼(UMaterial 또는 다른 MID가 아닌 UMaterialInterface)을 부모로 하는 새로운 MID를 생성
    UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(CurrentMaterial);
    if (NewMID)
    {
        DynamicMaterialInstances.Add(NewMID); // 소멸자에서 해제하기 위해 추적
        SetMaterial(ElementIndex, NewMID);    // 슬롯에 새로 만든 MID 설정
        NewMID->SetFilePath("(Instance) " + CurrentMaterial->GetFilePath());
        return NewMID;
    }

    return nullptr;
}
    
const TArray<UMaterialInterface*> USkinnedMeshComponent::GetMaterialSlots() const
{
    return MaterialSlots;
}
    
void USkinnedMeshComponent::SetMaterialTextureByUser(
    const uint32 InMaterialSlotIndex,
    EMaterialTextureSlot Slot,
    UTexture* Texture)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
    if (MID == nullptr)
    {
        MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
    }
    MID->SetTextureParameterValue(Slot, Texture);
}

void USkinnedMeshComponent::SetMaterialColorByUser(
    const uint32 InMaterialSlotIndex,
    const FString& ParameterName,
    const FLinearColor& Value
)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
    if (MID == nullptr)
    {
        MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
    }
    MID->SetColorParameterValue(ParameterName, Value);
}

void USkinnedMeshComponent::SetMaterialScalarByUser(
    const uint32 InMaterialSlotIndex,
    const FString& ParameterName,
    float Value
)
{
    UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
    if (MID == nullptr)
    {
        MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
    }
    MID->SetScalarParameterValue(ParameterName, Value);
}

FAABB USkinnedMeshComponent::GetWorldAABB() const
{
    {
        const FTransform WorldTransform = GetWorldTransform();
        const FMatrix WorldMatrix = GetWorldMatrix();

        if (!SkeletalMesh)
        {
            const FVector Origin = WorldTransform.TransformPosition(FVector());
            return FAABB(Origin, Origin);
        }

        const FAABB LocalBound = SkeletalMesh->GetLocalBound();
        const FVector LocalMin = LocalBound.Min;
        const FVector LocalMax = LocalBound.Max;

        const FVector LocalCorners[8] = {
            FVector(LocalMin.X, LocalMin.Y, LocalMin.Z),
            FVector(LocalMax.X, LocalMin.Y, LocalMin.Z),
            FVector(LocalMin.X, LocalMax.Y, LocalMin.Z),
            FVector(LocalMax.X, LocalMax.Y, LocalMin.Z),
            FVector(LocalMin.X, LocalMin.Y, LocalMax.Z),
            FVector(LocalMax.X, LocalMin.Y, LocalMax.Z),
            FVector(LocalMin.X, LocalMax.Y, LocalMax.Z),
            FVector(LocalMax.X, LocalMax.Y, LocalMax.Z)
        };

        FVector4 WorldMin4 = FVector4(LocalCorners[0].X, LocalCorners[0].Y, LocalCorners[0].Z, 1.0f) * WorldMatrix;
        FVector4 WorldMax4 = WorldMin4;

        for (int32 CornerIndex = 1; CornerIndex < 8; ++CornerIndex)
        {
            const FVector4 WorldPos = FVector4(LocalCorners[CornerIndex].X
                , LocalCorners[CornerIndex].Y
                , LocalCorners[CornerIndex].Z
                , 1.0f)
                * WorldMatrix;
            WorldMin4 = WorldMin4.ComponentMin(WorldPos);
            WorldMax4 = WorldMax4.ComponentMax(WorldPos);
        }

        FVector WorldMin = FVector(WorldMin4.X, WorldMin4.Y, WorldMin4.Z);
        FVector WorldMax = FVector(WorldMax4.X, WorldMax4.Y, WorldMax4.Z);
        return FAABB(WorldMin, WorldMax);
    }
}

void USkinnedMeshComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    // 이 함수는 '복사본' (PIE 컴포넌트)에서 실행됩니다.
    // 현재 'DynamicMaterialInstances'와 'MaterialSlots'는
    // '원본' (에디터 컴포넌트)의 포인터를 얕은 복사한 상태입니다.

    // 1. SkeletalMesh 깊은 복사 (Bone 조작을 위해 필수)
    if (SkeletalMesh)
    {
        SkeletalMesh = static_cast<USkeletalMesh*>(SkeletalMesh->Duplicate());
    }

    // 2. 원본 MID -> 복사본 MID 매핑 테이블
    TMap<UMaterialInstanceDynamic*, UMaterialInstanceDynamic*> OldToNewMIDMap;

    // 3. 복사본의 MID 소유권 리스트를 비웁니다. (메모리 해제 아님)
    //    이 리스트는 새로운 '복사본 MID'들로 다시 채워질 것입니다.
    DynamicMaterialInstances.Empty();

    // 4. MaterialSlots를 순회하며 MID를 찾습니다.
    for (int32 i = 0; i < MaterialSlots.Num(); ++i)
    {
        UMaterialInterface* CurrentSlot = MaterialSlots[i];
        UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(CurrentSlot);

        if (OldMID)
        {
            UMaterialInstanceDynamic* NewMID = nullptr;

            // 이 MID를 이미 복제했는지 확인합니다 (여러 슬롯이 같은 MID를 쓸 경우)
            if (OldToNewMIDMap.Contains(OldMID))
            {
                NewMID = OldToNewMIDMap[OldMID];
            }
            else
            {
                // 5. MID를 복제합니다.
                UMaterialInterface* Parent = OldMID->GetParentMaterial();
                if (!Parent)
                {
                    // 부모가 없으면 복제할 수 없으므로 nullptr 처리
                    MaterialSlots[i] = nullptr;
                    continue;
                }

                // 5-1. 새로운 MID (PIE용)를 생성합니다.
                NewMID = UMaterialInstanceDynamic::Create(Parent);

                // 5-2. 원본(OldMID)의 파라미터를 새 MID로 복사합니다.
                NewMID->CopyParametersFrom(OldMID);

                // 5-3. 이 컴포넌트(복사본)의 소유권 리스트에 새 MID를 추가합니다.
                DynamicMaterialInstances.Add(NewMID);
                OldToNewMIDMap.Add(OldMID, NewMID);
            }

            // 6. MaterialSlots가 원본(OldMID) 대신 새 복사본(NewMID)을 가리키도록 교체합니다.
            MaterialSlots[i] = NewMID;
        }
        // else (원본 UMaterial 애셋인 경우)
        // 얕은 복사된 포인터(애셋 경로)를 그대로 사용해도 안전합니다.
    }
}

void USkinnedMeshComponent::OnTransformUpdated()
{
    Super::OnTransformUpdated();
    MarkWorldPartitionDirty();
}

void USkinnedMeshComponent::MarkWorldPartitionDirty()
{
    if (UWorld* World = GetWorld())
    {
        if (UWorldPartitionManager* Partition = World->GetPartitionManager())
        {
            Partition->MarkDirty(this);
        }
    }
}