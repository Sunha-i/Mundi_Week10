#pragma once

#include <fbxsdk.h>
#include "UEContainer.h"
#include "Enums.h"

class UBone;
struct FSkeletalMesh;
struct FStaticMesh;
struct FFlesh;

// FBX 스케일 통합 (cm 기준)
//constexpr float FBXUnitScale = 0.01f;
// ============================================================================
// [FBX 유틸리티] 공통 변환 / 단위 스케일
// ============================================================================
namespace FBXUtil
{
	constexpr float UnitScale = 0.01f;

	inline FVector ConvertPosition(const FbxVector4& Pos)
	{
		// FBX는 cm 단위, 엔진은 m 단위 -> 0.01 스케일
		return FVector(
			static_cast<float>(Pos[0]) ,
			static_cast<float>(Pos[1]),
			static_cast<float>(Pos[2]) )  * UnitScale;
	}

	inline FVector ConvertNormal(const FbxVector4& Normal)
	{
		return FVector(
			static_cast<float>(Normal[0]),
			static_cast<float>(Normal[1]),
			static_cast<float>(Normal[2]));
	}

	inline FVector4 DefaultTangent()
	{
		return FVector4(1.0f, 0.0f, 0.0f, 1.0f);
	}
}

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
	void CollectMaterials(
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
	FMatrix ConvertFbxMatrix(const FbxAMatrix& InMatrix);

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

	// =======================
	//  Debug
	// =======================
	void DumpSkeletalMeshInfo(const FSkeletalMesh* SkeletalMesh, const FString& OutputPath);

private:
	FbxManager* SdkManager = nullptr;
};

