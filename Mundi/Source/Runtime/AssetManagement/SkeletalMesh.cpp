#include "pch.h"
#include "SkeletalMesh.h"

#include "FbxManager.h"

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::~USkeletalMesh()
{
    if (SkeletalMeshAsset)
         delete SkeletalMeshAsset;
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
        CreateVertexBuffer(SkeletalMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(SkeletalMeshAsset, InDevice);
        CreateLocalBound(SkeletalMeshAsset);
        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());
    }
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

    CreateVertexBuffer(InData, InDevice, InVertexType);
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

    // FSkeletalMesh 깊은 복사 (복사 생성자가 Skeleton과 Fleshes를 모두 복사함)
    if (SkeletalMeshAsset)
    {
        // ✅ 복사 생성자 사용: Skeleton 복사 + Flesh.Bones 재매핑
        FSkeletalMesh* OriginalAsset = SkeletalMeshAsset;
        SkeletalMeshAsset = new FSkeletalMesh(*OriginalAsset);

        // GPU 버퍼 재생성
        ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
        CreateVertexBuffer(SkeletalMeshAsset, Device, GetVertexType());
        CreateIndexBuffer(SkeletalMeshAsset, Device);
        CreateLocalBound(SkeletalMeshAsset);

        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());
        CacheFilePath = SkeletalMeshAsset->CacheFilePath;
    }
}