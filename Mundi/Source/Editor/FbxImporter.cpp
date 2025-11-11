#include "pch.h"
#include "FbxImporter.h"
#include "Bone.h"
#include "SkeletalMeshStruct.h"
#include "StaticMesh.h"

#include <fbxsdk/utils/fbxrootnodeutility.h>

// ============================================================================
// [FBX 임포트] Scene 불러오기
// ============================================================================
FbxScene* FFbxImporter::ImportFbxScene(const FString& Path)
{
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer->Initialize(Path.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Failed to initialize FBX importer: %s", Path.c_str());
		UE_LOG("Error: %s", Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		return nullptr;
	}

	FbxScene* Scene = FbxScene::Create(SdkManager, "ImportScene");
	if (!Importer->Import(Scene))
	{
		UE_LOG("Failed to import FBX scene: %s", Path.c_str());
		Scene->Destroy();
		Importer->Destroy();
		return nullptr;
	}

	Importer->Destroy();

	FbxAxisSystem SourceAxis = Scene->GetGlobalSettings().GetAxisSystem();
	FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eLeftHanded;
	FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
	FbxAxisSystem::EFrontVector FrontVector = FbxAxisSystem::eParityEven;
	FbxAxisSystem TargetAxis(UpVector, FrontVector, CoordSystem);
	if (SourceAxis != TargetAxis)
	{
		FbxRootNodeUtility::RemoveAllFbxRoots(Scene);
		TargetAxis.DeepConvertScene(Scene);
		Scene->GetGlobalSettings().SetAxisSystem(TargetAxis);
	}

	return Scene;
}
// ============================================================================
// [FBX 머티리얼 파싱] 스태틱/스켈레탈 공용
// ============================================================================
void FFbxImporter::CollectMaterials(FbxScene* Scene, const FString& Path,
	TMap<int64, FMaterialInfo>& OutMatMap, TArray<FMaterialInfo>& OutInfos)
{
	if (!Scene) return;

	std::filesystem::path FbxDir = std::filesystem::path(Path).parent_path();

	auto ExtractTexturePath = [&FbxDir](FbxProperty& Prop) -> FString
		{
			FString TexPath = "";
			if (auto* FileTex = Prop.GetSrcObject<FbxFileTexture>(0))
			{
				TexPath = NormalizePath(FString(FileTex->GetFileName()));
				if (!std::filesystem::exists(TexPath))
				{
					std::filesystem::path Relative = FbxDir / std::filesystem::path(TexPath).filename();
					if (std::filesystem::exists(Relative))
						TexPath = NormalizePath(Relative.string());
				}
			}
			return TexPath;
		};

	const int MatCount = Scene->GetMaterialCount();
	for (int i = 0; i < MatCount; i++)
	{
		FbxSurfaceMaterial* Mat = Scene->GetMaterial(i);
		if (!Mat) continue;

		FMaterialInfo Info;
		Info.MaterialName = Mat->GetName();
		int64 MatID = Mat->GetUniqueID();

		if (auto DiffProp = Mat->FindProperty(FbxSurfaceMaterial::sDiffuse); DiffProp.IsValid())
		{
			FbxDouble3 Color = DiffProp.Get<FbxDouble3>();
			Info.DiffuseColor = FVector((float)Color[0], (float)Color[1], (float)Color[2]);
			Info.DiffuseTextureFileName = ExtractTexturePath(DiffProp);
		}

		OutMatMap.Add(MatID, Info);
		OutInfos.Add(Info);
	}
}

// ============================================================================
// [FBX 변환행렬 → 엔진 Transform]
// ============================================================================
FTransform FFbxImporter::ConvertFbxTransform(const FbxAMatrix& InMatrix)
{
	using namespace FBXUtil;

	FTransform Result;
	FbxVector4 T = InMatrix.GetT();
	FbxQuaternion R = InMatrix.GetQ();
	FbxVector4 S = InMatrix.GetS();

	Result.Translation = FBXUtil::ConvertPosition(T);
	Result.Rotation = FQuat(
		static_cast<float>(R[0]),
		static_cast<float>(R[1]),
		static_cast<float>(R[2]),
		static_cast<float>(R[3]));
	Result.Scale3D = FVector(static_cast<float>(S[0]), static_cast<float>(S[1]), static_cast<float>(S[2]));
	return Result;
}

// ============================================================================
// [공통 메쉬 추출기] Static / Skeletal 겸용
// ============================================================================
template<typename MeshType>
void ExtractCommonMeshData(
	FbxMesh* InMesh,
	MeshType* OutMesh,
	const TMap<int64, FMaterialInfo>& MatMap,
	bool bSkeletal,
	std::function<void(FFlesh&)> OnAfterSection = nullptr 
)
{
	using namespace FBXUtil;
	if (!InMesh || !OutMesh)
		return;

	const FbxVector4* ControlPoints = InMesh->GetControlPoints();
	const int PolygonCount = InMesh->GetPolygonCount();

	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;
	TMap<int, TArray<int>> MaterialToTriangles;
	TMap<int, TArray<uint32>> PolyToVertIdx;

	// ----- [정점 데이터] -----
	for (int PolyIdx = 0; PolyIdx < PolygonCount; ++PolyIdx)
	{
		int PolySize = InMesh->GetPolygonSize(PolyIdx);
		if (PolySize < 3) continue;
		int NumTri = PolySize - 2;

		for (int TriIdx = 0; TriIdx < NumTri; ++TriIdx)
		{
			TArray<uint32> TriVertIdx;

			int V0 = 0, V1 = TriIdx + 1, V2 = TriIdx + 2;
			for (int LocalVertIdx : { V0, V1, V2 })
			{
				int CPIdx = InMesh->GetPolygonVertex(PolyIdx, LocalVertIdx);
				FNormalVertex V{};
				V.pos = ConvertPosition(ControlPoints[CPIdx]);

				FbxVector4 Normal;
				if (InMesh->GetPolygonVertexNormal(PolyIdx, LocalVertIdx, Normal))
					V.normal = ConvertNormal(Normal);

				// UV
				FbxStringList UVSets;
				InMesh->GetUVSetNames(UVSets);
				if (UVSets.GetCount() > 0)
				{
					FbxVector2 UV; bool bUnmapped;
					if (InMesh->GetPolygonVertexUV(PolyIdx, LocalVertIdx, UVSets[0], UV, bUnmapped))
						V.tex = FVector2D((float)UV[0], 1.0f - (float)UV[1]);
				}

				// Tangent
				V.Tangent = DefaultTangent();

				uint32 Idx = (uint32)Vertices.size();
				Vertices.Add(V);
				TriVertIdx.Add(Idx);
			}
			PolyToVertIdx.Add(PolyIdx * 100 + TriIdx, TriVertIdx);
		}
	}

	// ----- [머티리얼별 그룹 분할] -----
	FbxLayerElementMaterial* MatElem = InMesh->GetElementMaterial();
	if (MatElem)
	{
		for (int PolyIdx = 0; PolyIdx < PolygonCount; ++PolyIdx)
		{
			int PolySize = InMesh->GetPolygonSize(PolyIdx);
			if (PolySize < 3) continue;
			int MatIndex = 0;

			if (MatElem->GetMappingMode() == FbxLayerElement::eByPolygon)
			{
				if (MatElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
					MatIndex = MatElem->GetIndexArray().GetAt(PolyIdx);
			}

			if (!MaterialToTriangles.Contains(MatIndex))
				MaterialToTriangles.Add(MatIndex, TArray<int>());

			for (int TriIdx = 0; TriIdx < PolySize - 2; ++TriIdx)
				MaterialToTriangles[MatIndex].Add(PolyIdx * 100 + TriIdx);
		}
	}

	// ----- [섹션 생성] -----
	uint32 BaseVertexOffset = (uint32)OutMesh->Vertices.size();
	uint32 BaseIndexOffset = (uint32)OutMesh->Indices.size();
	uint32 CurStartIndex = BaseIndexOffset;

	TArray<int> SortedMatIndices;
	for (auto& P : MaterialToTriangles) SortedMatIndices.Add(P.first);
	SortedMatIndices.Sort();

	for (int MatIndex : SortedMatIndices)
	{
		FFlesh Section;
		Section.StartIndex = CurStartIndex;
		Section.IndexCount = 0;

		for (int Key : MaterialToTriangles[MatIndex])
		{
			const auto* Arr = PolyToVertIdx.Find(Key);
			if (!Arr) continue;
			OutMesh->Indices.insert(OutMesh->Indices.end(), {
				(*Arr)[0] + BaseVertexOffset,
				(*Arr)[1] + BaseVertexOffset,
				(*Arr)[2] + BaseVertexOffset
				});
			Section.IndexCount += 3;
		}

		// 머티리얼 이름
		if (MatIndex < InMesh->GetNode()->GetMaterialCount())
		{
			FbxSurfaceMaterial* Mat = InMesh->GetNode()->GetMaterial(MatIndex);
			if (Mat)
			{
				int64 MatID = Mat->GetUniqueID();
				if (const auto* Found = MatMap.Find(MatID))
					Section.InitialMaterialName = Found->MaterialName;
				else
					Section.InitialMaterialName = Mat->GetName();
			}
		}
		else Section.InitialMaterialName = "DefaultMaterial";

		if (OnAfterSection) OnAfterSection(Section); // 스킨 처리용 콜백
		if constexpr (std::is_same_v<MeshType, FStaticMesh>)
			OutMesh->GroupInfos.Add((FGroupInfo&)Section);
		else
			OutMesh->Fleshes.Add(Section);

		CurStartIndex += Section.IndexCount;
	}

	OutMesh->Vertices.insert(OutMesh->Vertices.end(), Vertices.begin(), Vertices.end());
	OutMesh->bHasMaterial |= (InMesh->GetElementMaterialCount() > 0);
}

// ============================================================================
// [FBX 본 스키닝 정보 추출]
// ============================================================================
void FFbxImporter::ExtractSkinningData(FbxMesh* InMesh, FFlesh& OutFlesh, const TMap<FString, UBone*>& BoneMap)
{
	int SkinCount = InMesh->GetDeformerCount(FbxDeformer::eSkin);
	if (SkinCount == 0) return;

	FbxSkin* Skin = static_cast<FbxSkin*>(InMesh->GetDeformer(0, FbxDeformer::eSkin));
	int ClusterCount = Skin->GetClusterCount();

	for (int i = 0; i < ClusterCount; ++i)
	{
		FbxCluster* Cl = Skin->GetCluster(i);
		if (!Cl || !Cl->GetLink()) continue;

		FString BoneName(Cl->GetLink()->GetName());
		if (auto* Found = BoneMap.Find(BoneName))
		{
			OutFlesh.Bones.Add(*Found);
			double* W = Cl->GetControlPointWeights();
			int Cnt = Cl->GetControlPointIndicesCount();

			float Avg = 0.0f;
			for (int j = 0; j < Cnt; ++j) Avg += (float)W[j];
			OutFlesh.Weights.Add(Cnt > 0 ? Avg / Cnt : 0.f);
		}
	}
}

// ============================================================================
// [FBX StaticMesh 로드]
// ============================================================================
bool FFbxImporter::BuildStaticMeshFromPath(const FString& Path, FStaticMesh* OutStatic, TArray<FMaterialInfo>& OutMats)
{
	if (!OutStatic) return false;
	FbxScene* Scene = ImportFbxScene(Path);
	if (!Scene) return false;

	TMap<int64, FMaterialInfo> MatMap;
	CollectMaterials(Scene, Path, MatMap, OutMats);

	FbxNode* Root = Scene->GetRootNode();
	if (Root)
	{
		ProcessMeshNodeAsStatic(Root, OutStatic, MatMap);
	}

	Scene->Destroy();
	return true;
}

// ============================================================================
// [FBX Mesh 처리 - 스태틱용]
// ============================================================================
void FFbxImporter::ProcessMeshNodeAsStatic(FbxNode* Node, FStaticMesh* OutMesh, const TMap<int64, FMaterialInfo>& MatMap)
{
	if (!Node || !OutMesh) return;
	if (auto* Attr = Node->GetNodeAttribute())
	{
		if (Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			ExtractCommonMeshData(Node->GetMesh(), OutMesh, MatMap, false);
		}
	}
	for (int i = 0; i < Node->GetChildCount(); ++i)
		ProcessMeshNodeAsStatic(Node->GetChild(i), OutMesh, MatMap);
}

// ============================================================================
// [FBX Skeleton Root 찾기 및 계층 생성]
// ============================================================================
UBone* FFbxImporter::FindSkeletonRootAndBuild(FbxNode* RootNode)
{
	if (!RootNode)
		return nullptr;

	// 내부 재귀 람다 (DFS 방식)
	std::function<UBone*(FbxNode*)> FindRecursively = [&](FbxNode* Node) -> UBone*
	{
		if (!Node)
			return nullptr;

		// 현재 노드가 Skeleton이면 바로 처리
		if (auto* Attr = Node->GetNodeAttribute())
		{
			if (Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
			{
				UE_LOG("Found skeleton root: %s", Node->GetName());
				return ProcessSkeletonNode(Node);
			}
		}

		// 자식 노드 순회 (깊이 우선 탐색)
		for (int i = 0; i < Node->GetChildCount(); ++i)
		{
			if (UBone* Found = FindRecursively(Node->GetChild(i)))
				return Found; // 첫 번째 스켈레톤 발견 시 반환
		}

		return nullptr;
	};

	return FindRecursively(RootNode);
}


// ============================================================================
// [FBX Skeleton 노드 재귀 생성]
// ============================================================================
UBone* FFbxImporter::ProcessSkeletonNode(FbxNode* InNode, UBone* InParent)
{
	if (!InNode)
		return nullptr;

	FbxAMatrix LocalTransform = InNode->EvaluateLocalTransform();
	FTransform BoneTransform = ConvertFbxTransform(LocalTransform);

	FName BoneName(InNode->GetName());
	UBone* NewBone = new UBone(BoneName, BoneTransform);
	ObjectFactory::AddToGUObjectArray(UBone::StaticClass(), NewBone);
	NewBone->SetRelativeBindPoseTransform(BoneTransform);

	if (InParent)
	{
		InParent->AddChild(NewBone);
		NewBone->SetParent(InParent);
	}

	for (int i = 0; i < InNode->GetChildCount(); i++)
	{
		FbxNode* ChildNode = InNode->GetChild(i);
		FbxNodeAttribute* Attr = ChildNode->GetNodeAttribute();
		if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			ProcessSkeletonNode(ChildNode, NewBone);
		}
	}

	return NewBone;
}

// ============================================================================
// [FBX Mesh 처리 - 스켈레탈용]
// ============================================================================
void FFbxImporter::ProcessMeshNode(
	FbxNode* InNode,
	FSkeletalMesh* OutSkeletalMesh,
	const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap)
{
	if (!InNode || !OutSkeletalMesh)
		return;

	FbxNodeAttribute* Attr = InNode->GetNodeAttribute();
	if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		FbxMesh* Mesh = InNode->GetMesh();
		if (Mesh)
		{
			// 기존과 동일: BoneMap을 지역으로 구성
			TMap<FString, UBone*> BoneMap;
			if (OutSkeletalMesh->Skeleton)
			{
				OutSkeletalMesh->Skeleton->ForEachBone([&BoneMap](UBone* Bone)
					{
						if (Bone)
						{
							FString BoneName = Bone->GetName().ToString();
							BoneMap.Add(BoneName, Bone);
						}
					});
			}

			// 공통 메시 데이터 추출 (스켈레탈 모드 true)
			ExtractCommonMeshData(Mesh, OutSkeletalMesh, MaterialIDToInfoMap, true,
				[&](FFlesh& FleshSection)
				{
					//  원래 코드와 동일한 방식으로 스킨 데이터 추출
					ExtractSkinningData(Mesh, FleshSection, BoneMap);
				});
		}
	}
	//  자식 노드 재귀 처리
	const int ChildCount = InNode->GetChildCount();
	for (int i = 0; i < ChildCount; ++i)
	{
		ProcessMeshNode(InNode->GetChild(i), OutSkeletalMesh, MaterialIDToInfoMap);
	}
}
