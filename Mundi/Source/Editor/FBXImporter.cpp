#include "pch.h"
#include "FBXImporter.h"
#include "GlobalConsole.h"

// ============================================================================
// [Helper] UVSet 이름 반환
// ----------------------------------------------------------------------------
// FBX 메시에 여러 UVSet이 존재할 수 있는데, 보통 첫 번째 것을 사용한다.
// ============================================================================
const char* FFBXImporter::GetFirstUVSetName(FbxMesh* Mesh)
{
    static FbxStringList UvSetNames;
    UvSetNames.Clear();

    if (!Mesh)
        return nullptr;

    Mesh->GetUVSetNames(UvSetNames);
    return (UvSetNames.GetCount() > 0) ? UvSetNames[0] : nullptr;
}

// ============================================================================
// 생성자 / 소멸자
// ============================================================================
FFBXImporter::FFBXImporter() = default;
FFBXImporter::~FFBXImporter()
{
    if (mScene)
    {
        mScene->Destroy();
        mScene = nullptr;
    }

    if (mManager)
    {
        mManager->Destroy();
        mManager = nullptr;
    }
}


// ============================================================================
// LoadFBX()
// ----------------------------------------------------------------------------
//  FBX 파일을 열고 Scene 데이터를 파싱한다.
//  - FBX Manager / Scene 초기화
//  - 파일 Import
//  - Triangulate(삼각형화) 처리
//  - Root Node부터 순회 시작
// ============================================================================

Matrix4x4 FFBXImporter::ConvertMatrix(const FbxAMatrix& Src)
{
    Matrix4x4 Out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            Out.m[r][c] = static_cast<float>(Src.Get(r, c));
    return Out;
}

bool FFBXImporter::LoadFBX(const std::string& FilePath)
{
    if (FilePath.empty())
    {
        UE_LOG("FFBXImporter::LoadFBX - Empty file path");
        return false;
    }

    // FBX Manager 생성
    if (mManager == nullptr)
    {
        mManager = FbxManager::Create();
        if (!mManager)
        {
            UE_LOG("FFBXImporter - Failed to create FbxManager");
            return false;
        }

        FbxIOSettings* IoSettings = FbxIOSettings::Create(mManager, IOSROOT);
        mManager->SetIOSettings(IoSettings);
    }

    // Scene 생성 (이전 Scene 있으면 삭제)
    if (mScene)
    {
        mScene->Destroy();
        mScene = nullptr;
    }

    mScene = FbxScene::Create(mManager, "MundiFBXScene");
    if (!mScene)
    {
        UE_LOG("FFBXImporter - Failed to create FbxScene");
        return false;
    }

    // Importer 생성
    FbxImporter* Importer = FbxImporter::Create(mManager, "");
    if (!Importer)
    {
        UE_LOG("FFBXImporter - Failed to create FbxImporter");
        return false;
    }

    // 파일 초기화
    if (!Importer->Initialize(FilePath.c_str(), -1, mManager->GetIOSettings()))
    {
        UE_LOG("FFBXImporter - Initialize failed: %s", Importer->GetStatus().GetErrorString());
        Importer->Destroy();
        return false;
    }

    // Scene으로 Import
    if (!Importer->Import(mScene))
    {
        UE_LOG("FFBXImporter - Import failed: %s", Importer->GetStatus().GetErrorString());
        Importer->Destroy();
        return false;
    }

    // 메시를 전부 삼각형화 (Quads → Triangles)
    {
        FbxGeometryConverter Converter(mManager);
        Converter.Triangulate(mScene, true);
    }

    // 이전 결과 초기화
    Vertices.Empty();

    // Root Node부터 순회 시작
    if (FbxNode* Root = mScene->GetRootNode())
    {
        ProcessNode(Root);
    }
    else
    {
        UE_LOG("FFBXImporter - No root node in scene");
    }

    Importer->Destroy();

    UE_LOG("FFBXImporter - Loaded '%s' | VertexCount=%d", FilePath.c_str(), Vertices.Num());
    return true;
}

void FFBXImporter::ProcessSkin(FbxMesh* Mesh)
{
    if (!Mesh)
        return;

    // ControlPoint(=Vertex) 개수 확인
    const int VertexCount = Mesh->GetControlPointsCount();
    if (VertexCount <= 0)
        return;

    SkinnedVertices.resize(VertexCount);

    // 스킨 디포머 개수 확인
    const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
    if (SkinCount <= 0)
        return;

    // 일반적으로 하나의 스킨만 존재
    FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
    const int ClusterCount = Skin->GetClusterCount();
    Bones.reserve(ClusterCount);

    // ================================================================
    // ① 각 클러스터(=본) 순회하며 Bone 정보 생성
    // ================================================================
    for (int ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
    {
        FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
        if (!Cluster)
            continue;

        FbxNode* BoneNode = Cluster->GetLink();
        if (!BoneNode)
            continue;

        Bone BoneData{};
        BoneData.Name = BoneNode->GetName();

        // 메시 기준 Transform
        FbxAMatrix MeshTransformMatrix;
        Cluster->GetTransformMatrix(MeshTransformMatrix);

        // 본 기준 Transform
        FbxAMatrix BoneTransformMatrix;
        Cluster->GetTransformLinkMatrix(BoneTransformMatrix);

        // InverseBindPose 계산
        FbxAMatrix BoneTransformMatrixInv = BoneTransformMatrix.Inverse();
        FbxAMatrix OffsetMatrix = BoneTransformMatrixInv * MeshTransformMatrix;

        // FBX → Matrix4x4 변환 (람다 X)
        Matrix4x4 BindPoseMat{};
        Matrix4x4 InverseBindPoseMat{};
        Matrix4x4 BoneTransformMat{};

        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Col = 0; Col < 4; ++Col)
            {
                BindPoseMat.m[Row][Col] = static_cast<float>(BoneTransformMatrix.Get(Row, Col));
                InverseBindPoseMat.m[Row][Col] = static_cast<float>(OffsetMatrix.Get(Row, Col));
            }
        }

        // 현재 프레임의 글로벌 본 행렬 계산
        FbxAMatrix GlobalTransform = BoneNode->EvaluateGlobalTransform();
        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Col = 0; Col < 4; ++Col)
            {
                BoneTransformMat.m[Row][Col] = static_cast<float>(GlobalTransform.Get(Row, Col));
            }
        }

        BoneData.BindPose = BindPoseMat;
        BoneData.InverseBindPose = InverseBindPoseMat;
        BoneData.BoneTransform = BoneTransformMat;

        // 스키닝 행렬 = BoneTransform × InverseBindPose
        Matrix4x4 SkinningMat{};
        for (int Row = 0; Row < 4; ++Row)
        {
            for (int Col = 0; Col < 4; ++Col)
            {
                float Sum = 0.0f;
                for (int K = 0; K < 4; ++K)
                    Sum += BoneData.BoneTransform.m[Row][K] * BoneData.InverseBindPose.m[K][Col];

                SkinningMat.m[Row][Col] = Sum;
            }
        }
        BoneData.SkinningMatrix = SkinningMat;

        Bones.push_back(BoneData);

        // ================================================================
        // ② 각 Bone이 영향을 주는 정점에 인덱스/가중치 등록
        // ================================================================
        int* Indices = Cluster->GetControlPointIndices();
        double* Weights = Cluster->GetControlPointWeights();
        int ControlPointCount = Cluster->GetControlPointIndicesCount();

        for (int i = 0; i < ControlPointCount; ++i)
        {
            int VertexIndex = Indices[i];
            float Weight = static_cast<float>(Weights[i]);

            if (VertexIndex < 0 || VertexIndex >= VertexCount)
                continue;

            FSkinnedVertex& Vertex = SkinnedVertices[VertexIndex];

            // boneIndices[4], boneWeights[4] 중 비어 있는 슬롯에 채움
            for (int Slot = 0; Slot < 8; ++Slot)
            {
                if (Vertex.boneWeights[Slot] == 0.0f)
                {
                    Vertex.boneIndices[Slot] = ClusterIndex;
                    Vertex.boneWeights[Slot] = Weight;
                    break;
                }
            }
        }
    }

    // ================================================================
    // ③ 가중치 정규화 (합 = 1.0)
    // ================================================================
    for (size_t i = 0; i < SkinnedVertices.size(); ++i)
    {
        FSkinnedVertex& V = SkinnedVertices[i];
        float Sum = V.boneWeights[0] + V.boneWeights[1] + V.boneWeights[2] + V.boneWeights[3];
        if (Sum > 0.0f)
        {
            for (int Slot = 0; Slot <8; ++Slot)
                V.boneWeights[Slot] /= Sum;
        }
    }

    UE_LOG("ProcessSkin 완료: Bones=%d, Vertices=%d", Bones.Num(), SkinnedVertices.Num());
}


// ============================================================================
// ProcessNode()
// ----------------------------------------------------------------------------
//  씬 그래프를 재귀적으로 탐색하면서, Mesh 노드를 발견하면 ProcessMesh 호출
// ============================================================================
void FFBXImporter::ProcessNode(FbxNode* Node)
{
    if (!Node)
        return;

    if (FbxMesh* Mesh = Node->GetMesh())
    {
        // 메시 처리
        ProcessMesh(Mesh);

        // 💡 스킨 메시라면 본 데이터도 추출
        if (Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0)
        {
            UE_LOG("FFBXImporter - Skinned Mesh detected, extracting skin data...");
            ProcessSkin(Mesh);
        }
    }

    // 자식 노드 재귀 탐색
    const int32 ChildCount = Node->GetChildCount();
    for (int32 i = 0; i < ChildCount; ++i)
    {
        ProcessNode(Node->GetChild(i));
    }
}


// ============================================================================
// ProcessMesh()
// ----------------------------------------------------------------------------
//  FbxMesh 객체에서 정점(Position), 노멀(Normal), UV 정보를 추출한다.
//  - 모든 폴리곤을 순회하며, 각 버텍스를 변환
// ============================================================================
void FFBXImporter::ProcessMesh(FbxMesh* Mesh)
{
    if (!Mesh)
        return;

    // 정점 데이터 
    const int ControlPointCount = Mesh->GetControlPointsCount();
    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
    // 삼각형 면의 정보 
    const int PolygonCount = Mesh->GetPolygonCount();

    const char* UvSetName = GetFirstUVSetName(Mesh);
    const bool bHasUV = (UvSetName != nullptr);

    // 모든 폴리곤(삼각형)을 순회
    for (int PolyIndex = 0; PolyIndex < PolygonCount; ++PolyIndex)
    {
        const int PolySize = Mesh->GetPolygonSize(PolyIndex);
        for (int Corner = 0; Corner < PolySize; ++Corner)
        {
            const int ControlPointIndex = Mesh->GetPolygonVertex(PolyIndex, Corner);
            if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
                continue;

            FNormalVertex VertexData{};

            // Position
            const FbxVector4& P = ControlPoints[ControlPointIndex];
            VertexData.pos = FVector((float)P[0], (float)P[1], (float)P[2]);

            // Normal
            FbxVector4 N(0, 0, 1, 0);
            if (Mesh->GetPolygonVertexNormal(PolyIndex, Corner, N))
            {
                const double len = N.Length();
                if (len > 1e-6)
                {
                    N[0] /= len;
                    N[1] /= len;
                    N[2] /= len;
                }
                VertexData.normal = FVector((float)N[0], (float)N[1], (float)N[2]);
            }
            else
            {
                VertexData.normal = FVector(0, 0, 1);
            }

            // UV (첫 번째 UVSet 사용)
            if (bHasUV)
            {
                FbxVector2 UV(0.0, 0.0);
                bool bUnmapped = false;
                if (Mesh->GetPolygonVertexUV(PolyIndex, Corner, UvSetName, UV, bUnmapped))
                    VertexData.tex = FVector2D((float)UV[0], (float)UV[1]);
                else
                    VertexData.tex = FVector2D(0.0f, 0.0f);
            }
            else
            {
                VertexData.tex = FVector2D(0.0f, 0.0f);
            }

            // 기본값 (색상/탄젠트)
            VertexData.Tangent = FVector4(0, 0, 0, 1);
            VertexData.color = FVector4(1, 1, 1, 1);

            // 결과 저장
            Vertices.Add(VertexData);
        }
    }

    UE_LOG("FFBXImporter - Mesh processed: Polygons=%d, ControlPoints=%d, TotalVerts=%d",
        PolygonCount, ControlPointCount, Vertices.Num());
}
