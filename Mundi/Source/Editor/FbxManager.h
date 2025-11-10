#pragma once

#include <fbxsdk.h>
#include "Skeleton.h"
#include "SkeletalMeshStruct.h"

class USkeletalMesh;

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
private:
    // Helper functions for FBX parsing
    UBone* ProcessSkeletonNode(FbxNode* InNode, UBone* InParent = nullptr);
    void ProcessMeshNode(FbxNode* InNode, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractMeshData(FbxMesh* InMesh, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractSkinningData(FbxMesh* InMesh, FFlesh& OutFlesh, const TMap<FString, UBone*>& BoneMap);
    FTransform ConvertFbxTransform(const FbxAMatrix& InMatrix);

private:
    // SDK 관리자. 이 객체는 메모리 관리를 처리함.
    FbxManager* SdkManager;
    // IO 설정 객체.
    FbxIOSettings* ios;
    // SDK 관리자를 사용하여 Importer를 생성한다.
    FbxImporter* Importer;
    
    TMap<FString, FSkeletalMesh*> FbxSkeletalMeshMap;
};