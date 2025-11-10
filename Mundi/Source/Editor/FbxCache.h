#pragma once

#include "UEContainer.h"
#include "Enums.h"

struct FStaticMesh;
struct FSkeletalMesh;

struct FStaticCachePaths
{
	FString MeshBinPath;
	FString MaterialBinPath;
};

struct FSkeletalCachePaths
{
	FString MeshBinPath;
	FString MaterialBinPath;
};

// Common
bool ShouldRegenerateFbxCache(const FString& AssetPath, const FString& MeshBinPath, const FString& MatBinPath);
void RegisterMaterialInfos(const TArray<FMaterialInfo>& MaterialInfos);
void EnsureCacheDirectory(const FString& MeshBinPath);
void RemoveCacheFiles(const FString& MeshBinPath, const FString& MatBinPath);

// Static
FStaticCachePaths GetStaticCachePaths(const FString& NormalizedPath);
bool TryLoadStaticMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FStaticMesh* Mesh, TArray<FMaterialInfo>& MaterialInfos);
void SaveStaticMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FStaticMesh* Mesh, const TArray<FMaterialInfo>& MaterialInfos);

// Skeletal
FSkeletalCachePaths GetSkeletalCachePaths(const FString& NormalizedPath);
bool TryLoadSkeletalMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FSkeletalMesh* Mesh, TArray<FMaterialInfo>& MaterialInfos);
void SaveSkeletalMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FSkeletalMesh* Mesh, const TArray<FMaterialInfo>& MaterialInfos);

