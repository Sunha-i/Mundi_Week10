#pragma once

#include <fbxsdk.h>
#include "UEContainer.h"
#include "Enums.h"

class UBone;
struct FSkeletalMesh;
struct FStaticMesh;
struct FFlesh;

constexpr float FBXUnitScale = 0.01f;

class FFbxImporter
{
public:
    explicit FFbxImporter(FbxManager* InSdkManager) : SdkManager(InSdkManager) {}
    ~FFbxImporter() = default;
public:
    // Scene / materials
    FbxScene* ImportFbxScene(const FString& Path);
    void CollectMaterials(FbxScene* Scene, TMap<int64, FMaterialInfo>& OutMatMap, TArray<FMaterialInfo>& OutMaterialInfos, const FString& Path);

    // Skeleton helpers
    UBone* FindSkeletonRootAndBuild(FbxNode* RootNode);
    UBone* ProcessSkeletonNode(FbxNode* InNode, UBone* InParent = nullptr);
    FTransform ConvertFbxTransform(const FbxAMatrix& InMatrix);

    // Mesh extraction (skeletal)
    void ProcessMeshNode(FbxNode* InNode, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractMeshData(FbxMesh* InMesh, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractSkinningData(FbxMesh* InMesh, FFlesh& OutFlesh, const TMap<FString, UBone*>& BoneMap);

    // Mesh extraction (static)
    void ProcessMeshNodeAsStatic(FbxNode* InNode, FStaticMesh* OutStaticMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    void ExtractMeshDataAsStatic(FbxMesh* InMesh, FStaticMesh* OutStaticMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap);
    bool BuildStaticMeshFromPath(const FString& Path, FStaticMesh* OutStaticMesh, TArray<FMaterialInfo>& OutMaterialInfos);
private:
    FbxManager* SdkManager = nullptr;
};

