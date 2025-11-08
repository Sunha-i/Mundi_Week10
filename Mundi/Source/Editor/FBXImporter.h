#pragma once
#include <fbxsdk.h>
#include "UEContainer.h"
#include "Vector.h"
#include "Enums.h"


// ============================================================================
// [Helper] FBX 정규화 함수
// ============================================================================
static int QuantizeFloat(float Value)
{
    return static_cast<int>(std::round(Value * 10000.0f));
}

static uint8 QuantizeWeight(float Weight)
{
    float Clamped = std::clamp(Weight, 0.0f, 1.0f);
    return static_cast<uint8>(std::round(Clamped * 255.0f));
}

// ============================================================================
// FFBXImporter
// ----------------------------------------------------------------------------
//  FBX 파일을 불러와서 정점(Position, Normal, UV)을 추출하는 Static Mesh Importer
//  FBX SDK를 이용해 FBX Scene을 순회하고 Mesh 데이터를 변환한다.
// ============================================================================
class FFBXImporter
{
public:
    FFBXImporter();
    ~FFBXImporter();

    Matrix4x4 ConvertMatrix(const FbxAMatrix& Src);

    // FBX 파일 로드 및 정점 데이터 추출
    bool LoadFBX(const std::string& FilePath);

    // 텍스트 덤프를 파일로 기록
    bool WriteDebugDump(const std::string& FilePath) const;

public:
    TArray<FSkinnedVertex> SkinnedVertices;
    TArray<Bone> Bones;
    // Corner-level raw data (for proper cooking)
    TArray<uint32> CornerControlPointIndices; // per corner: control point index
    TArray<FVector> CornerNormals;            // per corner normal
    TArray<FVector2D> CornerUVs;              // per corner uv
    TArray<uint32> TriangleCornerIndices;     // triangles as indices into corner arrays
    TArray<int> TriangleMaterialIndex;        // per triangle material slot index

    void ProcessSkin(FbxMesh* Mesh);


private:
    // 노드(트리) 순회: Mesh 노드를 찾으면 ProcessMesh 호출
    void ProcessNode(FbxNode* Node);

    // 메시 정점/노멀/UV 데이터를 추출하는 함수
    void ProcessMesh(FbxMesh* Mesh);

    // 첫 번째 UVSet 이름 반환 (없으면 nullptr)
    static const char* GetFirstUVSetName(FbxMesh* Mesh);
    static const char* SelectBestUVSetName(FbxMesh* Mesh);

private:
    FbxManager* mManager = nullptr;  // FBX SDK Manager
    FbxScene* mScene = nullptr;  // FBX Scene 객체
};

