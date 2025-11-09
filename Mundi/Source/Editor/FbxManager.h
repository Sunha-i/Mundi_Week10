#pragma once

#include "FBXSDK.h"
#include "UEContainer.h"
#include "Vector.h"

class USkeletalMesh;
class FSkeletalMesh;

struct FFBXImportOptions
{
	bool bImportAnimation = false;
	bool bImportNormals = false;
	bool bConvertToRightHanded = true;
};

struct FFBXMeshData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> TexCoords;

	TArray<int> BoneIndices;
	TArray<float> BoneWeights;
	TArray<uint32> Indices;
	TArray<FGroupInfo> GroupInfos;
};

struct FFBXSkeletonData
{
	struct FBone
	{
		FString Name;
		int32 ParentIndex;
		FMatrix BindPose;
		FMatrix InverseBindPose;
	};
	TArray<FBoneInfo> Bones;
};

class FFBXImporter
{
public:
	static bool ImportFBX(
		const FString& FilePath, 
		FSkeletalMesh* OutMesh,
		FFBXSkeletonData* OutSkeleton, 
		TArray<FMaterialInfo>& OutMaterialInfos,
		const FFBXImportOptions& Options = FFBXImportOptions()
	);

private:
	static void ParseSkeleton(FbxNode* Root, FFBXSkeletonData& OutSkeleton);
	static void ParseMesh(FbxNode* InNode, FbxMesh* Mesh, FFBXMeshData& OutMeshData);
};

class FFbxManager
{
public:
	static void Preload();
	static void Clear();
	static FSkeletalMesh* LoadFbxSkeletalMeshAsset(const FString& PathFileName);
	static USkeletalMesh* LoadFbxSkeletalMesh(const FString& PathFileName);

private:
	static TMap<FString, FSkeletalMesh*> CachedAssets;
	static TMap<FString, USkeletalMesh*> CachedResources;
	static bool IsSkeletalMesh(FbxMesh* Mesh);
};