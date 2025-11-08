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

// Quantization helpers and vertex key for deduplication

//struct VKey {
//    uint32 cp; 
//	int nx, ny, nz; 
//	int ux, uy; 
//	uint8 bi[4]; 
//	uint8 bw[4];
//    bool operator==(const VKey& o) const {
//        if (cp!=o.cp||nx!=o.nx||ny!=o.ny||nz!=o.nz||ux!=o.ux||uy!=o.uy) return false;
//        for(int i=0;i<4;++i){ if(bi[i]!=o.bi[i]||bw[i]!=o.bw[i]) return false; }
//        return true;
//    }
//};
//struct VKeyHash { size_t operator()(const VKey& k) const {
//    size_t h = k.cp;
//    auto mix=[&](size_t v){ h ^= v + 0x9e3779b97f4a7c15ull + (h<<6) + (h>>2); };
//    mix((size_t)k.nx); mix((size_t)k.ny); mix((size_t)k.nz); mix((size_t)k.ux); mix((size_t)k.uy);
//    for(int i=0;i<4;++i){ mix(k.bi[i]); mix(k.bw[i]); }
//    return h;
//} };

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

	// ────────────────────────────────────────────────
	// [0] 캐시 파일 경로 계산 및 디렉토리 준비
	// ────────────────────────────────────────────────
	FString CacheBase = ConvertDataPathToCachePath(NormalizedPath);
    const FString BinPath = CacheBase + ".skel.v4.bin";

	fs::path BinFs(BinPath);
	if (BinFs.has_parent_path())
		fs::create_directories(BinFs.parent_path());

	// 캐시 최신 여부 판단
	bool bShouldRegen = ShouldRegenerateCacheFBX(NormalizedPath, BinPath);
	UE_LOG("[FBXManager] Cache check: src='%s' bin='%s' -> %s",
		NormalizedPath.c_str(), BinPath.c_str(),
		bShouldRegen ? "REGENERATE" : "LOAD");

	// ────────────────────────────────────────────────
	// [1] 캐시가 최신이면 캐시에서 로드
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

    // Write raw (importer) UV debug dump on successful parse
    {
        std::string DumpPath = (CacheBase + ".uvdump.txt");
        Importer.WriteDebugDump(DumpPath);
        UE_LOG("[FBXManager] Wrote UV debug dump: %s", DumpPath.c_str());
    }

	auto* NewAsset = new FSkeletalMesh();
	NewAsset->PathFileName = NormalizedPath;
	NewAsset->Bones = Importer.Bones;

	// ────────────────────────────────────────────────
	// [2-1] Corner 기반 Vertex 생성 및 중복 제거 (Deduplication)
	// ────────────────────────────────────────────────
	std::unordered_map<VKey, uint32, VKeyHash> VertexLUT;

	const auto& CornerControlPointIndices = Importer.CornerControlPointIndices;
	const auto& CornerNormals = Importer.CornerNormals;
	const auto& CornerUVs = Importer.CornerUVs;
	const auto& TriangleCornerIndices = Importer.TriangleCornerIndices;
	const auto& TriangleMaterialIndices = Importer.TriangleMaterialIndex;

	NewAsset->Vertices.reserve(CornerControlPointIndices.size());

	// ────────────────────────────────────────────────
	// [2-2] 머티리얼별 인덱스 버킷 초기화
	// ────────────────────────────────────────────────
	size_t TriangleCount = TriangleCornerIndices.size() / 3;

	int MaxMaterialIndex = -1;
	if (!TriangleMaterialIndices.empty())
	{
		for (int MaterialIndex : TriangleMaterialIndices)
			if (MaterialIndex > MaxMaterialIndex)
				MaxMaterialIndex = MaterialIndex;
	}

	size_t BucketCount = (MaxMaterialIndex >= 0) ? static_cast<size_t>(MaxMaterialIndex + 1) : 1;

	// 삼각형 마다, 어떤 머터리얼에 속하는지 정보가 존재한다. 
	// 렌더링 최적화를 위해 머터리얼 별로 인덱스 그룹을 따로 둔다. 
	TArray<TArray<uint32>> IndexBuckets(BucketCount);

	// ────────────────────────────────────────────────
	// [2-3] Corner → Vertex 변환 함수
	// ────────────────────────────────────────────────
	auto GetVertexIndex = [&](uint32 CornerID) -> uint32
		{
			uint32 ControlPointIndex = CornerControlPointIndices[CornerID];
			const FVector& Normal = CornerNormals[CornerID];
			const FVector2D& UV = CornerUVs[CornerID];
			const FSkinnedVertex& ControlPointVertex = Importer.SkinnedVertices[ControlPointIndex];

			VKey Key{};
			Key.cp = ControlPointIndex;
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

			auto It = VertexLUT.find(Key);
			if (It != VertexLUT.end())
				return It->second;

			// 새로운 정점 추가
			FSkinnedVertex NewVertex{};
			NewVertex.pos = ControlPointVertex.pos;
			NewVertex.normal = Normal;
			NewVertex.uv = UV;

			std::copy_n(ControlPointVertex.boneIndices, 4, NewVertex.boneIndices);
			std::copy_n(ControlPointVertex.boneWeights, 4, NewVertex.boneWeights);

			uint32 NewIndex = static_cast<uint32>(NewAsset->Vertices.size());
			NewAsset->Vertices.push_back(NewVertex);
			VertexLUT.emplace(Key, NewIndex);
			return NewIndex;
		};

	// ────────────────────────────────────────────────
	// [2-4] 삼각형 인덱스 버퍼 생성 (머티리얼별 버킷 분류)
	// ────────────────────────────────────────────────
	for (size_t T = 0; T < TriangleCount; ++T)
	{
		uint32 Corner0 = TriangleCornerIndices[T * 3 + 0];
		uint32 Corner1 = TriangleCornerIndices[T * 3 + 1];
		uint32 Corner2 = TriangleCornerIndices[T * 3 + 2];

		uint32 Vertex0 = GetVertexIndex(Corner0);
		uint32 Vertex1 = GetVertexIndex(Corner1);
		uint32 Vertex2 = GetVertexIndex(Corner2);

		size_t Bucket = 0;
		if (!TriangleMaterialIndices.empty())
		{
			int MatIndex = TriangleMaterialIndices[T];
			Bucket = (MatIndex >= 0) ? static_cast<size_t>(MatIndex) : 0;

			// 만약 FBX에 없는 머티리얼 인덱스가 들어왔다면 확장
			if (Bucket >= IndexBuckets.size())
				IndexBuckets.resize(Bucket + 1);
		}

		IndexBuckets[Bucket].push_back(Vertex0);
		IndexBuckets[Bucket].push_back(Vertex1);
		IndexBuckets[Bucket].push_back(Vertex2);
	}

	// ────────────────────────────────────────────────
	// [2-5] 버킷 병합 → 최종 인덱스 버퍼 및 섹션 정보 생성
	// ────────────────────────────────────────────────
	NewAsset->Indices.clear();
	NewAsset->Sections.clear();

	uint32 StartIndex = 0;
	for (size_t M = 0; M < IndexBuckets.size(); ++M)
	{
		const auto& Indices = IndexBuckets[M];
		if (Indices.empty())
			continue;

		FGroupInfo Section{};
		Section.StartIndex = StartIndex;
		Section.IndexCount = static_cast<uint32>(Indices.size());
		// Section.MaterialName은 이후 매핑 단계에서 지정 가능

		NewAsset->Sections.push_back(Section);
		NewAsset->Indices.insert(NewAsset->Indices.end(), Indices.begin(), Indices.end());

		StartIndex += Section.IndexCount;
	}

	NewAsset->bHasMaterial = !NewAsset->Sections.empty();

	// ────────────────────────────────────────────────
	// [3] 캐시 파일 저장
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
