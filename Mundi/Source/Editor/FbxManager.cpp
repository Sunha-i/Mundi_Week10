#include "pch.h"
#include "FbxManager.h"
#include "FbxDebugLog.h"
#include "Bone.h"
#include "Keyboard.h"
#include "SkeletalMesh.h"
#include "ObjManager.h"
#include "StaticMesh.h"
#include "D3D11RHI.h"
#include "Enums.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include "FbxCache.h"
#include "FbxImporter.h"
#include "ObjectIterator.h"

// =============================================================
// FFbxManager
//   FBX 로딩 전체를 총괄하는 허브 클래스
//   - FBX SDK 초기화 및 Importer 생성
//   - 캐시 검사 → 로드 or 재생성
//   - Skeletal / Static Mesh 분기 처리
// =============================================================

FFbxManager::FFbxManager()
{
	Initialize();
}

FFbxManager::~FFbxManager()
{
	Shutdown();
}

// =============================================================
// Initialize()
//   - FBX SDK 관리자(FbxManager) 생성
//   - Importer, IO 설정 등 초기화
// =============================================================
void FFbxManager::Initialize()
{
	// FBX SDK 관리자 생성 (FBX SDK 전체 리소스 관리 담당)
	SdkManager = FbxManager::Create();

	// FBX Importer 유틸리티 생성 (FBX 데이터 파싱 담당 클래스)
	ImporterUtil = new FFbxImporter(SdkManager);

	// IO 설정 객체 생성 및 등록
	ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(ios);

	// FBXImporter 백엔드 인스턴스 생성 (실제 SDK용)
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
}

// =============================================================
// Shutdown()
//   - 로드된 FBX 관련 데이터 정리 및 FBX SDK 종료
// =============================================================
void FFbxManager::Shutdown()
{
	// SkeletalMeshMap의 모든 자산 삭제
	for (auto Iter = FbxSkeletalMeshMap.begin(); Iter != FbxSkeletalMeshMap.end(); Iter++)
	{
		if (Iter->second)
			delete Iter->second;
	}

	// Importer 유틸리티 해제
	if (ImporterUtil)
	{
		delete ImporterUtil;
		ImporterUtil = nullptr;
	}

	// FBX SDK 관리자 해제
	SdkManager->Destroy();
}

// =============================================================
// GetInstance()
//   - FFbxManager 싱글톤 반환
// =============================================================
FFbxManager& FFbxManager::GetInstance()
{
	static FFbxManager FbxManager;
	return FbxManager;
}

// =============================================================
// Preload()
//   - GFbxDataDir 경로에서 .fbx 파일을 전부 스캔하여 사전 로드
//   - SkeletalMesh / StaticMesh 형태에 맞게 자동 분류 및 등록
// =============================================================
void FFbxManager::Preload()
{
	UE_LOG("==========================================");
	UE_LOG("FFbxManager::Preload STARTED");
	UE_LOG("==========================================");

	const fs::path FbxDir(GFbxDataDir);
	UE_LOG("[FBX Preload] GFbxDataDir = %s", GFbxDataDir.c_str());

	// 폴더 존재 여부 확인
	if (!fs::exists(FbxDir) || !fs::is_directory(FbxDir))
	{
		UE_LOG("[FBX Preload ERROR] FBX directory not found: %s", FbxDir.string().c_str());
		return;
	}

	size_t LoadedCount = 0;
	size_t TotalFilesScanned = 0;
	std::unordered_set<FString> ProcessedFiles; // 중복 로딩 방지용

	try
	{
		// FBX 폴더 내 모든 파일을 재귀적으로 탐색
		for (const auto& Entry : fs::recursive_directory_iterator(FbxDir))
		{
			TotalFilesScanned++;

			if (!Entry.is_regular_file())
				continue;

			const fs::path& Path = Entry.path();
			FString Extension = Path.extension().string();
			std::transform(Extension.begin(), Extension.end(), Extension.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			// FBX 파일만 대상
			if (Extension == ".fbx")
			{
				FString PathStr = NormalizePath(Path.string());

				// 이미 처리된 파일은 스킵
				if (ProcessedFiles.find(PathStr) != ProcessedFiles.end())
					continue;

				ProcessedFiles.insert(PathStr);

				// SkeletalMesh 로드 시도
				FSkeletalMesh* SkeletalMeshAsset = LoadFbxSkeletalMeshAsset(PathStr);

				// 본이 존재한다면 SkeletalMesh, 아니면 StaticMesh로 구분
				if (SkeletalMeshAsset && SkeletalMeshAsset->Skeleton)
				{
					USkeletalMesh* USkeletalMeshObj = NewObject<USkeletalMesh>();
					USkeletalMeshObj->SetFilePath(PathStr);
					USkeletalMeshObj->Load(PathStr, GEngine.GetRHIDevice()->GetDevice());
					UResourceManager::GetInstance().Add<USkeletalMesh>(PathStr, USkeletalMeshObj);
				}

				++LoadedCount;
			}
		}
	}
	catch (const std::exception& e)
	{
		UE_LOG("[FBX Preload ERROR] Exception during scan: %s", e.what());
		return;
	}

	UE_LOG("[FBX Preload] Scan complete. Total files scanned: %zu", TotalFilesScanned);
	UE_LOG("Loaded %zu .fbx files from %s", LoadedCount, FbxDir.string().c_str());
	UE_LOG("==========================================");
}

// =============================================================
// Clear()
//   - 로드된 SkeletalMeshMap 내부 자산 전부 삭제
// =============================================================
void FFbxManager::Clear()
{
	for (auto Iter = FbxSkeletalMeshMap.begin(); Iter != FbxSkeletalMeshMap.end(); Iter++)
		delete Iter->second;

	FbxSkeletalMeshMap.clear();
}

// =============================================================
// LoadFbxSkeletalMeshAsset()
//   - FBX 파일을 SkeletalMesh로 로드하는 메인 함수
//   - 캐시를 우선 확인하고, 없거나 오래되었으면 Import 후 캐시 생성
// =============================================================
FSkeletalMesh* FFbxManager::LoadFbxSkeletalMeshAsset(const FString& PathFileName)
{
	FString NormalizedPathStr = NormalizePath(PathFileName);

	// 이미 로드된 FBX가 있으면 반환
	if (auto Cached = FbxSkeletalMeshMap.Find(NormalizedPathStr))
		return *Cached;

	// 확장자 확인 (.fbx)
	if (!ValidateFbxFile(NormalizedPathStr))
		return nullptr;

#ifdef USE_OBJ_CACHE
	// 캐시 유효하면 바로 로드
	{
		FSkeletalCachePaths CachePaths = GetSkeletalCachePaths(NormalizedPathStr);
		if (!ShouldRegenerateFbxCache(NormalizedPathStr, CachePaths.MeshBinPath, CachePaths.MaterialBinPath))
		{
			FSkeletalMesh* CachedMesh = new FSkeletalMesh();
			CachedMesh->PathFileName = NormalizedPathStr;
			TArray<FMaterialInfo> CachedMaterials;
			if (TryLoadSkeletalMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, CachedMesh, CachedMaterials))
			{
				RegisterMaterialInfos(CachedMaterials);
				FbxSkeletalMeshMap.Add(NormalizedPathStr, CachedMesh);
				return CachedMesh;
			}

			// 캐시 파일이 손상된 경우 재생성
			delete CachedMesh;
			RemoveCacheFiles(CachePaths.MeshBinPath, CachePaths.MaterialBinPath);
		}
	}
#endif

	// FBX Importer를 통해 씬 로드
	FbxScene* Scene = ImporterUtil->ImportFbxScene(NormalizedPathStr);
	if (!Scene)
		return nullptr;

	// 머티리얼 정보 추출 (함수명 및 인자 순서 수정됨)
	TMap<int64, FMaterialInfo> MaterialMap;
	TArray<FMaterialInfo> MaterialInfos;
	ImporterUtil->CollectMaterials(Scene, NormalizedPathStr, MaterialMap, MaterialInfos);

	// 루트 노드 확인
	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode)
	{
		Scene->Destroy();
		return nullptr;
	}

	// 본 계층 구조 생성
	UBone* RootBone = ImporterUtil->FindSkeletonRootAndBuild(RootNode);
	FSkeletalMesh* SkeletalMesh = nullptr;

	if (RootBone)
	{
		// 스켈레탈 메시 빌드
		SkeletalMesh = BuildSkeletalMesh(Scene, RootNode, RootBone, MaterialMap, NormalizedPathStr);
		RegisterMaterialInfos(MaterialInfos);

#ifdef USE_OBJ_CACHE
		// 캐시 저장
		if (SkeletalMesh)
		{
			FSkeletalCachePaths CachePaths = GetSkeletalCachePaths(NormalizedPathStr);
			EnsureCacheDirectory(CachePaths.MeshBinPath);
			SaveSkeletalMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, SkeletalMesh, MaterialInfos);
		}
#endif
	}
	else
	{
		// 본이 없는 경우 StaticMesh로 로드
		BuildStaticMeshFromScene(Scene, MaterialMap, MaterialInfos, NormalizedPathStr);
	}

	Scene->Destroy();
	return SkeletalMesh;
}


// =============================================================
// BuildStaticMeshFromScene()
//   - FBX 씬에서 정점/인덱스/머티리얼 정보를 추출해 StaticMesh 생성
// =============================================================
void FFbxManager::BuildStaticMeshFromScene(FbxScene* Scene, const TMap<int64, FMaterialInfo>& MaterialMap, const TArray<FMaterialInfo>& MaterialInfos, const FString& Path)
{
	FStaticMesh* NewStatic = new FStaticMesh();
	NewStatic->PathFileName = Path;

	// 정점/인덱스 추출
	FbxNode* Root = Scene->GetRootNode();
	ImporterUtil->ProcessMeshNodeAsStatic(Root, NewStatic, MaterialMap);

	RegisterMaterialInfos(MaterialInfos);

#ifdef USE_OBJ_CACHE
	// 캐시 저장
	FStaticCachePaths CachePaths = GetStaticCachePaths(Path);
	EnsureCacheDirectory(CachePaths.MeshBinPath);
	SaveStaticMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, NewStatic, MaterialInfos);
#endif

	// 엔진 리소스 등록
	UStaticMesh* UStatic = NewObject<UStaticMesh>();
	UStatic->SetFilePath(Path);
	UStatic->Load(Path, GEngine.GetRHIDevice()->GetDevice());
	UResourceManager::GetInstance().Add<UStaticMesh>(Path, UStatic);

	FObjManager::AddToCache(Path, NewStatic);
}

// =============================================================
// BuildSkeletalMesh()
//   - FBX에 Skeleton이 있을 경우 SkeletalMesh 생성
//   - 본 계층(USkeleton) 생성 및 메시 파싱
// =============================================================
FSkeletalMesh* FFbxManager::BuildSkeletalMesh(
	FbxScene* Scene,
	FbxNode* RootNode,
	UBone* RootBone,
	const TMap<int64, FMaterialInfo>& MaterialMap,
	const FString& Path)
{
	FSkeletalMesh* NewMesh = new FSkeletalMesh();
	NewMesh->PathFileName = Path;

	NewMesh->Skeleton = NewObject<USkeleton>();
	NewMesh->Skeleton->SetRoot(RootBone);

	ImporterUtil->ProcessMeshNode(RootNode, NewMesh, MaterialMap);
	FbxSkeletalMeshMap.Add(Path, NewMesh);

	return NewMesh;
}

// =============================================================
// ValidateFbxFile()
//   - 파일 확장자가 .fbx 인지 검사
// =============================================================
bool FFbxManager::ValidateFbxFile(const FString& Path)
{
	std::filesystem::path FilePath(Path);
	FString Extension = FilePath.extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char c) { return std::tolower(c); });

	return (Extension == ".fbx");
}

// =============================================================
// LoadFbxSkeletalMesh()
//   - 파일 경로를 통해 이미 로드된 USkeletalMesh를 반환하거나 새로 생성
// =============================================================
USkeletalMesh* FFbxManager::LoadFbxSkeletalMesh(const FString& PathFileName)
{
	FString NormalizedPathStr = NormalizePath(PathFileName);

	// 이미 로드된 USkeletalMesh가 있으면 재사용
	for (TObjectIterator<USkeletalMesh> It; It; ++It)
	{
		USkeletalMesh* SkeletalMesh = *It;
		if (SkeletalMesh->GetFilePath() == NormalizedPathStr)
			return SkeletalMesh;
	}

	// 없으면 새로 로드
	USkeletalMesh* SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(NormalizedPathStr);
	return SkeletalMesh;
}

// =============================================================
// LoadFbxStaticMeshAsset()
//   - FBX 파일을 StaticMesh 형태로 로드
//   - 캐시 존재 시 캐시 사용, 없으면 FBX 직접 Import
// =============================================================
FStaticMesh* FFbxManager::LoadFbxStaticMeshAsset(const FString& PathFileName)
{
	FString NormalizedPathStr = NormalizePath(PathFileName);

	// 이미 캐시된 StaticMesh가 있으면 그대로 반환
	if (FStaticMesh* CachedMesh = FObjManager::GetFromCache(NormalizedPathStr))
	{
		UE_LOG("[FBX] StaticMesh already cached: %s", NormalizedPathStr.c_str());
		return CachedMesh;
	}

	// 확장자 검사 (.fbx 파일만 허용)
	std::filesystem::path Path(NormalizedPathStr);
	FString Extension = Path.extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (Extension != ".fbx")
	{
		UE_LOG("this file is not fbx!: %s", NormalizedPathStr.c_str());
		return nullptr;
	}

	// 새 StaticMesh 객체 생성
	FStaticMesh* NewStaticMesh = new FStaticMesh();
	NewStaticMesh->PathFileName = NormalizedPathStr;

	TArray<FMaterialInfo> MaterialInfos;
	bool bLoadedSuccessfully = false;

#ifdef USE_OBJ_CACHE
	// 캐시 경로 계산
	FStaticCachePaths CachePaths = GetStaticCachePaths(NormalizedPathStr);
	EnsureCacheDirectory(CachePaths.MeshBinPath);

	// 캐시 최신 여부 확인
	bool bShouldRegenerate = ShouldRegenerateFbxCache(NormalizedPathStr, CachePaths.MeshBinPath, CachePaths.MaterialBinPath);

	if (!bShouldRegenerate)
	{
		// 캐시에서 로드 시도
		bLoadedSuccessfully = TryLoadStaticMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, NewStaticMesh, MaterialInfos);

		if (!bLoadedSuccessfully)
		{
			// 캐시 손상 시 초기화 후 새로 생성
			delete NewStaticMesh;
			NewStaticMesh = new FStaticMesh();
			NewStaticMesh->PathFileName = NormalizedPathStr;
			MaterialInfos.clear();
			RemoveCacheFiles(CachePaths.MeshBinPath, CachePaths.MaterialBinPath);
		}
		else
		{
			UE_LOG("[FBX Cache] Loaded StaticMesh cache: %s", NormalizedPathStr.c_str());
		}
	}
#endif // USE_OBJ_CACHE

	// 캐시 로드 실패 시 FBX 직접 Import
	if (!bLoadedSuccessfully)
	{
		if (!ImporterUtil->BuildStaticMeshFromPath(NormalizedPathStr, NewStaticMesh, MaterialInfos))
		{
			delete NewStaticMesh;
			UE_LOG("[FBX ERROR] Failed to import StaticMesh: %s", NormalizedPathStr.c_str());
			return nullptr;
		}

#ifdef USE_OBJ_CACHE
		// 캐시 저장
		SaveStaticMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, NewStaticMesh, MaterialInfos);
		UE_LOG("[FBX Cache] Saved new StaticMesh cache: %s", NormalizedPathStr.c_str());
#endif

		UE_LOG("[FBX] Generated StaticMesh from FBX: %s", NormalizedPathStr.c_str());
	}

	// 머티리얼 등록
	RegisterMaterialInfos(MaterialInfos);

	// StaticMesh를 ObjManager 캐시에 등록
	FObjManager::AddToCache(NormalizedPathStr, NewStaticMesh);

	return NewStaticMesh;
}

