#pragma once

#include <fbxsdk.h>
#include "Skeleton.h"
#include "SkeletalMeshStruct.h"

class FFbxImporter;
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

    // FBX Scene 관련
    bool ValidateFbxFile(const FString& Path);
    //FbxScene* ImportFbxScene(const FString& Path);

    FSkeletalMesh* LoadFbxSkeletalMeshAsset(const FString& PathFileName);
    // Material 관련
    //void CollectMaterials(FbxScene* Scene, TMap<int64, FMaterialInfo>& OutMatMap, TArray<FMaterialInfo>& OutMaterialInfos, const FString& Path);

    // Mesh 빌드 관련
    FSkeletalMesh* BuildSkeletalMesh(FbxScene* Scene, FbxNode* RootNode, UBone* RootBone, const TMap<int64, FMaterialInfo>& MaterialMap, const FString& Path);
    void BuildStaticMeshFromScene(FbxScene* Scene, const TMap<int64, FMaterialInfo>& MaterialMap, const TArray<FMaterialInfo>& MaterialInfos, const FString& Path);

    USkeletalMesh* LoadFbxSkeletalMesh(const FString& PathFileName);

    // Skeleton이 없는 FBX를 StaticMesh로 처리
    FStaticMesh* LoadFbxStaticMeshAsset(const FString& PathFileName);
private:
    bool BuildStaticMeshFromFbx(const FString& NormalizedPathStr, FStaticMesh* OutStaticMesh, TArray<FMaterialInfo>& OutMaterialInfos);
    // Helper functions for FBX parsing
    //FTransform ConvertFbxTransform(const FbxAMatrix& InMatrix);

private:
    // SDK 관리자. 이 객체는 메모리 관리를 처리함.
    FbxManager* SdkManager;
    // IO 설정 객체.
    FbxIOSettings* ios;
    // SDK 관리자를 사용하여 Importer를 생성한다.
    
    class FFbxImporter* ImporterUtil;

    TMap<FString, FSkeletalMesh*> FbxSkeletalMeshMap;
};

