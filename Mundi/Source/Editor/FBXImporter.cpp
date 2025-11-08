#include "pch.h"
#include "FBXImporter.h"

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

bool FFBXImporter::ImportFBX(const FString& FilePath, USkeletalMesh* OutMesh, FFBXSkeletonData* OutSkeleton, const FFBXImportOptions& Options)
{
	FbxManager* SDKManager = FbxManager::Create();
	FbxIOSettings* IOSettings = FbxIOSettings::Create(SDKManager, IOSROOT);
	SDKManager->SetIOSettings(IOSettings);

	FbxImporter* Importer = FbxImporter::Create(SDKManager, "");

	if (!Importer->Initialize(FilePath.c_str(), -1, SDKManager->GetIOSettings()))
	{
		UE_LOG("FBX Import Failed: %s\n", Importer->GetStatus().GetErrorString());
		return false;
	}

	FbxScene* Scene = FbxScene::Create(SDKManager, "Scene");
	Importer->Import(Scene);
	Importer->Destroy();

	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode)
	{
		UE_LOG("FBX has no root node\n");
		return false;
	}

	ParseSkeleton(RootNode, *OutSkeleton);

	for (int i = 0; i < RootNode->GetChildCount(); ++i)
	{
		FbxNode* Child = RootNode->GetChild(i);
		FbxMesh* Mesh = Child->GetMesh();
		if (Mesh)
		{
			FFBXMeshData MeshData;
			ParseMesh(Mesh, MeshData);
			//OutMesh->SetFromImportData(MeshData, *OutSkeleton);
		}
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
			FFBXSkeletonData::FBone Bone;
			Bone.Name = Node->GetName();
			Bone.ParentIndex = ParentIndex;

			FbxAMatrix BindPose = Node->EvaluateGlobalTransform();
			Bone.BindPose = FbxAMatrixToFMatrix(BindPose);
			Bone.InverseBindPose = FbxAMatrixToFMatrix(BindPose.Inverse());

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

FSkeletalMesh* FFbxManager::LoadFbxSkeletalMeshAsset(const FString& PathFileName)
{
	return nullptr;
}

USkeletalMesh* FFbxManager::LoadFbxSkeletalMesh(const FString& PathFileName)
{
	return nullptr;
}

bool FFbxManager::IsSkeletalMesh(FbxMesh* Mesh)
{
	const int DeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	return (DeformerCount > 0);
}
