#pragma once

#include <fbxsdk.h>
#include "Skeleton.h"
#include "SkeletalMeshStruct.h"

class USkeletalMesh;
class UStaticMesh;
struct FStaticMesh;

class FFbxManager
{
private:
    // Single Tone
    FFbxManager();
    ~FFbxManager();

    void Initialize();
    void Shutdown();
public:
    // Single Tone
    static FFbxManager& GetInstance();
public:
    void Preload();
    void Clear();
    FSkeletalMesh* LoadFbxSkeletalMeshAsset(const FString& PathFileName);
    USkeletalMesh* LoadFbxSkeletalMesh(const FString& PathFileName);

    // Skeleton이 없는 FBX를 StaticMesh로 처리
    FStaticMesh* LoadFbxStaticMeshAsset(const FString& PathFileName);
    UStaticMesh* LoadFbxStaticMesh(const FString& PathFileName);
private:
    // Helper functions for FBX parsing
    UBone* ProcessSkeletonNode(FbxNode* InNode, UBone* InParent = nullptr);
    void ProcessMeshNode(FbxNode* InNode, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractMeshData(FbxMesh* InMesh, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractSkinningData(FbxMesh* InMesh, FFlesh& OutFlesh, const TMap<FString, UBone*>& BoneMap);
    FTransform ConvertFbxTransform(const FbxAMatrix& InMatrix);

    // StaticMesh 처리용 헬퍼
    void ProcessMeshNodeAsStatic(FbxNode* InNode, FStaticMesh* OutStaticMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractMeshDataAsStatic(FbxMesh* InMesh, FStaticMesh* OutStaticMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);

private:
    // SDK 관리자. 이 객체는 메모리 관리를 처리함.
    FbxManager* SdkManager;
    // IO 설정 객체.
    FbxIOSettings* ios;
    // SDK 관리자를 사용하여 Importer를 생성한다.
    FbxImporter* Importer;

    TMap<FString, FSkeletalMesh*> FbxSkeletalMeshMap;
};