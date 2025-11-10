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
    CornerControlPointIndices.clear();
    CornerNormals.clear();
    CornerUVs.clear();
    TriangleCornerIndices.clear();

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

    // Vertices (control-point based)
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
        for (int s = 0; s < 4; ++s)
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

    // Corner-level data and triangles (to validate UV per-corner)
    out << "\n[Corners]\n";
    out << "CornerCount=" << CornerControlPointIndices.size() << "\n";
    out << "TriangleCount=" << (TriangleCornerIndices.size() / 3) << "\n";

    size_t triCount = TriangleCornerIndices.size() / 3;
    size_t dumpCount = std::min<size_t>(triCount, 200); // cap output
    size_t equalUVTris = 0;
    size_t unmappedCorners = 0;

    for (size_t t = 0; t < dumpCount; ++t)
    {
        uint32 c0 = TriangleCornerIndices[t * 3 + 0];
        uint32 c1 = TriangleCornerIndices[t * 3 + 1];
        uint32 c2 = TriangleCornerIndices[t * 3 + 2];

        const FVector2D& uv0 = CornerUVs[c0];
        const FVector2D& uv1 = CornerUVs[c1];
        const FVector2D& uv2 = CornerUVs[c2];

        auto isZero = [](const FVector2D& uv){ return std::fabs(uv.X) < 1e-6f && std::fabs(uv.Y) < 1e-6f; };
        if (isZero(uv0) || isZero(uv1) || isZero(uv2)) ++unmappedCorners;

        bool eq01 = (std::fabs(uv0.X - uv1.X) < 1e-6f) && (std::fabs(uv0.Y - uv1.Y) < 1e-6f);
        bool eq12 = (std::fabs(uv1.X - uv2.X) < 1e-6f) && (std::fabs(uv1.Y - uv2.Y) < 1e-6f);
        bool eq20 = (std::fabs(uv2.X - uv0.X) < 1e-6f) && (std::fabs(uv2.Y - uv0.Y) < 1e-6f);
        if (eq01 && eq12) ++equalUVTris;

        out << "Tri#" << t << ": ";
        out << "c0(cp=" << CornerControlPointIndices[c0] << ") uv=(" << uv0.X << ", " << uv0.Y << ")  ";
        out << "c1(cp=" << CornerControlPointIndices[c1] << ") uv=(" << uv1.X << ", " << uv1.Y << ")  ";
        out << "c2(cp=" << CornerControlPointIndices[c2] << ") uv=(" << uv2.X << ", " << uv2.Y << ")\n";
    }
    out << "Summary: equalUVTris(in first " << dumpCount << ")=" << equalUVTris
        << ", unmappedCornerCount=" << unmappedCorners << "\n";

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

    // 사용한 슬롯만 정규화
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
        // 4슬롯만 사용
    }
    UE_LOG("ProcessSkin 완료: Bones=%d, SkinnedVertices=%d", Bones.Num(), SkinnedVertices.Num());
}

// ============================================================================
// ProcessMesh
//  - FBX 메쉬 데이터를 읽어와 내부 포맷으로 변환
//  - 위치(Position), 노멀(Normal), UV 좌표를 추출하여 저장
// ============================================================================
void FFBXImporter::ProcessMesh(FbxMesh* Mesh)
{
    if (!Mesh)
        return;

    // ------------------------------------------------------------------------
    // 기본 메쉬 정보 추출
    // ------------------------------------------------------------------------
    const int ControlPointCount = Mesh->GetControlPointsCount();
    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
    const int PolygonCount = Mesh->GetPolygonCount();

    // 첫 번째 UVSet 이름 사용 (일반적으로 map1 또는 UVMap)
    const char* UvSetName = GetFirstUVSetName(Mesh);
    const bool HasUV = (UvSetName != nullptr);

    // 스킨 정점 배열 크기 보장
    if ((int)SkinnedVertices.size() < ControlPointCount)
        SkinnedVertices.resize(ControlPointCount);

    // ------------------------------------------------------------------------
    // 폴리곤 순회 (FBX는 n각형도 존재하지만 Triangulate() 이후라면 삼각형만 존재)
    // ------------------------------------------------------------------------
    for (int PolyIndex = 0; PolyIndex < PolygonCount; ++PolyIndex)
    {
        const int PolySize = Mesh->GetPolygonSize(PolyIndex);
        const int BaseCorner = (int)CornerControlPointIndices.size();

        // 각 폴리곤의 코너(vertex per polygon) 순회
        for (int Corner = 0; Corner < PolySize; ++Corner)
        {
            // ----------------------------------------------------------------
            // 1) 위치(Position)
            // ----------------------------------------------------------------
            const int ControlPointIndex = Mesh->GetPolygonVertex(PolyIndex, Corner);
            if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
                continue;

            const FbxVector4& P = ControlPoints[ControlPointIndex];
            SkinnedVertices[ControlPointIndex].pos = FVector(
                (float)P[0],
                (float)P[1],
                (float)P[2]);

            // ----------------------------------------------------------------
            // 2) 노멀(Normal)
            // ----------------------------------------------------------------
            FVector NormalOut(0, 0, 1);
            {
                FbxVector4 N(0, 0, 1, 0);
                if (Mesh->GetPolygonVertexNormal(PolyIndex, Corner, N))
                {
                    N.Normalize();
                    NormalOut = FVector((float)N[0], (float)N[1], (float)N[2]);
                }
            }

            // ----------------------------------------------------------------
            // 3) UV 좌표 (Texture Coordinate)
            // ----------------------------------------------------------------
            FVector2D UVOut(0.0f, 0.0f);

            if (HasUV)
            {
                FbxVector2 UV(0.0, 0.0);
                bool Unmapped = false;

                // (1) FBX SDK 헬퍼 함수 사용 — 가장 안전하고 일반적인 접근 방식
                if (Mesh->GetPolygonVertexUV(PolyIndex, Corner, UvSetName, UV, Unmapped) && !Unmapped)
                {
                    UVOut = FVector2D((float)UV[0], 1.0f - (float)UV[1]);
                }
                // (2) 폴백 — UV 요소를 직접 접근 (MappingMode / ReferenceMode 별 처리)
                else if (const FbxGeometryElementUV* UvElem = Mesh->GetElementUV(UvSetName))
                {
                    const auto MapMode = UvElem->GetMappingMode();
                    const auto RefMode = UvElem->GetReferenceMode();

                    // ControlPoint 기반 매핑
                    if (MapMode == FbxGeometryElement::eByControlPoint)
                    {
                        int Cp = ControlPointIndex;
                        int Idx = (RefMode == FbxGeometryElement::eIndexToDirect)
                            ? UvElem->GetIndexArray().GetAt(Cp)
                            : Cp;

                        if (Idx >= 0 && Idx < UvElem->GetDirectArray().GetCount())
                        {
                            FbxVector2 Uv = UvElem->GetDirectArray().GetAt(Idx);
                            UVOut = FVector2D((float)Uv[0], 1.0f - (float)Uv[1]);
                        }
                    }
                    // PolygonVertex 기반 매핑
                    else if (MapMode == FbxGeometryElement::eByPolygonVertex)
                    {
                        int VertexId = Mesh->GetTextureUVIndex(PolyIndex, Corner);
                        if (VertexId >= 0 && VertexId < UvElem->GetDirectArray().GetCount())
                        {
                            FbxVector2 Uv = UvElem->GetDirectArray().GetAt(VertexId);
                            UVOut = FVector2D((float)Uv[0], 1.0f - (float)Uv[1]);
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // 4) 추출된 데이터 저장
            // ----------------------------------------------------------------
            CornerControlPointIndices.push_back((uint32)ControlPointIndex);
            CornerNormals.push_back(NormalOut);
            CornerUVs.push_back(UVOut);
        }

        // --------------------------------------------------------------------
        // 삼각형 인덱스 추가 (Triangulate() 된 상태 기준)
        // --------------------------------------------------------------------
        if (PolySize == 3)
        {
            TriangleCornerIndices.push_back((uint32)BaseCorner + 0);
            TriangleCornerIndices.push_back((uint32)BaseCorner + 1);
            TriangleCornerIndices.push_back((uint32)BaseCorner + 2);
        }
    }

    // ------------------------------------------------------------------------
    // 처리 로그 출력
    // ------------------------------------------------------------------------
    UE_LOG("FFBXImporter - Mesh Processed | Polygons=%d | ControlPoints=%d | Corners=%d | UVSet=%s",
        PolygonCount, ControlPointCount, (int)CornerControlPointIndices.size(),
        UvSetName ? UvSetName : "None");
}
