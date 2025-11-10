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

FFbxManager::FFbxManager()
{
	Initialize();
}

FFbxManager::~FFbxManager()
{
	Shutdown();
}

void FFbxManager::Initialize()
{// SDK 관리자 초기화. 이 객체는 메모리 관리를 처리함.\n
	// SDK 관리자 초기화. 이 객체는 메모리 관리를 처리함.
	SdkManager = FbxManager::Create();

	ImporterUtil = new FFbxImporter(SdkManager);
	// IO 설정 객체를 생성한다.
	ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(ios);

	// SDK 관리자를 사용하여 Importer를 생성한다.
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
}

void FFbxManager::Shutdown()
{
	for (
		auto Iter = FbxSkeletalMeshMap.begin();
		Iter != FbxSkeletalMeshMap.end();
		Iter++
		)
	{
		if (Iter->second)
			delete Iter->second;
	}
	// SDK 관리자와 그것이 처리하던 다른 모든 객체를 소멸시킨다.
	if(ImporterUtil){ delete ImporterUtil; ImporterUtil=nullptr;}
	SdkManager->Destroy();
}

FFbxManager& FFbxManager::GetInstance()
{
	static FFbxManager FbxManager;
	return FbxManager;
}

void FFbxManager::Preload()
{
    UE_LOG("==========================================");
    UE_LOG("FFbxManager::Preload STARTED");
    UE_LOG("==========================================");

    const fs::path FbxDir(GFbxDataDir);
    UE_LOG("[FBX Preload] GFbxDataDir = %s", GFbxDataDir.c_str());

    if (!fs::exists(FbxDir) || !fs::is_directory(FbxDir))
    {
        UE_LOG("[FBX Preload ERROR] FBX directory not found: %s", FbxDir.string().c_str());
        return;
    }

    UE_LOG("[FBX Preload] Directory exists, scanning recursively...");

    size_t LoadedCount = 0;
    size_t TotalFilesScanned = 0;
    std::unordered_set<FString> ProcessedFiles; // 중복 로딩 방지

    UE_LOG("[FBX Preload] Starting recursive directory scan...");


    try
    {
        UE_LOG("[FBX Preload] Starting recursive_directory_iterator...");

        for (const auto& Entry : fs::recursive_directory_iterator(FbxDir))
        {
            TotalFilesScanned++;

            if (!Entry.is_regular_file())
            {
                continue;
            }

            const fs::path& Path = Entry.path();
            FString Extension = Path.extension().string();
            std::transform(Extension.begin(), Extension.end(), Extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (Extension == ".fbx")
            {
                FString PathStr = NormalizePath(Path.string());
                UE_LOG("[FBX Preload] Found FBX file: %s", PathStr.c_str());

                // 이미 처리된 파일인지 확인
                if (ProcessedFiles.find(PathStr) == ProcessedFiles.end())
                {
                    ProcessedFiles.insert(PathStr);
                    UE_LOG("[FBX Preload] Loading: %s", PathStr.c_str());

                    // FSkeletalMesh Asset 로드
                    FSkeletalMesh* SkeletalMeshAsset = LoadFbxSkeletalMeshAsset(PathStr);

                    // Skeleton이 있으면 USkeletalMesh 생성 및 ResourceManager 등록
                    if (SkeletalMeshAsset && SkeletalMeshAsset->Skeleton)
                    {
                        USkeletalMesh* USkeletalMeshObj = NewObject<USkeletalMesh>();
                        USkeletalMeshObj->SetFilePath(PathStr);
                        USkeletalMeshObj->Load(PathStr, GEngine.GetRHIDevice()->GetDevice());
                        UResourceManager::GetInstance().Add<USkeletalMesh>(PathStr, USkeletalMeshObj);
                        UE_LOG("[FBX Preload] Registered USkeletalMesh to ResourceManager: %s", PathStr.c_str());
                    }
                    // Skeleton이 없으면 이미 StaticMesh로 변환되어 ResourceManager에 등록됨

                    ++LoadedCount;
                }
                else
                {
                    UE_LOG("[FBX Preload] Skipped (already processed): %s", PathStr.c_str());
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        UE_LOG("[FBX Preload ERROR] Exception during scan: %s", e.what());
        return;
    }

    UE_LOG("[FBX Preload] Scan complete. Total files scanned: %zu", TotalFilesScanned);

    UE_LOG("==========================================");
    UE_LOG("FFbxManager::Preload FINISHED");
    UE_LOG("Loaded %zu .fbx files from %s", LoadedCount, FbxDir.string().c_str());
    UE_LOG("==========================================");
}

void FFbxManager::Clear()
{
	for (auto Iter = FbxSkeletalMeshMap.begin(); Iter != FbxSkeletalMeshMap.end(); Iter++)
	{
		delete Iter->second;
	}

	FbxSkeletalMeshMap.clear();
}
// =====================================
// FFbxManager::LoadFbxSkeletalMeshAsset (메인 흐름)
// =====================================
FSkeletalMesh* FFbxManager::LoadFbxSkeletalMeshAsset(const FString& PathFileName)
{
	FString NormalizedPathStr = NormalizePath(PathFileName);
	if (auto Cached = FbxSkeletalMeshMap.Find(NormalizedPathStr))
		return *Cached;

	if (!ValidateFbxFile(NormalizedPathStr))
		return nullptr;

#ifdef USE_OBJ_CACHE
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
				UE_LOG("[FBX Cache] Loaded SkeletalMesh cache: %s", NormalizedPathStr.c_str());
				return CachedMesh;
			}

			delete CachedMesh;
			RemoveCacheFiles(CachePaths.MeshBinPath, CachePaths.MaterialBinPath);
		}
	}
#endif

    FbxScene* Scene = ImporterUtil->ImportFbxScene( NormalizedPathStr);
	if (!Scene)
		return nullptr;

    TMap<int64, FMaterialInfo> MaterialMap;
    TArray<FMaterialInfo> MaterialInfos;
    ImporterUtil->CollectMaterials(Scene, MaterialMap, MaterialInfos, NormalizedPathStr);

	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode)
	{
		UE_LOG("[FBX] RootNode not found: %s", NormalizedPathStr.c_str());
		Scene->Destroy();
		return nullptr;
	}

    UBone* RootBone = ImporterUtil->FindSkeletonRootAndBuild(RootNode);
	FSkeletalMesh* SkeletalMesh = nullptr;

	if (RootBone)
	{
		SkeletalMesh = BuildSkeletalMesh(Scene, RootNode, RootBone, MaterialMap, NormalizedPathStr);
		RegisterMaterialInfos(MaterialInfos);

#ifdef USE_OBJ_CACHE
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
		BuildStaticMeshFromScene(Scene, MaterialMap, MaterialInfos, NormalizedPathStr);
	}

	Scene->Destroy();
	return SkeletalMesh;
}

void FFbxManager::BuildStaticMeshFromScene(FbxScene* Scene, const TMap<int64, FMaterialInfo>& MaterialMap, const TArray<FMaterialInfo>& MaterialInfos, const FString& Path)
{
	UE_LOG("[FBX] No skeleton detected - loading as StaticMesh: %s", Path.c_str());

	FStaticMesh* NewStatic = new FStaticMesh();
	NewStatic->PathFileName = Path;

	FbxNode* Root = Scene->GetRootNode();
	ImporterUtil->ProcessMeshNodeAsStatic(Root, NewStatic, MaterialMap);

RegisterMaterialInfos(MaterialInfos);

#ifdef USE_OBJ_CACHE
FStaticCachePaths CachePaths = GetStaticCachePaths(Path);
EnsureCacheDirectory(CachePaths.MeshBinPath);
SaveStaticMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, NewStatic, MaterialInfos);
#endif

	// 리소스 등록
	UStaticMesh* UStatic = NewObject<UStaticMesh>();
	UStatic->SetFilePath(Path);
	UStatic->Load(Path, GEngine.GetRHIDevice()->GetDevice());
	UResourceManager::GetInstance().Add<UStaticMesh>(Path, UStatic);

	FObjManager::AddToCache(Path, NewStatic);

	UE_LOG("[FBX] Converted to StaticMesh and registered: %s", Path.c_str());
}

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

	UE_LOG("[FBX] Built SkeletalMesh: %s", Path.c_str());
	return NewMesh;
}

bool FFbxManager::ValidateFbxFile(const FString& Path)
{
	std::filesystem::path FilePath(Path);
	FString Extension = FilePath.extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char c) { return std::tolower(c); });

	if (Extension != ".fbx")
	{
		UE_LOG("Invalid FBX file: %s", Path.c_str());
		return false;
	}
	return true;
}

USkeletalMesh* FFbxManager::LoadFbxSkeletalMesh(const FString& PathFileName)
{
	// 0) 경로
	FString NormalizedPathStr = NormalizePath(PathFileName);

	// 1) 이미 로드된 USkeletalMesh가 있는지 전체 검색 (정규화된 경로로 비교)
	for (TObjectIterator<USkeletalMesh> It; It; ++It)
	{
		USkeletalMesh* SkeletalMesh = *It;

		if (SkeletalMesh->GetFilePath() == NormalizedPathStr)
		{
			return SkeletalMesh;
		}
	}

	// 2) 없으면 새로 로드 (정규화된 경로 사용)
	USkeletalMesh* SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(NormalizedPathStr);

	UE_LOG("USkeletalMesh(filename: \'%s\') is successfully created!", NormalizedPathStr.c_str());
	return SkeletalMesh;
}

// Skeleton이 없는 FBX를 StaticMesh로 로드하고 ObjManager 캐시에 저장
bool FFbxManager::BuildStaticMeshFromFbx(const FString& NormalizedPathStr, FStaticMesh* OutStaticMesh, TArray<FMaterialInfo>& OutMaterialInfos)
{
	if (!OutStaticMesh)
	{
		return false;
	}

	FbxImporter* LocalImporter = FbxImporter::Create(SdkManager, "");
	if (!LocalImporter->Initialize(NormalizedPathStr.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Failed to initialize FBX Importer for StaticMesh: %s", NormalizedPathStr.c_str());
		LocalImporter->Destroy();
		return false;
	}

	FbxScene* Scene = FbxScene::Create(SdkManager, "StaticMeshScene");
	if (!LocalImporter->Import(Scene))
	{
		UE_LOG("Failed to import FBX scene for StaticMesh: %s", NormalizedPathStr.c_str());
		LocalImporter->Destroy();
		Scene->Destroy();
		return false;
	}
	LocalImporter->Destroy();

	OutStaticMesh->Vertices.clear();
	OutStaticMesh->Indices.clear();
	OutStaticMesh->GroupInfos.clear();
	OutStaticMesh->bHasMaterial = false;
	OutStaticMesh->PathFileName = NormalizedPathStr;

	TMap<int64, FMaterialInfo> MaterialIDToInfoMap;
	int MaterialCount = Scene->GetMaterialCount();
	std::filesystem::path FbxDir = std::filesystem::path(NormalizedPathStr).parent_path();

	auto ExtractTexturePath = [&FbxDir](FbxProperty& Prop) -> FString
		{
			FString TexturePath = "";
			int FileTextureCount = Prop.GetSrcObjectCount<FbxFileTexture>();
			if (FileTextureCount > 0)
			{
				FbxFileTexture* FileTexture = Prop.GetSrcObject<FbxFileTexture>(0);
				if (FileTexture)
				{
					const char* FileName = FileTexture->GetFileName();
					if (FileName && strlen(FileName) > 0)
					{
						TexturePath = FileName;
						std::filesystem::path TexPath(TexturePath);
						if (!std::filesystem::exists(TexPath))
						{
							std::filesystem::path RelativePath = FbxDir / TexPath.filename();
							if (std::filesystem::exists(RelativePath))
							{
								TexturePath = RelativePath.string();
							}
						}
						TexturePath = NormalizePath(TexturePath);
					}
				}
			}
			return TexturePath;
		};

	for (int i = 0; i < MaterialCount; i++)
	{
		FbxSurfaceMaterial* Material = Scene->GetMaterial(i);
		if (!Material)
			continue;

		FMaterialInfo MatInfo;
		MatInfo.MaterialName = Material->GetName();
		int64 MaterialID = Material->GetUniqueID();

		FbxProperty DiffuseProp = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
		if (DiffuseProp.IsValid())
		{
			FbxDouble3 DiffuseColor = DiffuseProp.Get<FbxDouble3>();
			MatInfo.DiffuseColor = FVector(static_cast<float>(DiffuseColor[0]),
				static_cast<float>(DiffuseColor[1]),
				static_cast<float>(DiffuseColor[2]));
			MatInfo.DiffuseTextureFileName = ExtractTexturePath(DiffuseProp);
		}

		MaterialIDToInfoMap.Add(MaterialID, MatInfo);
	}

	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		ImporterUtil->ProcessMeshNodeAsStatic(RootNode, OutStaticMesh, MaterialIDToInfoMap);
	}
	else
	{
		UE_LOG("Failed to locate root node while building static mesh: %s", NormalizedPathStr.c_str());
	}

	Scene->Destroy();

	OutMaterialInfos.clear();
	OutMaterialInfos.reserve(MaterialIDToInfoMap.size());
	for (auto& Pair : MaterialIDToInfoMap)
	{
		OutMaterialInfos.Add(Pair.second);
	}

	return true;
}

FStaticMesh* FFbxManager::LoadFbxStaticMeshAsset(const FString& PathFileName)
{
	FString NormalizedPathStr = NormalizePath(PathFileName);

	if (FStaticMesh* CachedMesh = FObjManager::GetFromCache(NormalizedPathStr))
	{
		UE_LOG("[FBX] StaticMesh already cached: %s", NormalizedPathStr.c_str());
		return CachedMesh;
	}

	std::filesystem::path Path(NormalizedPathStr);
	FString Extension = Path.extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (Extension != ".fbx")
	{
		UE_LOG("this file is not fbx!: %s", NormalizedPathStr.c_str());
		return nullptr;
	}

	FStaticMesh* NewStaticMesh = new FStaticMesh();
	NewStaticMesh->PathFileName = NormalizedPathStr;
	TArray<FMaterialInfo> MaterialInfos;
	bool bLoadedSuccessfully = false;

#ifdef USE_OBJ_CACHE
FStaticCachePaths CachePaths = GetStaticCachePaths(NormalizedPathStr);
EnsureCacheDirectory(CachePaths.MeshBinPath);
bool bShouldRegenerate = ShouldRegenerateFbxCache(NormalizedPathStr, CachePaths.MeshBinPath, CachePaths.MaterialBinPath);

	if (!bShouldRegenerate)
	{
	bLoadedSuccessfully = TryLoadStaticMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, NewStaticMesh, MaterialInfos);
		if (!bLoadedSuccessfully)
		{
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

	if (!bLoadedSuccessfully)
	{
		if (!BuildStaticMeshFromFbx(NormalizedPathStr, NewStaticMesh, MaterialInfos))
		{
			delete NewStaticMesh;
			return nullptr;
		}

#ifdef USE_OBJ_CACHE
		SaveStaticMeshCache(CachePaths.MeshBinPath, CachePaths.MaterialBinPath, NewStaticMesh, MaterialInfos);
#endif
		UE_LOG("[FBX] Generated StaticMesh from FBX: %s", NormalizedPathStr.c_str());
	}

	RegisterMaterialInfos(MaterialInfos);
	FObjManager::AddToCache(NormalizedPathStr, NewStaticMesh);
	return NewStaticMesh;
}