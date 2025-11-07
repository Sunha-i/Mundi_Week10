#pragma once

#include "FBXSDK.h"
#include "UEContainer.h"
#include "Vector.h"

class USkeletalMesh;

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
	TArray<FBone> Bones;
};

class FFBXImporter
{
public:
	static bool ImportFBX(
		const FString& FilePath, 
		USkeletalMesh* OutMesh,
		FFBXSkeletonData* OutSkeleton, 
		const FFBXImportOptions& Options = FFBXImportOptions()
	);

private:
	static void ParseMesh(FbxMesh* Mesh, FFBXMeshData& OutMeshData);
	static void ParseSkeleton(FbxNode* Root, FFBXSkeletonData& OutSkeleton);
};