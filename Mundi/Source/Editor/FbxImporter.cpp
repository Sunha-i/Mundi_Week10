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

FMatrix FFbxImporter::ConvertFbxMatrix(const FbxAMatrix& InMatrix)
{
	FMatrix Result;

	// FBX은 Column-major 의미, DirectX는 Row-major
	// → 행렬 전치 필요
	for (int Row = 0; Row < 4; ++Row)
	{
		for (int Col = 0; Col < 4; ++Col)
		{
			// Transpose while copying
			Result.M[Row][Col] = static_cast<float>(InMatrix.Get(Col, Row));
		}
	}

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
	std::function<void(FFlesh&)> OnAfterSection = nullptr,
	const TMap<FString, UBone*>* BoneMapPtr = nullptr,
	FFbxImporter* Importer = nullptr
)
{
	using namespace FBXUtil;
	if (!InMesh || !OutMesh)
		return;

	const FbxVector4* ControlPoints = InMesh->GetControlPoints();
	const int ControlPointCount = InMesh->GetControlPointsCount();
	const int PolygonCount = InMesh->GetPolygonCount();

	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;
	TMap<int, TArray<int>> MaterialToTriangles;
	TMap<int, TArray<uint32>> PolyToVertIdx;

	// ====== [스키닝 정보 추출: ControlPoint -> Bone Influences] ======
	struct FBoneInfluence
	{
		TArray<int32> BoneIndices;  // FFlesh.Bones 배열 내 인덱스
		TArray<float> Weights;
	};
	TArray<FBoneInfluence> ControlPointInfluences; // ControlPoint별 본 영향
	TArray<UBone*> MeshBoneList; // 이 메시가 사용하는 본 리스트

	if (bSkeletal && BoneMapPtr)
	{
		ControlPointInfluences.resize(ControlPointCount);

		// FbxSkin에서 스키닝 정보 추출
		int SkinCount = InMesh->GetDeformerCount(FbxDeformer::eSkin);
		if (SkinCount > 0)
		{
			FbxSkin* Skin = static_cast<FbxSkin*>(InMesh->GetDeformer(0, FbxDeformer::eSkin));
			int ClusterCount = Skin->GetClusterCount();

			// 먼저 이 메시가 사용하는 본 리스트 구축
			for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIdx);
				if (!Cluster || !Cluster->GetLink()) continue;

				FString BoneName(Cluster->GetLink()->GetName());
				if (auto* Found = BoneMapPtr->Find(BoneName))
				{
					MeshBoneList.Add(*Found);
				}
			}

			// 각 Cluster(본)마다 영향받는 ControlPoint 처리
			for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIdx);
				if (!Cluster || !Cluster->GetLink()) continue;

				FString BoneName(Cluster->GetLink()->GetName());
				int32 BoneIndex = -1;

				// MeshBoneList에서 이 본의 인덱스 찾기
				for (int i = 0; i < MeshBoneList.size(); i++)
				{
					if (MeshBoneList[i]->GetName().ToString() == BoneName)
					{
						BoneIndex = i;
						break;
					}
				}

				if (BoneIndex < 0) continue;

				// 이 본이 영향을 주는 ControlPoint들
				int* CPIndices = Cluster->GetControlPointIndices();
				double* CPWeights = Cluster->GetControlPointWeights();
				int CPCount = Cluster->GetControlPointIndicesCount();

				for (int i = 0; i < CPCount; ++i)
				{
					int CPIdx = CPIndices[i];
					float Weight = (float)CPWeights[i];

					if (CPIdx < ControlPointCount && Weight > 0.0001f)
					{
						ControlPointInfluences[CPIdx].BoneIndices.Add(BoneIndex);
						ControlPointInfluences[CPIdx].Weights.Add(Weight);
					}
				}
			}

			// 각 ControlPoint의 가중치 정규화 (합이 1.0이 되도록)
			for (int CPIdx = 0; CPIdx < ControlPointCount; ++CPIdx)
			{
				auto& Influence = ControlPointInfluences[CPIdx];
				if (Influence.Weights.size() == 0) continue;

				float TotalWeight = 0.0f;
				for (float W : Influence.Weights)
					TotalWeight += W;

				if (TotalWeight > 0.0001f)
				{
					for (float& W : Influence.Weights)
						W /= TotalWeight;
				}
			}
		}
	}

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

				// ====== 스키닝 정보 설정 (최대 4개 본) ======
				if (bSkeletal && CPIdx < ControlPointInfluences.size())
				{
					const auto& Influence = ControlPointInfluences[CPIdx];
					int InfluenceCount = std::min((int)Influence.BoneIndices.size(), 4);

					for (int i = 0; i < InfluenceCount; ++i)
					{
						V.BoneIndices[i] = (uint32)Influence.BoneIndices[i];
						V.BoneWeights[i] = Influence.Weights[i];
					}

					// 남은 슬롯은 0으로 초기화 (이미 기본값이지만 명시적으로)
					for (int i = InfluenceCount; i < 4; ++i)
					{
						V.BoneIndices[i] = 0;
						V.BoneWeights[i] = 0.0f;
					}
				}

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

		// ====== Skeletal Mesh: 본 리스트 및 OffsetMatrix 저장 ======
		if (bSkeletal && BoneMapPtr)
		{
			// FFlesh에 본 리스트 저장
			Section.Bones = MeshBoneList;

			// 각 본의 OffsetMatrix 계산 (ExtractSkinningData 기능 통합)
			int SkinCount = InMesh->GetDeformerCount(FbxDeformer::eSkin);
			if (SkinCount > 0)
			{
				FbxSkin* Skin = static_cast<FbxSkin*>(InMesh->GetDeformer(0, FbxDeformer::eSkin));
				int ClusterCount = Skin->GetClusterCount();

				for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
				{
					FbxCluster* Cluster = Skin->GetCluster(ClusterIdx);
					if (!Cluster || !Cluster->GetLink()) continue;

					FString BoneName(Cluster->GetLink()->GetName());
					if (auto* Found = BoneMapPtr->Find(BoneName))
					{
						UBone* Bone = *Found;

						// OffsetMatrix 계산 및 저장
						FbxAMatrix TransformLinkMatrix;
						Cluster->GetTransformLinkMatrix(TransformLinkMatrix);

						FbxAMatrix TransformMatrix;
						Cluster->GetTransformMatrix(TransformMatrix);

						// OffsetMatrix = Inverse(BindPoseWorld) * MeshWorld
						FbxAMatrix OffsetMatrixFbx = TransformLinkMatrix.Inverse() * TransformMatrix;
						FMatrix OffsetMatrix = Importer->ConvertFbxMatrix(OffsetMatrixFbx);
						Bone->SetOffsetMatrix(OffsetMatrix);
					}
				}
			}

			// FFlesh.Weights는 더 이상 사용하지 않음 (정점별 가중치로 대체)
			Section.Weights.resize(Section.Bones.size(), 1.0f);
		}

		if (OnAfterSection) OnAfterSection(Section); // 스킨 처리용 콜백 (필요시)
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
			UBone* Bone = *Found;
			OutFlesh.Bones.Add(Bone);

			double* W = Cl->GetControlPointWeights();
			int Cnt = Cl->GetControlPointIndicesCount();

			float Avg = 0.0f;
			for (int j = 0; j < Cnt; ++j) Avg += (float)W[j];
			OutFlesh.Weights.Add(Cnt > 0 ? Avg / Cnt : 0.f);

			// OffsetMatrix 계산 및 저장
			// TransformLinkMatrix = 본의 BindPose 월드 행렬
			FbxAMatrix TransformLinkMatrix;
			Cl->GetTransformLinkMatrix(TransformLinkMatrix);

			// TransformMatrix = 메시의 월드 행렬
			FbxAMatrix TransformMatrix;
			Cl->GetTransformMatrix(TransformMatrix);

			// OffsetMatrix = MeshWorld * Inverse(BindPoseWorld)
			// VertexWorld = VertexMeshLocal * MeshWorld * Inverse(BoneBindPose) * BoneCurrent
			FbxAMatrix OffsetMatrixFbx = TransformLinkMatrix.Inverse() * TransformMatrix;
			FMatrix OffsetMatrix = ConvertFbxMatrix(OffsetMatrixFbx);
			Bone->SetOffsetMatrix(OffsetMatrix);
		}
	}
}

// ============================================================================
// [디버그: 스켈레탈 메시 정보 txt 출력]
// ============================================================================
void FFbxImporter::DumpSkeletalMeshInfo(const FSkeletalMesh* SkeletalMesh, const FString& OutputPath)
{
	if (!SkeletalMesh)
		return;

	std::ofstream OutFile(OutputPath);
	if (!OutFile.is_open())
	{
		UE_LOG("[FBX Dump] Failed to open file: %s", OutputPath.c_str());
		return;
	}

	OutFile << "========================================\n";
	OutFile << "SKELETAL MESH DEBUG INFO\n";
	OutFile << "========================================\n\n";

	// Skeleton 정보
	if (SkeletalMesh->Skeleton && SkeletalMesh->Skeleton->GetRoot())
	{
		OutFile << "### SKELETON ###\n";
		std::function<void(UBone*, int)> DumpBone = [&](UBone* Bone, int Depth)
		{
			if (!Bone)
				return;

			FString Indent(Depth * 2, ' ');
			OutFile << Indent.c_str() << "Bone: " << Bone->GetName().ToString().c_str() << "\n";

			FMatrix OffsetMatrix = Bone->GetOffsetMatrix();
			OutFile << Indent.c_str() << "  OffsetMatrix:\n";
			OutFile << Indent.c_str() << "    [" << OffsetMatrix.M[0][0] << ", " << OffsetMatrix.M[0][1] << ", " << OffsetMatrix.M[0][2] << ", " << OffsetMatrix.M[0][3] << "]\n";
			OutFile << Indent.c_str() << "    [" << OffsetMatrix.M[1][0] << ", " << OffsetMatrix.M[1][1] << ", " << OffsetMatrix.M[1][2] << ", " << OffsetMatrix.M[1][3] << "]\n";
			OutFile << Indent.c_str() << "    [" << OffsetMatrix.M[2][0] << ", " << OffsetMatrix.M[2][1] << ", " << OffsetMatrix.M[2][2] << ", " << OffsetMatrix.M[2][3] << "]\n";
			OutFile << Indent.c_str() << "    [" << OffsetMatrix.M[3][0] << ", " << OffsetMatrix.M[3][1] << ", " << OffsetMatrix.M[3][2] << ", " << OffsetMatrix.M[3][3] << "]\n";

			FTransform BindPose = Bone->GetRelativeBindPose();
			OutFile << Indent.c_str() << "  BindPose (Relative):\n";
			OutFile << Indent.c_str() << "    Loc: (" << BindPose.Translation.X << ", " << BindPose.Translation.Y << ", " << BindPose.Translation.Z << ")\n";
			OutFile << Indent.c_str() << "    Rot: (" << BindPose.Rotation.X << ", " << BindPose.Rotation.Y << ", " << BindPose.Rotation.Z << ", " << BindPose.Rotation.W << ")\n";
			OutFile << Indent.c_str() << "    Scl: (" << BindPose.Scale3D.X << ", " << BindPose.Scale3D.Y << ", " << BindPose.Scale3D.Z << ")\n";

			for (UBone* Child : Bone->GetChildren())
			{
				DumpBone(Child, Depth + 1);
			}
		};

		DumpBone(SkeletalMesh->Skeleton->GetRoot(), 0);
		OutFile << "\n";
	}

	// Fleshes 정보
	OutFile << "### FLESHES (Submeshes) ###\n";
	OutFile << "Total Fleshes: " << SkeletalMesh->Fleshes.Num() << "\n\n";

	for (int i = 0; i < SkeletalMesh->Fleshes.Num(); i++)
	{
		const FFlesh& Flesh = SkeletalMesh->Fleshes[i];
		OutFile << "Flesh [" << i << "]:\n";
		OutFile << "  StartIndex: " << Flesh.StartIndex << "\n";
		OutFile << "  IndexCount: " << Flesh.IndexCount << "\n";
		OutFile << "  Material: " << Flesh.InitialMaterialName.c_str() << "\n";
		OutFile << "  Bones Count: " << Flesh.Bones.Num() << "\n";
		OutFile << "  Weights:\n";

		for (int j = 0; j < Flesh.Bones.Num(); j++)
		{
			UBone* Bone = Flesh.Bones[j];
			float Weight = j < Flesh.Weights.Num() ? Flesh.Weights[j] : 0.f;
			FString BoneName = Bone ? Bone->GetName().ToString() : "NULL";
			OutFile << "    [" << j << "] " << BoneName.c_str() << " : Weight=" << Weight << "\n";
		}
		OutFile << "\n";
	}

	// Vertices 정보 (처음 10개만 샘플링)
	OutFile << "### VERTICES (First 10) ###\n";
	OutFile << "Total Vertices: " << SkeletalMesh->Vertices.size() << "\n\n";

	int VertexCount = std::min((size_t)10, SkeletalMesh->Vertices.size());
	for (int i = 0; i < VertexCount; i++)
	{
		const auto& Vertex = SkeletalMesh->Vertices[i];
		OutFile << "Vertex [" << i << "]: Pos=(" << Vertex.pos.X << ", " << Vertex.pos.Y << ", " << Vertex.pos.Z << ")\n";
		OutFile << "  BoneIndices=[" << Vertex.BoneIndices[0] << ", " << Vertex.BoneIndices[1] << ", "
		        << Vertex.BoneIndices[2] << ", " << Vertex.BoneIndices[3] << "]\n";
		OutFile << "  BoneWeights=[" << Vertex.BoneWeights[0] << ", " << Vertex.BoneWeights[1] << ", "
		        << Vertex.BoneWeights[2] << ", " << Vertex.BoneWeights[3] << "]\n";
	}
	OutFile << "\n";

	OutFile << "========================================\n";
	OutFile << "END OF DEBUG INFO\n";
	OutFile << "========================================\n";

	OutFile.close();
	UE_LOG("[FBX Dump] Successfully wrote debug info to: %s", OutputPath.c_str());
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

			// 공통 메시 데이터 추출 (스켈레탈 모드 true, BoneMap 전달)
			ExtractCommonMeshData(Mesh, OutSkeletalMesh, MaterialIDToInfoMap, true, nullptr, &BoneMap, this);
		}
	}
	//  자식 노드 재귀 처리
	const int ChildCount = InNode->GetChildCount();
	for (int i = 0; i < ChildCount; ++i)
	{
		ProcessMeshNode(InNode->GetChild(i), OutSkeletalMesh, MaterialIDToInfoMap);
	}
}
