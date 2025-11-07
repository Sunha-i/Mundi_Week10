#include "pch.h"
#include "FBXImporter.h"

bool FFBXImporter::ImportFBX(const FString& FilePath, USkeletalMesh* OutMesh, FFBXSkeletonData* OutSkeleton, const FFBXImportOptions& Options)
{
	return false;
}

void FFBXImporter::ParseMesh(FbxMesh* Mesh, FFBXMeshData& OutMeshData)
{
}

void FFBXImporter::ParseSkeleton(FbxNode* Root, FFBXSkeletonData& OutSkeleton)
{
}
