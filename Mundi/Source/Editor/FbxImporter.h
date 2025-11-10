#pragma once

#include <fbxsdk.h>
#include "UEContainer.h"
#include "Enums.h"

class UBone;
struct FSkeletalMesh;
struct FStaticMesh;
struct FFlesh;

// FBX 스케일 통합 (cm 기준)
constexpr float FBXUnitScale = 0.01f;

class FFbxImporter
{
public:
	explicit FFbxImporter(FbxManager* InSdkManager) : SdkManager(InSdkManager) {}
	~FFbxImporter() = default;

public:
	// =======================
	//  FBX Scene 로드
	// =======================
	FbxScene* ImportFbxScene(const FString& Path);

	// =======================
	//  Material 파싱
	// =======================
	void ParseFbxMaterials(
		FbxScene* Scene,
		const FString& Path,
		TMap<int64, FMaterialInfo>& OutMatMap,
		TArray<FMaterialInfo>& OutInfos
	);

	// =======================
	//  Skeleton 처리
	// =======================
	UBone* FindSkeletonRootAndBuild(FbxNode* RootNode);
	UBone* ProcessSkeletonNode(FbxNode* InNode, UBone* InParent = nullptr);
	FTransform ConvertFbxTransform(const FbxAMatrix& InMatrix);

	// =======================
	//  Skeletal Mesh
	// =======================
	void ProcessMeshNode(
		FbxNode* InNode,
		FSkeletalMesh* OutSkeletalMesh,
		const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap
	);
	void ExtractSkinningData(
		FbxMesh* InMesh,
		FFlesh& OutFlesh,
		const TMap<FString, UBone*>& BoneMap
	);

	// =======================
	//  Static Mesh
	// =======================
	void ProcessMeshNodeAsStatic(
		FbxNode* InNode,
		FStaticMesh* OutStaticMesh,
		const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap
	);
	bool BuildStaticMeshFromPath(
		const FString& Path,
		FStaticMesh* OutStaticMesh,
		TArray<FMaterialInfo>& OutMaterialInfos
	);

private:
	FbxManager* SdkManager = nullptr;
};
