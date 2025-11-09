#include "pch.h"
#include "SkeletalMesh.h"
#include "FbxManager.h"

IMPLEMENT_CLASS(USkeletalMesh)

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    SkeletalMeshAsset = FFbxManager::LoadFbxSkeletalMeshAsset(InFilePath);

    if (SkeletalMeshAsset && 0 < SkeletalMeshAsset->Vertices.size() && 0 < SkeletalMeshAsset->Indices.size())
    {
        CacheFilePath = SkeletalMeshAsset->CacheFilePath;
        SkeletalMeshAsset->UpdateSkinnedVertices();
        CreateVertexBuffer(SkeletalMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(SkeletalMeshAsset, InDevice);
        VertexCount = static_cast<uint32>(SkeletalMeshAsset->SkinnedVertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());
        VertexStride = sizeof(FSkinnedVertex);
    }
}

void USkeletalMesh::CreateVertexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    const auto& SrcVertices = InSkeletalMesh->SkinnedVertices.empty()
        ? InSkeletalMesh->Vertices : InSkeletalMesh->SkinnedVertices;

    HRESULT hr = D3D11RHI::CreateVertexBuffer<FVertexSkinned>(InDevice, SrcVertices, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InSkeletalMesh, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

inline FVector TransformPosition(const FMatrix& M, const FVector& P)
{
    return FVector(
        M.M[0][0] * P.X + M.M[1][0] * P.Y + M.M[2][0] * P.Z + M.M[3][0],
        M.M[0][1] * P.X + M.M[1][1] * P.Y + M.M[2][1] * P.Z + M.M[3][1],
        M.M[0][2] * P.X + M.M[1][2] * P.Y + M.M[2][2] * P.Z + M.M[3][2]
    );
}

inline FVector TransformVector(const FMatrix& M, const FVector& V)
{
    return FVector(
        M.M[0][0] * V.X + M.M[1][0] * V.Y + M.M[2][0] * V.Z,
        M.M[0][1] * V.X + M.M[1][1] * V.Y + M.M[2][1] * V.Z,
        M.M[0][2] * V.X + M.M[1][2] * V.Y + M.M[2][2] * V.Z
    );
}

void FSkeletalMesh::UpdateSkinnedVertices()
{
    if (Bones.empty() || Vertices.empty())
        return;

    const size_t BoneCount = Bones.size();
    std::vector<FMatrix> SkinMatrices(BoneCount);

    for (size_t i = 0; i < BoneCount; ++i)
    {
        const FBoneInfo& B = Bones[i];

        // ParseSkeleton 에서 BindPose를 이미 Global 로 넣었다면:
        // 현재 애니메이션 없음 → M_current = BindPose
        // SkinMatrix = M_current * InvBindPose = BindPose * InvBindPose = Identity
        // => 결과적으로 Vertices 그대로 (망가지지 않음)
        SkinMatrices[i] = B.BindPose * B.InverseBindpose;
    }

    SkinnedVertices.clear();
    SkinnedVertices.reserve(Vertices.size());

    for (const FSkinnedVertex& Src : Vertices)
    {
        FVector Pos(0, 0, 0);
        FVector Nor(0, 0, 0);

        for (int i = 0; i < 4; ++i)
        {
            const int BoneIdx = Src.BoneIndices[i];
            const float W = Src.BoneWeights[i];
            if (BoneIdx < 0 || BoneIdx >= (int)BoneCount || W <= 0.0f)
                continue;

            const FMatrix& M = SkinMatrices[BoneIdx];

            FVector TP = Src.Position * M; // 엔진에서 이미 쓰던 방식 유지
            FVector4 N4 = FVector4::FromDirection(Src.Normal) * M;
            FVector TN(N4.X, N4.Y, N4.Z);

            Pos += TP * W;
            Nor += TN * W;
        }

        FSkinnedVertex Out = Src;
        Out.Position = Pos;
        Out.Normal = Nor.IsZero() ? Src.Normal : Nor.GetSafeNormal();
        SkinnedVertices.push_back(Out);
    }
}