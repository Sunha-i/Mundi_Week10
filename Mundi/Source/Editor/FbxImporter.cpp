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
	TArray<int32> VertexToControlPointMap;  // 각 정점이 어떤 ControlPoint에서 왔는지

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
				VertexToControlPointMap.Add(CPIdx);  // ControlPoint 매핑 저장
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
// [정점별 스키닝 정보 추출] - CPU Skinning용
// ============================================================================
void FFbxImporter::ExtractVertexSkinningData(
	FbxMesh* InMesh,
	TArray<FSkinnedVertex>& OutSkinnedVertices,
	const TMap<UBone*, int32>& BoneToIndexMap,
	const TArray<FNormalVertex>& InVertices,
	const TArray<int32>& VertexToControlPointMap,
	int32 VertexOffset)
{
	int TotalVertexCount = InVertices.Num();
	OutSkinnedVertices.resize(TotalVertexCount);

	// 1. FNormalVertex 데이터를 FSkinnedVertex로 복사 (전체)
	for (int i = 0; i < TotalVertexCount; i++)
	{
		OutSkinnedVertices[i].Position = InVertices[i].pos;
		OutSkinnedVertices[i].Normal = InVertices[i].normal;
		OutSkinnedVertices[i].UV = InVertices[i].tex;
		OutSkinnedVertices[i].Tangent = InVertices[i].Tangent;
		OutSkinnedVertices[i].Color = InVertices[i].color;
	}

	// 2. Skinning 데이터가 없으면 기본값 유지
	int SkinCount = InMesh->GetDeformerCount(FbxDeformer::eSkin);
	if (SkinCount == 0)
		return;

	FbxSkin* Skin = static_cast<FbxSkin*>(InMesh->GetDeformer(0, FbxDeformer::eSkin));
	int ClusterCount = Skin->GetClusterCount();

	// 3. BoneName -> UBone 역매핑 생성 (FbxCluster에서 이름으로 찾기 위함)
	TMap<FString, UBone*> NameToBoneMap;
	for (const auto& Pair : BoneToIndexMap)
	{
		UBone* Bone = Pair.first;
		if (Bone)
		{
			NameToBoneMap.Add(Bone->GetName().ToString(), Bone);
		}
	}

	// 4. ControlPoint별 본 영향 정보 저장 (FBX 원본 정점)
	int ControlPointCount = InMesh->GetControlPointsCount();
	struct FVertexBoneInfluence
	{
		TArray<TPair<int32, float>> Influences; // <BoneIndex, Weight>
	};
	TArray<FVertexBoneInfluence> ControlPointInfluences;
	ControlPointInfluences.resize(ControlPointCount);

	// 5-1. 먼저 모든 Cluster에서 World Bind Pose 추출 (순서 무관)
	TMap<UBone*, FTransform> BoneWorldBindPoseMap;
	for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ClusterIdx++)
	{
		FbxCluster* Cluster = Skin->GetCluster(ClusterIdx);
		if (!Cluster || !Cluster->GetLink())
			continue;

		FString BoneName(Cluster->GetLink()->GetName());
		UBone* const* FoundBone = NameToBoneMap.Find(BoneName);
		if (!FoundBone || !*FoundBone)
			continue;

		// FBX Cluster에서 올바른 Bind Pose World Transform 추출
		FbxAMatrix TransformLinkMatrix;
		Cluster->GetTransformLinkMatrix(TransformLinkMatrix);
		FTransform WorldBindPose = ConvertFbxTransform(TransformLinkMatrix);

		BoneWorldBindPoseMap.Add(*FoundBone, WorldBindPose);
	}

	// 5-2. World Bind Pose를 Relative Bind Pose로 변환하여 Bone에 설정
	for (const auto& Pair : BoneWorldBindPoseMap)
	{
		UBone* Bone = Pair.first;
		const FTransform& WorldBindPose = Pair.second;

		if (Bone->GetParent())
		{
			// Parent의 World Bind Pose를 찾아서 Relative로 변환
			const FTransform* ParentWorldBindPose = BoneWorldBindPoseMap.Find(Bone->GetParent());
			if (ParentWorldBindPose)
			{
				FTransform RelativeBindPose = WorldBindPose.GetRelativeTransform(*ParentWorldBindPose);
				Bone->SetRelativeBindPoseTransform(RelativeBindPose);
			}
			else
			{
				// Parent 정보가 없으면 그대로 사용 (fallback)
				Bone->SetRelativeBindPoseTransform(WorldBindPose);
			}
		}
		else
		{
			// Root Bone은 World가 곧 Relative
			Bone->SetRelativeBindPoseTransform(WorldBindPose);
		}
	}

	// 5-3. 이제 가중치 수집
	for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ClusterIdx++)
	{
		FbxCluster* Cluster = Skin->GetCluster(ClusterIdx);
		if (!Cluster || !Cluster->GetLink())
			continue;

		FString BoneName(Cluster->GetLink()->GetName());
		UBone* const* FoundBone = NameToBoneMap.Find(BoneName);
		if (!FoundBone || !*FoundBone)
			continue;

		const int32* BoneIdxPtr = BoneToIndexMap.Find(*FoundBone);
		if (!BoneIdxPtr)
			continue;

		int32 BoneIdx = *BoneIdxPtr;

		int* ControlPointIndices = Cluster->GetControlPointIndices();
		double* Weights = Cluster->GetControlPointWeights();
		int InfluenceCount = Cluster->GetControlPointIndicesCount();

		// 이 본이 영향을 주는 각 ControlPoint에 대해
		for (int i = 0; i < InfluenceCount; i++)
		{
			int CPIdx = ControlPointIndices[i];
			float Weight = static_cast<float>(Weights[i]);

			if (CPIdx >= 0 && CPIdx < ControlPointCount && Weight > 0.0001f)
			{
				ControlPointInfluences[CPIdx].Influences.Add(TPair<int32, float>(BoneIdx, Weight));
			}
		}
	}

	// 6. ControlPoint 영향 -> 실제 정점에 매핑
	// 이 메시에서 추가된 정점만 처리 (VertexOffset ~ VertexOffset+MapSize)
	int MapSize = VertexToControlPointMap.Num();
	for (int LocalIdx = 0; LocalIdx < MapSize; LocalIdx++)
	{
		int VertexIdx = VertexOffset + LocalIdx;
		if (VertexIdx >= TotalVertexCount)
			break;

		int CPIdx = VertexToControlPointMap[LocalIdx];
		if (CPIdx < 0 || CPIdx >= ControlPointCount)
			continue;

		auto& Influences = ControlPointInfluences[CPIdx].Influences;

		// 가중치 내림차순 정렬
		Influences.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) {
			return A.second > B.second;
		});

		// 최대 4개만 사용
		int InfluenceCount = FMath::Min(static_cast<int>(Influences.Num()), 4);

		// 가중치 합 계산
		float TotalWeight = 0.0f;
		for (int i = 0; i < InfluenceCount; i++)
		{
			TotalWeight += Influences[i].second;
		}

		// 정규화하여 저장
		if (TotalWeight > 0.0001f)
		{
			for (int i = 0; i < InfluenceCount; i++)
			{
				OutSkinnedVertices[VertexIdx].BoneIndices[i] = Influences[i].first;
				OutSkinnedVertices[VertexIdx].BoneWeights[i] = Influences[i].second / TotalWeight;
			}
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
			// Skeleton으로부터 BoneToIndexMap 생성 (ForEachBone 순서대로)
			TMap<UBone*, int32> BoneToIndexMap;
			if (OutSkeletalMesh->Skeleton)
			{
				int32 BoneIndex = 0;
				OutSkeletalMesh->Skeleton->ForEachBone([&](UBone* Bone)
					{
						if (Bone)
						{
							BoneToIndexMap.Add(Bone, BoneIndex++);
						}
					});
			}

			// VertexToControlPointMap을 저장할 변수 준비
			TArray<int32> VertexToControlPointMap;

			// 공통 메시 데이터 추출 (스켈레탈 모드 true)
			// ExtractCommonMeshData 내부에서 VertexToControlPointMap을 채웁니다
			size_t VertexCountBefore = OutSkeletalMesh->Vertices.size();
			ExtractCommonMeshData(Mesh, OutSkeletalMesh, MaterialIDToInfoMap, true);

			// ExtractCommonMeshData 결과로부터 VertexToControlPointMap 복사
			// (ExtractCommonMeshData가 VertexToControlPointMap을 채웠으므로)
			// FIXME: 임시 해결책 - 메시 파싱을 통해 매핑 재생성
			size_t NewVertexCount = OutSkeletalMesh->Vertices.size();
			size_t AddedVertexCount = NewVertexCount - VertexCountBefore;

			// 메시를 다시 순회하여 매핑 생성
			const FbxVector4* ControlPoints = Mesh->GetControlPoints();
			const int PolygonCount = Mesh->GetPolygonCount();

			VertexToControlPointMap.resize(AddedVertexCount);
			int VertexIdx = 0;

			for (int PolyIdx = 0; PolyIdx < PolygonCount; ++PolyIdx)
			{
				int PolySize = Mesh->GetPolygonSize(PolyIdx);
				if (PolySize < 3) continue;
				int NumTri = PolySize - 2;

				for (int TriIdx = 0; TriIdx < NumTri; ++TriIdx)
				{
					int V0 = 0, V1 = TriIdx + 1, V2 = TriIdx + 2;
					for (int LocalVertIdx : { V0, V1, V2 })
					{
						int CPIdx = Mesh->GetPolygonVertex(PolyIdx, LocalVertIdx);
						if (VertexIdx < (int)AddedVertexCount)
						{
							VertexToControlPointMap[VertexIdx] = CPIdx;
						}
						VertexIdx++;
					}
				}
			}

			// CPU Skinning용 정점별 스키닝 데이터 추출 (BoneToIndexMap 전달)
			ExtractVertexSkinningData(Mesh, OutSkeletalMesh->SkinnedVertices, BoneToIndexMap,
				OutSkeletalMesh->Vertices, VertexToControlPointMap, static_cast<int32>(VertexCountBefore));
		}
	}
	//  자식 노드 재귀 처리
	const int ChildCount = InNode->GetChildCount();
	for (int i = 0; i < ChildCount; ++i)
	{
		ProcessMeshNode(InNode->GetChild(i), OutSkeletalMesh, MaterialIDToInfoMap);
	}
}
