#pragma once
#include <fbxsdk.h>
#include "UEContainer.h"
#include "Vector.h"
#include "Enums.h"

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

public:
    TArray<FSkinnedVertex> SkinnedVertices;
    TArray<Bone> Bones;

    void ProcessSkin(FbxMesh* Mesh);


private:
    // 노드(트리) 순회: Mesh 노드를 찾으면 ProcessMesh 호출
    void ProcessNode(FbxNode* Node);

    // 메시 정점/노멀/UV 데이터를 추출하는 함수
    void ProcessMesh(FbxMesh* Mesh);

    // 첫 번째 UVSet 이름 반환 (없으면 nullptr)
    static const char* GetFirstUVSetName(FbxMesh* Mesh);

private:
    FbxManager* mManager = nullptr;  // FBX SDK Manager
    FbxScene* mScene = nullptr;  // FBX Scene 객체

    // 읽어온 정점 데이터 (임시 캐싱용)
    TArray<FNormalVertex> Vertices;
};
