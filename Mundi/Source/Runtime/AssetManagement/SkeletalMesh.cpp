#include "pch.h"
#include "SkeletalMesh.h"

#include "FbxManager.h"

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::~USkeletalMesh()
{
    if (SkeletalMeshAsset)
         delete SkeletalMeshAsset;

    // UResourceManager 캐시에서 제거 (댕글링 포인터 방지)
    FString FilePath = GetFilePath();
    if (!FilePath.empty())
    {
        UResourceManager::GetInstance().Unload<USkeletalMesh>(FilePath);
    }
};

void USkeletalMesh::Load(
    const FString& InFilePath,
    ID3D11Device* InDevice,
    EVertexLayoutType InVertexType
)
{
    assert(InDevice);

    SetVertexType(InVertexType);

    FFbxManager& FbxManager = FFbxManager::GetInstance();

    FSkeletalMesh* LoadedMesh = FbxManager.LoadFbxSkeletalMeshAsset(InFilePath);

    // 빈 버텍스, 인덱스로 버퍼 생성 방지
    if (LoadedMesh &&
        0 < LoadedMesh->Vertices.size() &&
        0 < LoadedMesh->Indices.size()
    )
    {
        // 복사 생성자로 Bone, Flesh의 정보를 깊은 복사하여야 함.
        SkeletalMeshAsset = new FSkeletalMesh(*LoadedMesh);
        CacheFilePath = SkeletalMeshAsset->CacheFilePath;

        // CPU Skinning용 Dynamic Vertex Buffer 생성
        CreateDynamicVertexBuffer(InDevice, SkeletalMeshAsset->SkinnedVertices.Num());

        CreateIndexBuffer(SkeletalMeshAsset, InDevice);
        CreateLocalBound(SkeletalMeshAsset);
        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());
    }
    else
    {
        MessageBoxA(nullptr, "[USkeletalMesh::Load] Warning : None Loaded Mesh\n", "Error", 0);
    }
}

void USkeletalMesh::CreateDynamicVertexBuffer(ID3D11Device* Device, int VertexCount)
{
    if (VertexBuffer)
    {
        VertexBuffer->Release();
        VertexBuffer = nullptr;
    }

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;  // Dynamic으로 생성
    bufferDesc.ByteWidth = sizeof(FVertexDynamic) * VertexCount;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // CPU에서 쓰기 가능

    HRESULT hr = Device->CreateBuffer(&bufferDesc, nullptr, &VertexBuffer);
    OutputDebugStringA("CreateDynamicBuffer Before Assert\n");
    if (FAILED(hr))
    {
        OutputDebugStringA("Tlqkf");
        exit(1);
    }
    assert(SUCCEEDED(hr) && "Failed to create dynamic vertex buffer");
    OutputDebugStringA("CreateDynamicBuffer After Assert\n");
}
void USkeletalMesh::Load(
    FMeshData* InData,
    ID3D11Device* InDevice,
    EVertexLayoutType InVertexType
)
{
    SetVertexType(InVertexType);

    if (VertexBuffer)
    {
        VertexBuffer->Release();
        VertexBuffer = nullptr;
    }
    if (IndexBuffer)
    {
        IndexBuffer->Release();
        IndexBuffer = nullptr;
    }

    // CreateVertexBuffer(InData, InDevice, InVertexType);
    CreateDynamicVertexBuffer(InDevice, SkeletalMeshAsset->SkinnedVertices.Num());
    CreateIndexBuffer(InData, InDevice);
    CreateLocalBound(InData);

    VertexCount = static_cast<uint32>(InData->Vertices.size());
    IndexCount = static_cast<uint32>(InData->Indices.size());
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
    return SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : FilePath;
}

FSkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const
{
    return SkeletalMeshAsset;
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InSkeletalMesh)
{
    SkeletalMeshAsset = InSkeletalMesh;
}

const TArray<FFlesh>& USkeletalMesh::GetFleshesInfo() const
{
    if (!SkeletalMeshAsset)
    {
        MessageBoxA(
            nullptr,
            "USkeletalMesh::GetFleshesInfo : SkeletalMesh is nullptr",
            "Error",
            1
        );

        exit(1);
    }

    return SkeletalMeshAsset->Fleshes;
}
bool USkeletalMesh::HasMaterial() const
{
    return SkeletalMeshAsset->bHasMaterial;
}

uint64 USkeletalMesh::GetFleshesCount() const
{
    if (!SkeletalMeshAsset)
    {
        MessageBoxA(
            nullptr,
            "USkeletalMesh::GetFleshesInfo : SkeletalMesh is nullptr",
            "Error",
            1
        );

        exit(1);
    }

    return SkeletalMeshAsset->Fleshes.Num();
}

void USkeletalMesh::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    // 부모 클래스 직렬화 (FilePath 저장/로드)
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading) // --- 로드 ---
    {
        // FilePath가 로드되었으므로, 그 경로로 다시 Load하여 GPU 버퍼와 Cooked Data 재생성
        if (!FilePath.empty())
        {
            ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
            Load(FilePath, Device, GetVertexType());
        }
    }
    // 저장 시에는 Super::Serialize가 FilePath를 저장하므로 추가 작업 불필요
}

void USkeletalMesh::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    // 얕은 복사된 리소스들을 nullptr로 초기화 (Load에서 새로 생성)
    FString AssetPath = SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : FString();

    VertexBuffer = nullptr;
    IndexBuffer = nullptr;
    SkeletalMeshAsset = nullptr;  // 얕은 복사된 포인터, 원본 것이므로 nullptr로

    // FSkeletalMesh 깊은 복사 (복사 생성자가 Skeleton과 Fleshes를 모두 복사함)
    if (!AssetPath.empty())
    {
        Load(AssetPath, GEngine.GetRHIDevice()->GetDevice());
    }
}

// ============================================================================
// CPU Skinning - 매 프레임 정점 변환
// ============================================================================
void USkeletalMesh::UpdateCPUSkinning(ID3D11DeviceContext* DeviceContext)
{
    if (!SkeletalMeshAsset || !SkeletalMeshAsset->Skeleton)
        return;

    const TArray<FSkinnedVertex>& SkinnedVerts = SkeletalMeshAsset->SkinnedVertices;
    if (SkinnedVerts.IsEmpty())
        return;

    // 1. 변환된 정점 버퍼 준비
    int VertexCount = SkinnedVerts.Num();
    if (TransformedVertices.Num() != VertexCount)
    {
        TransformedVertices.resize(VertexCount);
    }

    // 2. Bone Skinning Matrix 배열 준비
    // 먼저 Bone 개수를 세어서 배열 크기 확보
    int32 BoneCount = 0;
    SkeletalMeshAsset->Skeleton->ForEachBone([&BoneCount](UBone* Bone)
    {
        if (Bone) BoneCount++;
    });

    // TArray<FMatrix> BoneMatrices;
    // BoneMatrices.resize(BoneCount);

    // 각 Bone의 인덱스에 맞춰 매트릭스 저장
    // int32 BoneIndex = 0;
    // SkeletalMeshAsset->Skeleton->ForEachBone([&](UBone* Bone)
    // {
    //     if (Bone && BoneIndex < BoneCount)
    //     {
    //         BoneMatrices[BoneIndex] = Bone->GetSkinningMatrix();
    //         BoneIndex++;
    //     }
    // });

    // 3. 각 정점마다 CPU Skinning 수행
    for (int i = 0; i < VertexCount; i++)
    {
        const FSkinnedVertex& SrcVertex = SkinnedVerts[i];
        FVertexDynamic& DstVertex = TransformedVertices[i];

        // 원본 데이터 복사 (UV, Tangent, Color는 변환 안 함)
        DstVertex.UV = SrcVertex.UV;
        DstVertex.Tangent = SrcVertex.Tangent;
        DstVertex.Color = SrcVertex.Color;

        // Skinning 계산: Position과 Normal을 Bone Matrix로 변환
        // 먼저 총 Weight 확인
        float TotalWeight = SrcVertex.BoneWeights[0] + SrcVertex.BoneWeights[1] +
                           SrcVertex.BoneWeights[2] + SrcVertex.BoneWeights[3];

        // Weight가 없는 정점은 원본 위치 유지 (Rigid Body)
        if (TotalWeight < 0.0001f)
        {
            DstVertex.Position = SrcVertex.Position;
            DstVertex.Normal = SrcVertex.Normal;
            continue;
        }

        FVector SkinnedPosition(0, 0, 0);
        FVector SkinnedNormal(0, 0, 0);

        for (int j = 0; j < 4; j++)
        {
            float Weight = SrcVertex.BoneWeights[j];
            if (Weight <= 0.0001f)
                continue;

            UBone* Bone = SrcVertex.BonePointers[j];
            if (!Bone)
                continue;

            // 올바른 Skinning Matrix 계산 (Row-vector convention)
            // v' = v × InverseBindPoseMatrix × CurrentWorldMatrix
            FTransform WorldTransform = Bone->GetWorldTransform();
            FTransform WorldBindPose = Bone->GetWorldBindPose();

            FMatrix CurrentWorldMatrix = WorldTransform.ToMatrix();
            FMatrix InverseBindPoseMatrix = WorldBindPose.ToMatrix().InverseAffine();

            // Row-vector: 먼저 InvBindPose, 그 다음 CurrentWorld
            FMatrix SkinningMatrix = InverseBindPoseMatrix * CurrentWorldMatrix;

            // Position 변환
            FVector TransformedPos = SrcVertex.Position * SkinningMatrix;
            SkinnedPosition += TransformedPos * Weight;

            // Normal 변환 (Rotation만 적용)
            FMatrix RotationOnlyMatrix = SkinningMatrix;
            RotationOnlyMatrix.M[3][0] = 0.0f;
            RotationOnlyMatrix.M[3][1] = 0.0f;
            RotationOnlyMatrix.M[3][2] = 0.0f;

            FVector TransformedNormal = SrcVertex.Normal * RotationOnlyMatrix;
            SkinnedNormal += TransformedNormal * Weight;
        }

        DstVertex.Position = SkinnedPosition;
        DstVertex.Normal = SkinnedNormal.GetNormalized();
    }

    // 4. Dynamic Vertex Buffer 업데이트
    if (VertexBuffer && DeviceContext)
    {
        D3D11_MAPPED_SUBRESOURCE MappedResource;
        HRESULT hr = DeviceContext->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
        if (SUCCEEDED(hr))
        {
            memcpy(MappedResource.pData, TransformedVertices.data(), sizeof(FVertexDynamic) * VertexCount);
            DeviceContext->Unmap(VertexBuffer, 0);
        }
    }
}