#include "pch.h"
#include "FBXManager.h"

#include "FBXImporter.h"
#include "PathUtils.h"
#include "SkeletalMesh.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include <filesystem>

namespace fs = std::filesystem;

static bool ShouldRegenerateCacheFBX(const FString& FBXPath, const FString& BinPath)
{
	try
	{
		if (!fs::exists(BinPath)) return true;
		if (!fs::exists(FBXPath)) return true; // missing source, force regen path (will fail later)
		return fs::last_write_time(FBXPath) > fs::last_write_time(BinPath);
	}
	catch (...)
	{
		return true;
	}
}

FFBXManager& FFBXManager::Get()
{
	static FFBXManager Instance;
	return Instance;
}

void FFBXManager::Clear()
{
	for (auto& Pair : FBXSkeletalAssetMap)
	{
		delete Pair.second;
	}
	FBXSkeletalAssetMap.Empty();
}

// FBX 스켈레탈 메시를 로드(또는 캐시에서 불러오는) Asset 로더
FSkeletalMesh* FFBXManager::LoadFBXSkeletalMeshAsset(const FString& PathFileName)
{
	if (PathFileName.empty())
		return nullptr;

	FString NormalizedPath = NormalizePath(PathFileName);

	// 이미 로드된 자산이 있으면 바로 반환
	if (FSkeletalMesh** Found = FBXSkeletalAssetMap.Find(NormalizedPath))
		return *Found;

	// 캐시 파일 경로 계산
	FString CacheBase = ConvertDataPathToCachePath(NormalizedPath);
	const FString BinPath = CacheBase + ".skel.bin";

	// 캐시 디렉토리 존재 보장
	fs::path BinFs(BinPath);
	if (BinFs.has_parent_path())
		fs::create_directories(BinFs.parent_path());

	// 캐시 최신 여부 판단
	bool bShouldRegen = ShouldRegenerateCacheFBX(NormalizedPath, BinPath);
	UE_LOG("[FBXManager] Cache check: src='%s' bin='%s' -> %s",
		NormalizedPath.c_str(), BinPath.c_str(), bShouldRegen ? "REGENERATE" : "LOAD");

	// ────────────────────────────────────────────────
	// [1] 캐시가 최신이면: 캐시에서 읽기
	// ────────────────────────────────────────────────
	if (!bShouldRegen)
	{
		FWindowsBinReader Reader(BinPath);
		if (Reader.IsOpen()) // ← 예외 대신 단순한 스트림 상태 체크
		{
			auto* NewAsset = new FSkeletalMesh();
			Reader << *NewAsset;
			Reader.Close();

			UE_LOG("[FBXManager] Loaded FSkeletalMesh from cache: %s", BinPath.c_str());
			FBXSkeletalAssetMap.Add(NormalizedPath, NewAsset);
			return NewAsset;
		}
		else
		{
			UE_LOG("[FBXManager] Cache read failed: %s", BinPath.c_str());
		}
	}

	// ────────────────────────────────────────────────
	// [2] 캐시가 없거나 실패했으면 FBX 재파싱
	// ────────────────────────────────────────────────
	FFBXImporter Importer;
	if (!Importer.LoadFBX(NormalizedPath))
	{
		UE_LOG("[FBXManager] Failed to load FBX '%s'", NormalizedPath.c_str());
		return nullptr;
	}

	auto* NewAsset = new FSkeletalMesh();
	NewAsset->PathFileName = NormalizedPath;
	NewAsset->Vertices = Importer.SkinnedVertices;
	NewAsset->Indices = Importer.TriangleIndices;
	NewAsset->Bones = Importer.Bones;

	// ────────────────────────────────────────────────
	// [3] 캐시 파일 저장 (예외 없이)
	// ────────────────────────────────────────────────
	FWindowsBinWriter Writer(BinPath);
	if (Writer.IsOpen())
	{
		Writer << *NewAsset;
		Writer.Close();
		UE_LOG("[FBXManager] Wrote FSkeletalMesh cache: %s", BinPath.c_str());
	}
	else
	{
		UE_LOG("[FBXManager] Cache write failed: %s", BinPath.c_str());
	}

	FBXSkeletalAssetMap.Add(NormalizedPath, NewAsset);
	return NewAsset;
}
