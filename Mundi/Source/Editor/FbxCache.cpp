#include "pch.h"

#include "FbxCache.h"

#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include "PathUtils.h"
#include "ResourceManager.h"
#include "Material.h"
#include "StaticMesh.h"
#include "SkeletalMeshStruct.h"
#include "Skeleton.h"
#include "Bone.h"



// ================================================================
// [1] FBX 캐시 파일의 최신 여부 검사
//  - FBX 원본 파일이 캐시(.bin)보다 더 최근이면 true 반환 → 재생성 필요
// ================================================================
bool ShouldRegenerateFbxCache(const FString& AssetPath, const FString& MeshBinPath, const FString& MatBinPath)
{
	if (!fs::exists(MeshBinPath) || !fs::exists(MatBinPath))
	{
		return true; // 캐시 파일이 없으면 무조건 재생성
	}

	try
	{
		auto BinTimestamp = fs::last_write_time(MeshBinPath);
		if (fs::last_write_time(AssetPath) > BinTimestamp)
		{
			return true; // FBX가 더 최신 → 캐시 갱신 필요
		}
	}
	catch (const fs::filesystem_error& e)
	{
		UE_LOG("Filesystem error during FBX cache validation: %s", e.what());
		return true; // 파일 접근 실패 시 안전하게 재생성
	}

	return false;
}



// ================================================================
// [2] FBX에서 읽은 머티리얼 정보를 리소스 매니저에 등록
//  - 새 머티리얼 객체를 생성하고, 기본 셰이더/매크로를 설정
// ================================================================
void RegisterMaterialInfos(const TArray<FMaterialInfo>& MaterialInfos)
{
	if (MaterialInfos.empty())
	{
		return;
	}

	UResourceManager& ResourceManager = UResourceManager::GetInstance();
	UMaterial* DefaultMaterial = ResourceManager.GetDefaultMaterial();
	UShader* DefaultShader = DefaultMaterial ? DefaultMaterial->GetShader() : nullptr;

	for (const FMaterialInfo& MatInfo : MaterialInfos)
	{
		if (MatInfo.MaterialName.empty())
		{
			continue;
		}

		// 동일 이름의 머티리얼이 없으면 새로 생성
		if (!ResourceManager.Get<UMaterial>(MatInfo.MaterialName))
		{
			UMaterial* Material = NewObject<UMaterial>();
			Material->SetMaterialInfo(MatInfo);

			// 기본 머티리얼의 셰이더 매크로를 복사
			if (DefaultMaterial)
			{
				Material->SetShaderMacros(DefaultMaterial->GetShaderMacros());
			}

			// 셰이더가 없으면 기본 셰이더 적용
			if (!Material->GetShader() && DefaultShader)
			{
				Material->SetShader(DefaultShader);
			}

			ResourceManager.Add<UMaterial>(MatInfo.MaterialName, Material);
		}
	}
}



// ================================================================
// [3] 스켈레탈 캐시 데이터 구조체들
//  - FSkeletalCacheBone : 본 정보 저장
//  - FSkeletalCacheFlesh : 스킨 파트(가중치, 본 인덱스 등)
//  - FSkeletalCacheData : 전체 메시 + 본 구조 저장
// ================================================================
struct FSkeletalCacheBone
{
	FString Name;
	int32 ParentIndex = -1;
	FTransform RelativeTransform;
	FTransform BindPose;

	// 직렬화/역직렬화 지원 (저장/읽기 공용)
	friend FArchive& operator<<(FArchive& Ar, FSkeletalCacheBone& Bone)
	{
		if (Ar.IsSaving())
		{
			Serialization::WriteString(Ar, Bone.Name);
		}
		else
		{
			Serialization::ReadString(Ar, Bone.Name);
		}

		Ar << Bone.ParentIndex;
		Ar << Bone.RelativeTransform;
		Ar << Bone.BindPose;
		return Ar;
	}
};

struct FSkeletalCacheFlesh
{
	FGroupInfo GroupInfo;
	TArray<int32> BoneIndices;
	TArray<float> Weights;
	float WeightsTotal = 1.0f;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalCacheFlesh& Flesh)
	{
		Ar << Flesh.GroupInfo;

		if (Ar.IsSaving())
		{
			Serialization::WriteArray(Ar, Flesh.BoneIndices);
			Serialization::WriteArray(Ar, Flesh.Weights);
		}
		else
		{
			Serialization::ReadArray(Ar, Flesh.BoneIndices);
			Serialization::ReadArray(Ar, Flesh.Weights);
		}

		Ar << Flesh.WeightsTotal;
		return Ar;
	}
};

struct FSkeletalCacheData
{
	FString PathFileName;
	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;
	bool bHasMaterial = false;
	TArray<FSkeletalCacheFlesh> Fleshes;
	TArray<FSkeletalCacheBone> Bones;

	// 메시 + 본 전체 직렬화 (저장/읽기)
	friend FArchive& operator<<(FArchive& Ar, FSkeletalCacheData& Data)
	{
		if (Ar.IsSaving())
		{
			Serialization::WriteString(Ar, Data.PathFileName);
			Serialization::WriteArray(Ar, Data.Vertices);
			Serialization::WriteArray(Ar, Data.Indices);
		}
		else
		{
			Serialization::ReadString(Ar, Data.PathFileName);
			Serialization::ReadArray(Ar, Data.Vertices);
			Serialization::ReadArray(Ar, Data.Indices);
		}

		Ar << Data.bHasMaterial;

		// Flesh 정보
		uint32 FleshCount = static_cast<uint32>(Data.Fleshes.size());
		Ar << FleshCount;
		if (Ar.IsSaving())
		{
			for (auto& Flesh : Data.Fleshes) Ar << Flesh;
		}
		else
		{
			Data.Fleshes.resize(FleshCount);
			for (auto& Flesh : Data.Fleshes) Ar << Flesh;
		}

		// Bone 정보
		uint32 BoneCount = static_cast<uint32>(Data.Bones.size());
		Ar << BoneCount;
		if (Ar.IsSaving())
		{
			for (auto& Bone : Data.Bones) Ar << Bone;
		}
		else
		{
			Data.Bones.resize(BoneCount);
			for (auto& Bone : Data.Bones) Ar << Bone;
		}

		return Ar;
	}
};



// ================================================================
// [4] 스켈레톤 계층을 재귀적으로 순회하여 캐시용 본 데이터 수집
// ================================================================
static void GatherBonesRecursive(UBone* Bone, int32 ParentIndex, TArray<FSkeletalCacheBone>& OutBones, TMap<UBone*, int32>& OutBoneIndexMap)
{
	if (!Bone) return;

	const int32 ThisIndex = static_cast<int32>(OutBones.size());
	OutBoneIndexMap.Add(Bone, ThisIndex);

	OutBones.emplace_back();
	FSkeletalCacheBone& CachedBone = OutBones.back();
	CachedBone.Name = Bone->GetName().ToString();
	CachedBone.ParentIndex = ParentIndex;
	CachedBone.RelativeTransform = Bone->GetRelativeTransform();
	CachedBone.BindPose = Bone->GetRelativeBindPose();

	for (UBone* Child : Bone->GetChildren())
	{
		GatherBonesRecursive(Child, ThisIndex, OutBones, OutBoneIndexMap);
	}
}



// ================================================================
// [5] 스켈레탈 메시로부터 캐시 데이터 생성 (저장 전 변환 단계)
// ================================================================
static void BuildSkeletalCacheData(const FSkeletalMesh* Mesh, FSkeletalCacheData& OutData)
{
	if (!Mesh) return;

	OutData.PathFileName = Mesh->PathFileName;
	OutData.Vertices = Mesh->Vertices;
	OutData.Indices = Mesh->Indices;
	OutData.bHasMaterial = Mesh->bHasMaterial;
	OutData.Fleshes.clear();
	OutData.Bones.clear();

	if (!Mesh->Skeleton) return;

	UBone* Root = Mesh->Skeleton->GetRoot();
	if (!Root) return;

	TMap<UBone*, int32> BoneIndexMap;
	GatherBonesRecursive(Root, -1, OutData.Bones, BoneIndexMap);

	// Flesh 그룹 변환
	OutData.Fleshes.resize(Mesh->Fleshes.size());
	for (size_t FleshIdx = 0; FleshIdx < Mesh->Fleshes.size(); ++FleshIdx)
	{
		const FFlesh& Flesh = Mesh->Fleshes[FleshIdx];
		FSkeletalCacheFlesh& CachedFlesh = OutData.Fleshes[FleshIdx];
		CachedFlesh.GroupInfo.StartIndex = Flesh.StartIndex;
		CachedFlesh.GroupInfo.IndexCount = Flesh.IndexCount;
		CachedFlesh.GroupInfo.InitialMaterialName = Flesh.InitialMaterialName;
		CachedFlesh.Weights = Flesh.Weights;
		CachedFlesh.WeightsTotal = Flesh.WeightsTotal;

		// 본 인덱스 매핑
		CachedFlesh.BoneIndices.clear();
		for (UBone* Bone : Flesh.Bones)
		{
			CachedFlesh.BoneIndices.push_back(BoneIndexMap.Contains(Bone) ? *BoneIndexMap.Find(Bone) : -1);
		}
	}
}



// ================================================================
// [6] 캐시 데이터로부터 스켈레톤 복원 (메모리 상에서 재구성)
// ================================================================
static USkeleton* RebuildSkeletonFromCache(const TArray<FSkeletalCacheBone>& CachedBones, TArray<UBone*>& OutBonePointers)
{
	OutBonePointers.clear();
	if (CachedBones.empty()) return nullptr;

	// 본 인스턴스 생성
	OutBonePointers.resize(CachedBones.size());
	for (size_t i = 0; i < CachedBones.size(); ++i)
	{
		const auto& CachedBone = CachedBones[i];
		UBone* Bone = new UBone(FName(CachedBone.Name.c_str()), CachedBone.RelativeTransform);
		ObjectFactory::AddToGUObjectArray(UBone::StaticClass(), Bone);
		Bone->SetRelativeBindPoseTransform(CachedBone.BindPose);
		OutBonePointers[i] = Bone;
	}

	// 부모-자식 관계 복원
	for (size_t i = 0; i < CachedBones.size(); ++i)
	{
		int32 ParentIdx = CachedBones[i].ParentIndex;
		if (ParentIdx >= 0 && ParentIdx < (int32)CachedBones.size())
		{
			OutBonePointers[i]->SetParent(OutBonePointers[ParentIdx]);
			OutBonePointers[ParentIdx]->AddChild(OutBonePointers[i]);
		}
	}

	// 루트 본 탐색 및 스켈레톤 생성
	UBone* Root = nullptr;
	for (size_t i = 0; i < CachedBones.size(); ++i)
	{
		if (CachedBones[i].ParentIndex < 0) { Root = OutBonePointers[i]; break; }
	}
	if (!Root) Root = OutBonePointers.front();

	USkeleton* Skeleton = NewObject<USkeleton>();
	Skeleton->SetRoot(Root);
	return Skeleton;
}

// ================================================================
// [7] 캐시 경로 생성 (스태틱/스켈레탈 각각)
// ================================================================
FStaticCachePaths GetStaticCachePaths(const FString& NormalizedPath)
{
	FString CachePathStr = ConvertDataPathToCachePath(NormalizedPath);
	return { CachePathStr + ".bin", CachePathStr + ".mat.bin" };
}

FSkeletalCachePaths GetSkeletalCachePaths(const FString& NormalizedPath)
{
	FString CachePathStr = ConvertDataPathToCachePath(NormalizedPath);
	return { CachePathStr + ".skel.bin", CachePathStr + ".skel.mat.bin" };
}



// ================================================================
// [8] 캐시 폴더 존재 보장 (없으면 생성)
// ================================================================
void EnsureCacheDirectory(const FString& MeshBinPath)
{
	fs::path CacheFileDirPath(MeshBinPath);
	if (CacheFileDirPath.has_parent_path())
	{
		std::error_code ec;
		fs::create_directories(CacheFileDirPath.parent_path(), ec);
		if (ec)
		{
			UE_LOG("[FBX Cache] Failed to create cache directory %s: %s",
				CacheFileDirPath.parent_path().string().c_str(),
				ec.message().c_str());
		}
	}
}



// ================================================================
// [9] 정적 메시 캐시 로드 / 저장 / 삭제
// ================================================================
bool TryLoadStaticMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FStaticMesh* Mesh, TArray<FMaterialInfo>& MaterialInfos)
{
	try
	{
		// 메시 캐시 로드
		FWindowsBinReader Reader(MeshBinPath);
		if (!Reader.IsOpen()) throw std::runtime_error("Failed to open mesh cache.");
		Reader << *Mesh;
		Reader.Close();

		// 머티리얼 캐시 로드
		FWindowsBinReader MatReader(MatBinPath);
		if (!MatReader.IsOpen()) throw std::runtime_error("Failed to open material cache.");
		Serialization::ReadArray(MatReader, MaterialInfos);
		MatReader.Close();

		Mesh->CacheFilePath = MeshBinPath;
		return true;
	}
	catch (const std::exception& e)
	{
		UE_LOG("[FBX Cache] %s", e.what());
		return false;
	}
}

void SaveStaticMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FStaticMesh* Mesh, const TArray<FMaterialInfo>& MaterialInfos)
{
	try
	{
		// 메시 저장
		FWindowsBinWriter Writer(MeshBinPath);
		Writer << *Mesh;
		Writer.Close();

		// 머티리얼 저장
		FWindowsBinWriter MatWriter(MatBinPath);
		Serialization::WriteArray(MatWriter, MaterialInfos);
		MatWriter.Close();

		Mesh->CacheFilePath = MeshBinPath;
	}
	catch (const std::exception& e)
	{
		UE_LOG("[FBX Cache] Failed to save cache: %s", e.what());
	}
}

void RemoveCacheFiles(const FString& MeshBinPath, const FString& MatBinPath)
{
	std::error_code ec;
	fs::remove(MeshBinPath, ec);
	fs::remove(MatBinPath, ec);
}



// ================================================================
// [10] 스켈레탈 메시 캐시 로드 / 저장
//  - 본 및 웨이트 구조를 함께 복원
// ================================================================
bool TryLoadSkeletalMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FSkeletalMesh* Mesh, TArray<FMaterialInfo>& MaterialInfos)
{
	try
	{
		// 메시 + 본 데이터 읽기
		FWindowsBinReader Reader(MeshBinPath);
		if (!Reader.IsOpen()) throw std::runtime_error("Failed to open skeletal mesh cache.");
		FSkeletalCacheData CacheData;
		Reader << CacheData;
		Reader.Close();

		// 머티리얼 읽기
		FWindowsBinReader MatReader(MatBinPath);
		if (!MatReader.IsOpen()) throw std::runtime_error("Failed to open skeletal material cache.");
		Serialization::ReadArray(MatReader, MaterialInfos);
		MatReader.Close();

		// 기본 데이터 복원
		Mesh->PathFileName = CacheData.PathFileName;
		Mesh->Vertices = CacheData.Vertices;
		Mesh->Indices = CacheData.Indices;
		Mesh->bHasMaterial = CacheData.bHasMaterial;
		Mesh->CacheFilePath = MeshBinPath;
		Mesh->Fleshes.clear();
		Mesh->Skeleton = nullptr;

		// 본 복원
		if (!CacheData.Bones.empty())
		{
			TArray<UBone*> BonePointers;
			USkeleton* Skeleton = RebuildSkeletonFromCache(CacheData.Bones, BonePointers);
			Mesh->Skeleton = Skeleton;

			// Flesh 데이터 복원
			for (const FSkeletalCacheFlesh& CachedFlesh : CacheData.Fleshes)
			{
				FFlesh Flesh;
				Flesh.StartIndex = CachedFlesh.GroupInfo.StartIndex;
				Flesh.IndexCount = CachedFlesh.GroupInfo.IndexCount;
				Flesh.InitialMaterialName = CachedFlesh.GroupInfo.InitialMaterialName;
				for (int32 BoneIndex : CachedFlesh.BoneIndices)
				{
					if (BoneIndex >= 0 && BoneIndex < BonePointers.Num())
						Flesh.Bones.Add(BonePointers[BoneIndex]);
				}
				Flesh.Weights = CachedFlesh.Weights;
				Flesh.WeightsTotal = CachedFlesh.WeightsTotal;
				Mesh->Fleshes.Add(Flesh);
			}
		}

		return true;
	}
	catch (const std::exception& e)
	{
		UE_LOG("[FBX Cache] %s", e.what());
		return false;
	}
}

void SaveSkeletalMeshCache(const FString& MeshBinPath, const FString& MatBinPath, FSkeletalMesh* Mesh, const TArray<FMaterialInfo>& MaterialInfos)
{
	try
	{
		// 캐시 데이터 구성
		FSkeletalCacheData CacheData;
		BuildSkeletalCacheData(Mesh, CacheData);

		// 메시 저장
		FWindowsBinWriter Writer(MeshBinPath);
		Writer << CacheData;
		Writer.Close();

		// 머티리얼 저장
		FWindowsBinWriter MatWriter(MatBinPath);
		Serialization::WriteArray(MatWriter, MaterialInfos);
		MatWriter.Close();

		Mesh->CacheFilePath = MeshBinPath;
	}
	catch (const std::exception& e)
	{
		UE_LOG("[FBX Cache] Failed to save skeletal cache: %s", e.what());
	}
}
