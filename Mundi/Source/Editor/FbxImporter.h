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
// ============================================================================
// [FBX 유틸리티] 공통 변환 / 단위 스케일
// ============================================================================
namespace FBXUtil
{
	constexpr float UnitScale = 0.01f;

	inline FVector RotateForwardFix(const FVector& In)
	{
		// 1) Rotate +X -> +Z (90° about +Y)
		// 2) Rotate 90° about +X so meshes face +Z while staying Z-up
		return FVector( // 축 교환 . ! 
			In.Z,   // X'
			In.X,   // Y'
			In.Y    // Z'
		);
	}

	inline FVector ConvertPosition(const FbxVector4& Pos)
	{
		FVector Converted(
			static_cast<float>(Pos[0]),
			static_cast<float>(Pos[1]),
			static_cast<float>(Pos[2]));
		return RotateForwardFix(Converted * UnitScale);
	}

	inline FVector ConvertNormal(const FbxVector4& Normal)
	{
		return RotateForwardFix(FVector(
			static_cast<float>(Normal[0]),
			static_cast<float>(Normal[1]),
			static_cast<float>(Normal[2])));
	}

	inline FQuat ConvertRotation(const FbxQuaternion& Rotation)
	{
		FQuat Result(
			static_cast<float>(Rotation[0]),
			static_cast<float>(Rotation[1]),
			static_cast<float>(Rotation[2]),
			static_cast<float>(Rotation[3]));

		constexpr float HalfSqrt2 = 0.70710678118f;
		const FQuat AdjustY(0.0f, HalfSqrt2, 0.0f, HalfSqrt2);               // +90° about +Y
		const FQuat AdjustX(HalfSqrt2, 0.0f, 0.0f, HalfSqrt2);               // +90° about +X
		const FQuat Adjust = AdjustX * AdjustY;

		Result = Adjust * Result;
		Result.Normalize();
		return Result;
	}

	inline FVector4 DefaultTangent()
	{
		const FVector Rotated = RotateForwardFix(FVector(1.0f, 0.0f, 0.0f));
		return FVector4(Rotated.X, Rotated.Y, Rotated.Z, 1.0f);
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
