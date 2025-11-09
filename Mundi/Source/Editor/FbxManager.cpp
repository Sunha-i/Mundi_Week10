#include "pch.h"
#include "FbxManager.h"
#include "ObjectIterator.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"

TMap<FString, FSkeletalMesh*> FFbxManager::CachedAssets;
TMap<FString, USkeletalMesh*> FFbxManager::CachedResources;

static inline FMatrix FbxAMatrixToFMatrix(const FbxAMatrix& A)
{
	// Cast the elements of FbxAMatrix to float and pass them to the FMatrix constructor (row-major)
	return FMatrix(
		(float)A.Get(0, 0), (float)A.Get(0, 1), (float)A.Get(0, 2), (float)A.Get(0, 3),
		(float)A.Get(1, 0), (float)A.Get(1, 1), (float)A.Get(1, 2), (float)A.Get(1, 3),
		(float)A.Get(2, 0), (float)A.Get(2, 1), (float)A.Get(2, 2), (float)A.Get(2, 3),
		(float)A.Get(3, 0), (float)A.Get(3, 1), (float)A.Get(3, 2), (float)A.Get(3, 3)
	);
}

bool FFBXImporter::ImportFBX(const FString& FilePath, FSkeletalMesh* OutMesh, FFBXSkeletonData* OutSkeleton, const FFBXImportOptions& Options)
{
	FbxManager* SDKManager = FbxManager::Create();
	FbxIOSettings* IOSettings = FbxIOSettings::Create(SDKManager, IOSROOT);
	SDKManager->SetIOSettings(IOSettings);

	FbxImporter* Importer = FbxImporter::Create(SDKManager, "");
	if (!Importer->Initialize(FilePath.c_str(), -1, SDKManager->GetIOSettings()))
	{
		UE_LOG("FBX Import Failed: %s\n", Importer->GetStatus().GetErrorString());
		SDKManager->Destroy();
		return false;
	}

	FbxScene* Scene = FbxScene::Create(SDKManager, "Scene");
	Importer->Import(Scene);
	Importer->Destroy();

	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode)
	{
		UE_LOG("FBX has no root node\n");
		SDKManager->Destroy();
		return false;
	}

	// 스켈레톤 파싱
	ParseSkeleton(RootNode, *OutSkeleton);

	// 메시 데이터 파싱
	for (int i = 0; i < RootNode->GetChildCount(); ++i)
	{
		FbxNode* Child = RootNode->GetChild(i);
		if (!Child) continue;

		FbxMesh* Mesh = Child->GetMesh();
		if (!Mesh) continue;

		FFBXMeshData MeshData;
		ParseMesh(Mesh, MeshData);

		// OutMesh는 이미 생성된 FSkeletalMesh*
		OutMesh->PathFileName = FilePath;
		OutMesh->bHasSkinning = true;
		OutMesh->bHasNormals = MeshData.Normals.Num() > 0;

		// Vertex 변환
		OutMesh->Vertices.reserve(MeshData.Positions.Num());
		for (int v = 0; v < MeshData.Positions.Num(); ++v)
		{
			FSkinnedVertex Vtx{};
			Vtx.Position = MeshData.Positions[v];
			Vtx.Normal = (v < MeshData.Normals.Num()) ? MeshData.Normals[v] : FVector(0, 0, 1);
			Vtx.TexCoord = FVector2D(0, 0);

			for (int i = 0; i < 4; ++i)
			{
				int idx = v * 4 + i;
				if (idx < MeshData.BoneIndices.Num())
				{
					Vtx.BoneIndices[i] = MeshData.BoneIndices[idx];
					Vtx.BoneWeights[i] = MeshData.BoneWeights[idx];
				}
			}

			OutMesh->Vertices.push_back(Vtx);
		}

		// 인덱스 & 본 복사
		OutMesh->Indices = MeshData.Indices;
		OutMesh->Bones = OutSkeleton->Bones;

		break; // 한 Mesh만 처리
	}

	SDKManager->Destroy();
	return true;
}

void FFBXImporter::ParseSkeleton(FbxNode* Root, FFBXSkeletonData& OutSkeleton)
{
	OutSkeleton.Bones.Empty();

	std::function<void(FbxNode*, int)> Traverse = [&](FbxNode* Node, int ParentIndex)
	{
		if (!Node)	return;

		FbxSkeleton* Skeleton = Node->GetSkeleton();
		if (Skeleton)
		{
			FBoneInfo Bone;
			Bone.Name = Node->GetName();
			Bone.ParentIndex = ParentIndex;

			FbxAMatrix BindPose = Node->EvaluateGlobalTransform();
			Bone.BindPose = FbxAMatrixToFMatrix(BindPose);
			Bone.InverseBindpose = FbxAMatrixToFMatrix(BindPose.Inverse());

			int BoneIndex = OutSkeleton.Bones.Num();
			OutSkeleton.Bones.Add(Bone);
			ParentIndex = BoneIndex;
		}

		for (int i = 0; i < Node->GetChildCount(); ++i) {
			Traverse(Node->GetChild(i), ParentIndex);
		}
	};

	Traverse(Root, -1);
}

void FFBXImporter::ParseMesh(FbxMesh* Mesh, FFBXMeshData& OutMeshData)
{
	if (!Mesh)	return;

	const int ControlPointCount = Mesh->GetControlPointsCount();
	OutMeshData.Positions.Reserve(ControlPointCount);

	// 1) Position
	for (int i = 0; i < ControlPointCount; ++i)
	{
		FbxVector4 v = Mesh->GetControlPointAt(i);
		OutMeshData.Positions.Add(FVector((float)v[0], (float)v[1], (float)v[2]));
	}

	// 2) Normals (if exists)
	if (Mesh->GetElementNormalCount() > 0)
	{
		const int PolyCount = Mesh->GetPolygonCount();
		OutMeshData.Normals.SetNum(ControlPointCount);

		for (int p = 0; p < PolyCount; ++p)
		{
			const int VertCount = Mesh->GetPolygonSize(p);
			for (int v = 0; v < VertCount; ++v)
			{
				int CtrlIdx = Mesh->GetPolygonVertex(p, v);
				FbxVector4 N;
				if (Mesh->GetPolygonVertexNormal(p, v, N)) {
					OutMeshData.Normals[CtrlIdx] = FVector((float)N[0], (float)N[1], (float)N[2]);
				}
				else {
					OutMeshData.Normals[CtrlIdx] = FVector(0.0f, 0.0f, 1.0f);
				}
			}
		}
	}

	// 3) Index
	const int PolygonCount = Mesh->GetPolygonCount();
	OutMeshData.Indices.Reserve(PolygonCount * 3);

	for (int i = 0; i < PolygonCount; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			int ControlPointIndex = Mesh->GetPolygonVertex(i, j);
			OutMeshData.Indices.Add(ControlPointIndex);
		}
	}

	// 4) Skin/Bone weight
	const int DeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	if (DeformerCount > 0)
	{
		FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(0, FbxDeformer::eSkin);
		const int ClusterCount = Skin->GetClusterCount();

		OutMeshData.BoneIndices.SetNum(ControlPointCount * 4);
		OutMeshData.BoneWeights.SetNum(ControlPointCount * 4);

		for (int c = 0; c < ClusterCount; ++c)
		{
			FbxCluster* Cluster = Skin->GetCluster(c);
			int* Indices = Cluster->GetControlPointIndices();
			double* Weights = Cluster->GetControlPointWeights();
			int Count = Cluster->GetControlPointIndicesCount();

			for (int i = 0; i < Count; ++i)
			{
				int vtx = Indices[i];
				double w = Weights[i];

				for (int slot = 0; slot < 4; ++slot)
				{
					int idx = vtx * 4 + slot;
					if (OutMeshData.BoneWeights[idx] == 0.0f)
					{
						OutMeshData.BoneIndices[idx] = c;
						OutMeshData.BoneWeights[idx] = (float)w;
						break;
					}
				}
			}
		}
	}
}

bool ShouldRegenerateCache(const FString& FbxPath, const FString& BinPath)
{
	namespace fs = std::filesystem;

	// 캐시 파일이 없으면 무조건 재생성
	if (!fs::exists(BinPath))
	{
		return true;
	}

	try
	{
		auto BinTimestamp = fs::last_write_time(BinPath);

		// 원본 FBX가 더 최신이면 캐시 무효
		if (fs::exists(FbxPath) && fs::last_write_time(FbxPath) > BinTimestamp)
		{
			return true;
		}
	}
	catch (const fs::filesystem_error& e)
	{
		UE_LOG("Filesystem error during FBX cache validation: %s. Forcing regeneration.", e.what());
		return true;
	}

	// 최신 캐시가 유지되고 있으면 그대로 사용
	return false;
}

void FFbxManager::Preload()
{
	const fs::path DataDir(GDataDir);

	if (!fs::exists(DataDir) || !fs::is_directory(DataDir))
	{
		UE_LOG("FFbxManager::Preload: Data directory not found: %s", DataDir.string().c_str());
		return;
	}

	size_t LoadedCount = 0;
	std::unordered_set<FString> ProcessedFiles; // 중복 로딩 방지

	for (const auto& Entry : fs::recursive_directory_iterator(DataDir))
	{
		if (!Entry.is_regular_file())
			continue;

		const fs::path& Path = Entry.path();
		FString Extension = Path.extension().string();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (Extension == ".fbx")
		{
			FString PathStr = NormalizePath(Path.string());

			// 이미 처리된 파일인지 확인
			if (ProcessedFiles.find(PathStr) == ProcessedFiles.end())
			{
				ProcessedFiles.insert(PathStr);
				LoadFbxSkeletalMesh(PathStr);
				++LoadedCount;
			}
		}
	}

	// 모든 SkeletalMeshes 가져오기
	RESOURCE.SetSkeletalMeshes();

	UE_LOG("FFbxManager::Preload: Loaded %zu .obj files from %s", LoadedCount, DataDir.string().c_str());
}

void FFbxManager::Clear()
{
	for (auto& Pair : CachedResources)
	{
		delete Pair.second;
	}
	CachedResources.Empty();

	for (auto& Pair : CachedAssets)
	{
		delete Pair.second;
	}
	CachedAssets.Empty();
}

FSkeletalMesh* FFbxManager::LoadFbxSkeletalMeshAsset(const FString& PathFileName)
{
	FString NormalizedPathStr = NormalizePath(PathFileName);

	// 1. 메모리 캐시 확인: 이미 로드된 에셋이 있으면 즉시 반환합니다.
	if (FSkeletalMesh** It = CachedAssets.Find(NormalizedPathStr))
	{
		return *It;
	}

	// 2. 파일 경로 설정
	std::filesystem::path Path(NormalizedPathStr);
	FString Extension = Path.extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (Extension != ".fbx")
	{
		UE_LOG("this file is not fbx!: %s", NormalizedPathStr.c_str());
		return nullptr;
	}

#ifdef USE_FBX_CACHE
	// 2-1. 캐시 파일 경로 설정
	FString CachePathStr = ConvertDataPathToCachePath(NormalizedPathStr);
	const FString BinPathFileName = CachePathStr + ".bin";

	// 캐시를 저장할 디렉토리가 없으면 생성
	fs::path CacheFileDirPath(BinPathFileName);
	if (CacheFileDirPath.has_parent_path())
	{
		fs::create_directories(CacheFileDirPath.parent_path());
	}

	// 3. 캐시 데이터 로드 시도 및 실패 시 재생성 로직
	FSkeletalMesh* NewSkeletalMesh = new FSkeletalMesh();
	bool bLoadedSuccessfully = false;

	// 캐시가 오래되었는지 먼저 확인
	bool bShouldRegenerate = ShouldRegenerateCache(NormalizedPathStr, BinPathFileName);

	if (!bShouldRegenerate)
	{
		UE_LOG("Attempting to load '%s' from cache.", NormalizedPathStr.c_str());
		try
		{
			FWindowsBinReader Reader(BinPathFileName);
			if (!Reader.IsOpen())
				throw std::runtime_error("Failed to open FBX bin file.");

			Reader << *NewSkeletalMesh;
			Reader.Close();

			NewSkeletalMesh->CacheFilePath = BinPathFileName;
			bLoadedSuccessfully = true;
			UE_LOG("Successfully loaded skeletal mesh '%s' from cache.", NormalizedPathStr.c_str());
		}
		catch (const std::exception& e)
		{
			UE_LOG("Error loading FBX cache: %s", e.what());
			delete NewSkeletalMesh;
			NewSkeletalMesh = nullptr;
			std::filesystem::remove(BinPathFileName);
			bLoadedSuccessfully = false;
		}
	}
#else
	FSkeletalMesh* NewSkeletalMesh = new FSkeletalMesh();
	bool bLoadedSuccessfully = false;
#endif

	// 4. 캐시 로드 실패 시 새로 생성
	if (!bLoadedSuccessfully)
	{
		if (!NewSkeletalMesh) NewSkeletalMesh = new FSkeletalMesh();
		UE_LOG("Regenerating FBX cache for '%s'...", NormalizedPathStr.c_str());

		// FBX Import
		FFBXSkeletonData OutSkeleton;
		FFBXImportOptions Options;
		if (!FFBXImporter::ImportFBX(NormalizedPathStr, NewSkeletalMesh, &OutSkeleton, Options))
		{
			UE_LOG("Failed to import FBX: %s", NormalizedPathStr.c_str());
			delete NewSkeletalMesh;
			return nullptr;
		}

#ifdef USE_FBX_CACHE
		// 성공 시 캐시 저장
		try
		{
			FWindowsBinWriter Writer(BinPathFileName);
			Writer << *NewSkeletalMesh;
			Writer.Close();
			UE_LOG("Cache regeneration complete for '%s'.", NormalizedPathStr.c_str());
		}
		catch (const std::exception& e)
		{
			UE_LOG("Failed to write FBX cache: %s", e.what());
		}
#endif
	}

	// 5. 로드 완료 로그
	UE_LOG("Loaded skeletal mesh '%s' with %d bones and %d vertices.",
		NormalizedPathStr.c_str(),
		static_cast<int>(NewSkeletalMesh->Bones.size()),
		static_cast<int>(NewSkeletalMesh->Vertices.size()));

	// 6. 메모리 캐시에 등록
	CachedAssets.Add(NormalizedPathStr, NewSkeletalMesh);
	return NewSkeletalMesh;
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

	// 2) 없으면 새로 로드(정규화된 경로 사용)
	USkeletalMesh* SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(NormalizedPathStr);

	UE_LOG("USkeletalMesh(filename: '%s') is successfully created!", NormalizedPathStr.c_str());
	return SkeletalMesh;
}

bool FFbxManager::IsSkeletalMesh(FbxMesh* Mesh)
{
	const int DeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	return (DeformerCount > 0);
}
