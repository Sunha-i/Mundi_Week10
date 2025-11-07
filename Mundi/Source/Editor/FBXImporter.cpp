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
FFBXImporter::~FFBXImporter() = default;

// ============================================================================
// LoadFBX()
// ----------------------------------------------------------------------------
//  FBX 파일을 열고 Scene 데이터를 파싱한다.
//  - FBX Manager / Scene 초기화
//  - 파일 Import
//  - Triangulate(삼각형화) 처리
//  - Root Node부터 순회 시작
// ============================================================================
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

// ============================================================================
// ProcessNode()
// ----------------------------------------------------------------------------
//  씬 그래프를 재귀적으로 탐색하면서, Mesh 노드를 발견하면 ProcessMesh 호출
// ============================================================================
void FFBXImporter::ProcessNode(FbxNode* Node)
{
    if (!Node)
        return;

    // 메시 노드일 경우 처리
    if (FbxMesh* Mesh = Node->GetMesh())
        ProcessMesh(Mesh);

    // 자식 노드 재귀 탐색
    const int32 ChildCount = Node->GetChildCount();
    for (int32 i = 0; i < ChildCount; ++i)
        ProcessNode(Node->GetChild(i));
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

    const int ControlPointCount = Mesh->GetControlPointsCount();
    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
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
