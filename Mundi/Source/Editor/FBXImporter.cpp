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

    // 좌표축/단위계를 엔진 표준으로 변환 (DirectX 좌표, 미터)
    {
        const FbxAxisSystem desiredAxis = FbxAxisSystem::DirectX; // Left-handed, +Y up
        desiredAxis.ConvertScene(mScene);

        const FbxSystemUnit desiredUnit = FbxSystemUnit::m; // meters
        if (mScene->GetGlobalSettings().GetSystemUnit() != desiredUnit)
        {
            desiredUnit.ConvertScene(mScene);
        }
    }

    // 메시를 전부 삼각형화 (Quads → Triangles)
    {
        FbxGeometryConverter Converter(mManager);
        Converter.Triangulate(mScene, true);
    }

    // 이전 결과 초기화
    SkinnedVertices.clear();
    Bones.clear();

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

    UE_LOG("FFBXImporter - Loaded '%s' | VertexCount=%d", FilePath.c_str(), (int)SkinnedVertices.size());
    return true;
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

        // 스킨 메시라면 본 데이터도 추출
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


// LogSummary removed; use WriteDebugDump instead.

bool FFBXImporter::WriteDebugDump(const std::string& FilePath) const
{
    std::ofstream out(FilePath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
        return false;

    out.setf(std::ios::fixed, std::ios::floatfield);

    out << "FBXImporter Debug Dump\n";
    out << "SkinnedVertices=" << SkinnedVertices.size() << "\n";
    out << "Bones=" << Bones.Num() << "\n\n";

    // Bones
    out << "[Bones]\n";
    auto writeMat = [&](const Matrix4x4& M, const char* label)
    {
        out << label << "=" << "\n";
        for (int r = 0; r < 4; ++r)
        {
            out << "  "
                << M.m[r][0] << ' ' << M.m[r][1] << ' ' << M.m[r][2] << ' ' << M.m[r][3]
                << "\n";
        }
    };

    for (int i = 0; i < Bones.Num(); ++i)
    {
        const Bone& B = Bones[i];
        out << "- index=" << i << ", name=" << B.Name << ", parentIndex=" << B.ParentIndex << "\n";
        writeMat(B.BindPose,        "  BindPose");
        writeMat(B.InverseBindPose, "  InverseBindPose");
        writeMat(B.BoneTransform,   "  BoneTransform");
        writeMat(B.SkinningMatrix,  "  SkinningMatrix");
        out << "\n";
    }

    // Vertices
    out << "[Vertices]\n";
    for (size_t i = 0; i < SkinnedVertices.size(); ++i)
    {
        const FSkinnedVertex& v = SkinnedVertices[i];
        out << "- index=" << i
            << ", pos=(" << v.pos.X << ' ' << v.pos.Y << ' ' << v.pos.Z << ")"
            << ", normal=(" << v.normal.X << ' ' << v.normal.Y << ' ' << v.normal.Z << ")"
            << ", uv=(" << v.uv.X << ' ' << v.uv.Y << ")\n";

        out << "  influences=";
        bool first = true;
        float sum = 0.0f;
        for (int s = 0; s < 8; ++s)
        {
            if (v.boneWeights[s] > 0.0f)
            {
                if (!first) out << ", ";
                first = false;
                out << v.boneIndices[s] << ':' << v.boneWeights[s];
                sum += v.boneWeights[s];
            }
        }
        if (first) out << "(none)";
        out << ", sum=" << sum << "\n\n";
    }

    out.flush();
    return true;
}

void FFBXImporter::ProcessSkin(FbxMesh* Mesh)
{
    if (!Mesh) return;

    const int VertexCount = Mesh->GetControlPointsCount();
    if (VertexCount <= 0) return;
    SkinnedVertices.resize(VertexCount);

    const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
    if (SkinCount <= 0) return;

    FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
    const int ClusterCount = Skin->GetClusterCount();
    Bones.Reserve(ClusterCount);

    TArray<uint8> InfluenceCount;
    InfluenceCount.SetNum(VertexCount); // 0으로 초기화

    for (int ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
    {
        FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
        if (!Cluster) continue;

        FbxNode* BoneNode = Cluster->GetLink();
        if (!BoneNode) continue;

        // FBX 공간에서 계산
        FbxAMatrix MeshM;  Cluster->GetTransformMatrix(MeshM);
        FbxAMatrix BoneM;  Cluster->GetTransformLinkMatrix(BoneM);
        FbxAMatrix InvBindM = BoneM.Inverse() * MeshM; // OffsetMatrix
        const FbxAMatrix GlobalM = BoneNode->EvaluateGlobalTransform();
        const FbxAMatrix SkinningM = GlobalM * InvBindM;

        // 본 정보 제자리 생성 (복사/이동 제거)
        Bone& OutBone = Bones.emplace_back();
        OutBone.Name = BoneNode->GetName();
        OutBone.BindPose = ConvertMatrix(BoneM);
        OutBone.InverseBindPose = ConvertMatrix(InvBindM);
        OutBone.BoneTransform = ConvertMatrix(GlobalM);
        OutBone.SkinningMatrix = ConvertMatrix(SkinningM);

        // 본이 영향을 주는 정점에 상위 4개만 유지
        int* Indices = Cluster->GetControlPointIndices();
        double* Weights = Cluster->GetControlPointWeights();
        const int CPCount = Cluster->GetControlPointIndicesCount();

        for (int i = 0; i < CPCount; ++i)
        {
            const int v = Indices[i]; // 정점 인덱스 
            const float w = static_cast<float>(Weights[i]); // 가중치 
            // 잘못된 값이면 패스 
            if (v < 0 || v >= VertexCount || w <= 0.0f) continue;

            FSkinnedVertex& SkinnedVertex = SkinnedVertices[v];
            uint8& Count = InfluenceCount[v];
            constexpr int MaxInf = 4;

            if (Count < MaxInf)
            {
                SkinnedVertex.boneIndices[Count] = ClusterIndex;
                SkinnedVertex.boneWeights[Count] = w;
                ++Count;
            }
            else
            {
                int minIdx = 0; float minW = SkinnedVertex.boneWeights[0];
                for (int s = 1; s < MaxInf; ++s)
                {
                    if (SkinnedVertex.boneWeights[s] < minW) { minW = SkinnedVertex.boneWeights[s]; minIdx = s; }
                }
                if (w > minW)
                {
                    SkinnedVertex.boneIndices[minIdx] = ClusterIndex;
                    SkinnedVertex.boneWeights[minIdx] = w;
                }
            }
        }
    }

    // 사용한 슬롯만 정규화 + 나머지 0으로 정리
    for (int i = 0; i < (int)SkinnedVertices.size(); ++i)
    {
        FSkinnedVertex& V = SkinnedVertices[i];
        const int Used = std::min<int>(InfluenceCount[i], 4);

        float Sum = 0.0f; for (int s = 0; s < Used; ++s) Sum += V.boneWeights[s];
        if (Sum > 0.0f)
        {
            const float Inv = 1.0f / Sum;
            for (int s = 0; s < Used; ++s) V.boneWeights[s] *= Inv;
        }
        for (int s = Used; s < 8; ++s) { V.boneIndices[s] = 0; V.boneWeights[s] = 0.0f; }
    }

    UE_LOG("ProcessSkin 완료: Bones=%d, SkinnedVertices=%d", Bones.Num(), SkinnedVertices.Num());

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

    // 정점 데이터 (ControlPoint 기준으로 스킨 정점 채움)
    const int ControlPointCount = Mesh->GetControlPointsCount();
    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
    // 삼각형의 수 
    const int PolygonCount = Mesh->GetPolygonCount();
    // 여러 UV 세트(예: Lightmap UV, Diffuse UV)가 있을 수 있지만 보통 첫 번째 UVSet만 사용한다. 
    const char* UvSetName = GetFirstUVSetName(Mesh);
    const bool bHasUV = (UvSetName != nullptr);

    // 스킨 정점 배열을 ControlPoint 개수로 맞춰 둔다
    if ((int)SkinnedVertices.size() < ControlPointCount)
        SkinnedVertices.resize(ControlPointCount);

    // 현재 씬 단위계를 가져와서 포지션을 미터 단위로 정규화(안전장치)
    float unitToMeters = 1.0f;
    if (mScene)
    {
        const double scale = mScene->GetGlobalSettings().GetSystemUnit().GetScaleFactor();
        if (scale > 0.0)
            unitToMeters = static_cast<float>(1.0 / scale);
    }

    // 모든 폴리곤(삼각형)을 순회하며, 각 ControlPoint 인덱스에 위치/노멀/UV 기록
    for (int PolyIndex = 0; PolyIndex < PolygonCount; ++PolyIndex)
    {
        // 삼각형이면 PolySize 값은 3이다. 
        const int PolySize = Mesh->GetPolygonSize(PolyIndex);
        for (int Corner = 0; Corner < PolySize; ++Corner)
        {
            // 폴리곤 면이 참조하는 실제 정점 인덱스를 가져오는 핵심 코드
            const int ControlPointIndex = Mesh->GetPolygonVertex(PolyIndex, Corner);
            if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
            {
                continue;
            }

            FSkinnedVertex& VertexData = SkinnedVertices[ControlPointIndex];

            // Position
            const FbxVector4& P = ControlPoints[ControlPointIndex];
            VertexData.pos = FVector((float)P[0] * unitToMeters, (float)P[1] * unitToMeters, (float)P[2] * unitToMeters);

            // Normal (per-polygon vertex normal as a reasonable default)
            FbxVector4 N(0, 0, 1, 0);
            // 해당 위치에서의 노말값 가져오기 
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
                    VertexData.uv = FVector2D((float)UV[0], (float)UV[1]);
                else
                    VertexData.uv = FVector2D(0.0f, 0.0f);
            }
            else
            {
                VertexData.uv = FVector2D(0.0f, 0.0f);
            }

            // 스킨용 기본값 초기화 (나중에 ProcessSkin에서 덮어씀)
            for (int slot = 0; slot < 8; ++slot)
            {
                VertexData.boneIndices[slot] = 0;
                VertexData.boneWeights[slot] = 0.0f;
            }
        }
    }

    UE_LOG("FFBXImporter - Mesh processed: Polygons=%d, ControlPoints=%d, SkinnedVerts=%d",
        PolygonCount, ControlPointCount, (int)SkinnedVertices.size());
}
