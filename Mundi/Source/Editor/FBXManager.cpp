#include "pch.h"
#include "FBXManager.h"

#include "FBXImporter.h"
#include "PathUtils.h"
#include "SkeletalMesh.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <cmath>

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
	const FString BinPath = CacheBase + ".skel.v2.bin";

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
		if (Reader.IsOpen())
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
	NewAsset->Bones = Importer.Bones;

	// ────────────────────────────────────────────────
	// [2-1] Corner 기반 Vertex 생성 및 Deduplication
	// ────────────────────────────────────────────────

	// 부동소수점 → 정수 변환 (정규화) 함수
	//auto QuantizeFloat = [](float Value) -> int
	//	{
	//		return static_cast<int>(std::round(Value * 10000.0f));
	//	};

	//// Bone Weight(0~1)를 0~255 범위로 정규화
	//auto QuantizeWeight = [](float Weight) -> uint8
	//	{
	//		float Clamped = std::clamp(Weight, 0.0f, 1.0f);
	//		return static_cast<uint8>(std::round(Clamped * 255.0f));
	//	};

	std::unordered_map<VKey, uint32, VKeyHash> VertexLUT;

	const auto& CornerControlPointIndices = Importer.CornerControlPointIndices;
	const auto& CornerNormals = Importer.CornerNormals;
	const auto& CornerUVs = Importer.CornerUVs;
	const auto& TriangleCornerIndices = Importer.TriangleCornerIndices;

	NewAsset->Vertices.reserve(CornerControlPointIndices.size());
	NewAsset->Indices.reserve(TriangleCornerIndices.size());

	for (size_t CornerIndex = 0; CornerIndex < TriangleCornerIndices.size(); ++CornerIndex)
	{
		uint32 CornerID = TriangleCornerIndices[CornerIndex];
		uint32 ControlPointID = CornerControlPointIndices[CornerID];
		const FVector& Normal = CornerNormals[CornerID];
		const FVector2D& UV = CornerUVs[CornerID];
		const FSkinnedVertex& ControlPointVertex = Importer.SkinnedVertices[ControlPointID];

		VKey Key{};
		Key.cp = ControlPointID;
		Key.nx = QuantizeFloat(Normal.X);
		Key.ny = QuantizeFloat(Normal.Y);
		Key.nz = QuantizeFloat(Normal.Z);
		Key.ux = QuantizeFloat(UV.X);
		Key.uy = QuantizeFloat(UV.Y);

		for (int i = 0; i < 4; ++i)
		{
			Key.bi[i] = static_cast<uint8>(ControlPointVertex.boneIndices[i]);
			Key.bw[i] = QuantizeWeight(ControlPointVertex.boneWeights[i]);
		}

		// 중복 정점 검색
		auto It = VertexLUT.find(Key);
		uint32 VertexIndex = 0;

		if (It == VertexLUT.end())
		{
            FSkinnedVertex Vertex{};
            Vertex.pos = ControlPointVertex.pos;
            Vertex.normal = Normal;
            Vertex.uv = UV;
			// 4 개 원소 복사 .
            std::copy_n(ControlPointVertex.boneIndices, 4, Vertex.boneIndices);
            std::copy_n(ControlPointVertex.boneWeights, 4, Vertex.boneWeights);

			VertexIndex = static_cast<uint32>(NewAsset->Vertices.size());
			NewAsset->Vertices.push_back(Vertex);
			VertexLUT.emplace(Key, VertexIndex);
		}
		else
		{
			VertexIndex = It->second;
		}

		NewAsset->Indices.push_back(VertexIndex);
	}

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


