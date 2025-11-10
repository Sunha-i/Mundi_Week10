#include "pch.h"

#include "FbxImporter.h"
#include "Bone.h"
#include "SkeletalMeshStruct.h"
#include "StaticMesh.h"


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
    return Scene;
}

void FFbxImporter::CollectMaterials(FbxScene* Scene, TMap<int64, FMaterialInfo>& OutMatMap, TArray<FMaterialInfo>& OutMaterialInfos, const FString& Path)
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
        OutMaterialInfos.Add(Info);
    }
}

UBone* FFbxImporter::FindSkeletonRootAndBuild(FbxNode* RootNode)
{
    for (int i = 0; i < RootNode->GetChildCount(); i++)
    {
        FbxNode* Child = RootNode->GetChild(i);
        if (auto* Attr = Child->GetNodeAttribute())
        {
            if (Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
                return ProcessSkeletonNode(Child);
        }
    }
    return nullptr;
}

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


FTransform FFbxImporter::ConvertFbxTransform(const FbxAMatrix& InMatrix)
{
    FTransform Result;

    FbxVector4 Translation = InMatrix.GetT();
    Result.Translation = FVector(static_cast<float>(Translation[0]),
                                  static_cast<float>(-Translation[1]),
                                  static_cast<float>(Translation[2]));

    FbxQuaternion Rotation = InMatrix.GetQ();
    Result.Rotation = FQuat(static_cast<float>(Rotation[0]),
                            static_cast<float>(-Rotation[1]),
                            static_cast<float>(-Rotation[2]),
                            static_cast<float>(Rotation[3]));

    FbxVector4 Scale = InMatrix.GetS();
    Result.Scale3D = FVector(static_cast<float>(Scale[0]),
                             static_cast<float>(Scale[1]),
                             static_cast<float>(Scale[2]));

    return Result;
}

void FFbxImporter::ProcessMeshNode(FbxNode* InNode, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap)
{
    if (!InNode || !OutSkeletalMesh)
        return;

    FbxNodeAttribute* Attr = InNode->GetNodeAttribute();
    if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
        FbxMesh* Mesh = InNode->GetMesh();
        if (Mesh)
        {
            ExtractMeshData(Mesh, OutSkeletalMesh, MaterialIDToInfoMap);
        }
    }

    for (int i = 0; i < InNode->GetChildCount(); i++)
    {
        ProcessMeshNode(InNode->GetChild(i), OutSkeletalMesh, MaterialIDToInfoMap);
    }
}

void FFbxImporter::ExtractMeshData(FbxMesh* InMesh, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap)
{
    if (!InMesh || !OutSkeletalMesh)
        return;

    uint32 BaseVertexOffset = static_cast<uint32>(OutSkeletalMesh->Vertices.size());
    uint32 BaseIndexOffset = static_cast<uint32>(OutSkeletalMesh->Indices.size());

    int VertexCount = InMesh->GetControlPointsCount();
    FbxVector4* ControlPoints = InMesh->GetControlPoints();

    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;

    int PolygonCount = InMesh->GetPolygonCount();
    TMap<int, TArray<uint32>> PolyIdxToVertexIndices;
    int ProcessedTriangleCount = 0;

    int TriangleCount = 0;
    int QuadCount = 0;
    int NgonCount = 0;
    int TotalNgonVertices = 0;

    for (int PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
    {
        int PolySize = InMesh->GetPolygonSize(PolyIdx);

        if (PolySize == 3) TriangleCount++;
        else if (PolySize == 4) QuadCount++;
        else if (PolySize > 4)
        {
            NgonCount++;
            TotalNgonVertices += PolySize;
        }

        if (PolySize < 3)
        {
            UE_LOG("[Warning] Invalid polygon with %d vertices at index %d - skipping", PolySize, PolyIdx);
            continue;
        }

        int NumTriangles = PolySize - 2;

        for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
        {
            TArray<uint32> TriangleIndices;

            int V0 = 0;
            int V1 = TriIdx + 2;
            int V2 = TriIdx + 1;

            for (int LocalVertIdx : {V0, V1, V2})
            {
                int ControlPointIdx = InMesh->GetPolygonVertex(PolyIdx, LocalVertIdx);

                FNormalVertex Vertex;

                FbxVector4 Pos = ControlPoints[ControlPointIdx];
                Vertex.pos = FVector(static_cast<float>(Pos[0]), static_cast<float>(-Pos[1]), static_cast<float>(Pos[2]));

                FbxVector4 Normal;
                if (InMesh->GetPolygonVertexNormal(PolyIdx, LocalVertIdx, Normal))
                {
                    Vertex.normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(-Normal[1]), static_cast<float>(Normal[2]));
                }

                FbxGeometryElementTangent* TangentElement = InMesh->GetElementTangent();
                if (TangentElement)
                {
                    int TangentIndex = -1;
                    int PolyVertIndex = InMesh->GetPolygonVertexIndex(PolyIdx) + LocalVertIdx;

                    if (TangentElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                    {
                        if (TangentElement->GetReferenceMode() == FbxGeometryElement::eDirect)
                        {
                            TangentIndex = PolyVertIndex;
                        }
                        else if (TangentElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
                        {
                            TangentIndex = TangentElement->GetIndexArray().GetAt(PolyVertIndex);
                        }
                    }
                    else if (TangentElement->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                    {
                        if (TangentElement->GetReferenceMode() == FbxGeometryElement::eDirect)
                        {
                            TangentIndex = ControlPointIdx;
                        }
                        else if (TangentElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
                        {
                            TangentIndex = TangentElement->GetIndexArray().GetAt(ControlPointIdx);
                        }
                    }

                    if (TangentIndex >= 0 && TangentIndex < TangentElement->GetDirectArray().GetCount())
                    {
                        FbxVector4 Tangent = TangentElement->GetDirectArray().GetAt(TangentIndex);
                        Vertex.Tangent = FVector4(
                            static_cast<float>(Tangent[0]),
                            static_cast<float>(-Tangent[1]),
                            static_cast<float>(Tangent[2]),
                            static_cast<float>(Tangent[3])
                        );
                    }
                    else
                    {
                        Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
                    }
                }
                else
                {
                    Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
                }

                FbxStringList UVSetNames;
                InMesh->GetUVSetNames(UVSetNames);
                if (UVSetNames.GetCount() > 0)
                {
                    FbxVector2 UV;
                    bool bUnmapped;
                    if (InMesh->GetPolygonVertexUV(PolyIdx, LocalVertIdx, UVSetNames[0], UV, bUnmapped))
                    {
                        Vertex.tex = FVector2D(static_cast<float>(UV[0]), 1.0f - static_cast<float>(UV[1]));
                    }
                }

                uint32 VertexIndex = static_cast<uint32>(Vertices.size());
                Vertices.Add(Vertex);
                TriangleIndices.Add(VertexIndex);
            }

            int StorageKey = PolyIdx * 100 + TriIdx;
            PolyIdxToVertexIndices.Add(StorageKey, TriangleIndices);
            ProcessedTriangleCount++;
        }
    }

    FbxLayerElementMaterial* MaterialElement = InMesh->GetElementMaterial();
    TMap<int, TArray<int>> MaterialToTriangles;

    if (MaterialElement)
    {
        for (int PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
        {
            int PolySize = InMesh->GetPolygonSize(PolyIdx);
            if (PolySize < 3)
                continue;

            int MaterialIndex = 0;

            if (MaterialElement->GetMappingMode() == FbxLayerElement::eByPolygon)
            {
                if (MaterialElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                {
                    MaterialIndex = MaterialElement->GetIndexArray().GetAt(PolyIdx);
                }
                else if (MaterialElement->GetReferenceMode() == FbxLayerElement::eDirect)
                {
                    MaterialIndex = PolyIdx;
                }
            }

            if (!MaterialToTriangles.Contains(MaterialIndex))
            {
                MaterialToTriangles.Add(MaterialIndex, TArray<int>());
            }

            int NumTriangles = PolySize - 2;
            for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
            {
                int StorageKey = PolyIdx * 100 + TriIdx;
                MaterialToTriangles[MaterialIndex].Add(StorageKey);
            }
        }
    }
    else
    {
        TArray<int> AllTriangles;
        for (int PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
        {
            int PolySize = InMesh->GetPolygonSize(PolyIdx);
            if (PolySize < 3)
                continue;

            int NumTriangles = PolySize - 2;
            for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
            {
                int StorageKey = PolyIdx * 100 + TriIdx;
                AllTriangles.Add(StorageKey);
            }
        }
        MaterialToTriangles.Add(0, AllTriangles);
    }

    TArray<uint32> ReorderedIndices;
    ReorderedIndices.reserve(Indices.Num());
    uint32 CurrentStartIndex = BaseIndexOffset;

    TArray<int> SortedMaterialIndices;
    for (auto& Pair : MaterialToTriangles)
    {
        SortedMaterialIndices.Add(Pair.first);
    }
    SortedMaterialIndices.Sort();

    TMap<FString, UBone*> BoneMap;
    if (OutSkeletalMesh->Skeleton)
    {
        OutSkeletalMesh->Skeleton->ForEachBone([&BoneMap](UBone* Bone) {
            if (Bone)
            {
                FString BoneName = Bone->GetName().ToString();
                BoneMap.Add(BoneName, Bone);
            }
            });
    }

    for (int MaterialIndex : SortedMaterialIndices)
    {
        TArray<int>& TriangleIndices = MaterialToTriangles[MaterialIndex];

        FFlesh NewFlesh;
        NewFlesh.StartIndex = CurrentStartIndex;
        NewFlesh.IndexCount = 0;

        for (int StorageKey : TriangleIndices)
        {
            TArray<uint32>* VertexIndicesPtr = PolyIdxToVertexIndices.Find(StorageKey);
            if (!VertexIndicesPtr || VertexIndicesPtr->Num() != 3)
                continue;

            const TArray<uint32>& VertexIndices = *VertexIndicesPtr;
            ReorderedIndices.Add(VertexIndices[0] + BaseVertexOffset);
            ReorderedIndices.Add(VertexIndices[1] + BaseVertexOffset);
            ReorderedIndices.Add(VertexIndices[2] + BaseVertexOffset);
            NewFlesh.IndexCount += 3;
        }

        if (MaterialElement && MaterialIndex < InMesh->GetNode()->GetMaterialCount())
        {
            FbxSurfaceMaterial* Material = InMesh->GetNode()->GetMaterial(MaterialIndex);
            if (Material)
            {
                FString MaterialName = Material->GetName();
                int64 MaterialID = Material->GetUniqueID();

                if (const FMaterialInfo* FoundMatInfo = MaterialIDToInfoMap.Find(MaterialID))
                {
                    NewFlesh.InitialMaterialName = FoundMatInfo->MaterialName;
                    UE_LOG("[Flesh Material] Assigned Material '%s' to Flesh (StartIndex=%d, IndexCount=%d)",
                        NewFlesh.InitialMaterialName.c_str(), NewFlesh.StartIndex, NewFlesh.IndexCount);
                }
                else
                {
                    NewFlesh.InitialMaterialName = MaterialName;
                    UE_LOG("[Flesh Material] Material[%lld]='%s' not found in map - using name directly",
                        MaterialID, MaterialName.c_str());
                }
            }
        }
        else
        {
            NewFlesh.InitialMaterialName = "DefaultMaterial";
            UE_LOG("[Flesh Material] No material assigned - using DefaultMaterial");
        }

        ExtractSkinningData(InMesh, NewFlesh, BoneMap);

        OutSkeletalMesh->Fleshes.Add(NewFlesh);
        CurrentStartIndex += NewFlesh.IndexCount;
    }

    OutSkeletalMesh->Vertices.insert(OutSkeletalMesh->Vertices.end(), Vertices.begin(), Vertices.end());
    OutSkeletalMesh->Indices.insert(OutSkeletalMesh->Indices.end(), ReorderedIndices.begin(), ReorderedIndices.end());
    OutSkeletalMesh->bHasMaterial = OutSkeletalMesh->bHasMaterial || (InMesh->GetElementMaterialCount() > 0);
}

void FFbxImporter::ExtractSkinningData(FbxMesh* InMesh, FFlesh& OutFlesh, const TMap<FString, UBone*>& BoneMap)
{
    if (!InMesh)
        return;

    int SkinCount = InMesh->GetDeformerCount(FbxDeformer::eSkin);
    if (SkinCount == 0)
        return;

    FbxSkin* Skin = static_cast<FbxSkin*>(InMesh->GetDeformer(0, FbxDeformer::eSkin));
    int ClusterCount = Skin->GetClusterCount();

    TArray<UBone*> Bones;
    TArray<float> Weights;
    float WeightsTotal = 0.0f;

    for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ClusterIdx++)
    {
        FbxCluster* Cluster = Skin->GetCluster(ClusterIdx);
        if (!Cluster || !Cluster->GetLink())
            continue;

        FString BoneName(Cluster->GetLink()->GetName());
        UBone* const* FoundBone = BoneMap.Find(BoneName);

        if (FoundBone && *FoundBone)
        {
            Bones.Add(*FoundBone);

            int* Indices = Cluster->GetControlPointIndices();
            double* Weights_Raw = Cluster->GetControlPointWeights();
            int IndexCount = Cluster->GetControlPointIndicesCount();

            float AvgWeight = 0.0f;
            for (int i = 0; i < IndexCount; i++)
            {
                AvgWeight += static_cast<float>(Weights_Raw[i]);
            }
            AvgWeight /= (IndexCount > 0 ? IndexCount : 1);

            Weights.Add(AvgWeight);
            WeightsTotal += AvgWeight;
        }
    }

    OutFlesh.Bones = Bones;
    OutFlesh.Weights = Weights;
    OutFlesh.WeightsTotal = WeightsTotal;
}

void FFbxImporter::ProcessMeshNodeAsStatic(FbxNode* InNode, FStaticMesh* OutStaticMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap)
{
    if (!InNode || !OutStaticMesh)
        return;

    FbxNodeAttribute* Attr = InNode->GetNodeAttribute();
    if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
        FbxMesh* Mesh = InNode->GetMesh();
        if (Mesh)
        {
            ExtractMeshDataAsStatic(Mesh, OutStaticMesh, MaterialIDToInfoMap);
        }
    }

    for (int i = 0; i < InNode->GetChildCount(); i++)
    {
        ProcessMeshNodeAsStatic(InNode->GetChild(i), OutStaticMesh, MaterialIDToInfoMap);
    }
}

void FFbxImporter::ExtractMeshDataAsStatic(FbxMesh* InMesh, FStaticMesh* OutStaticMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap)
{
    if (!InMesh || !OutStaticMesh)
        return;

    uint32 BaseVertexOffset = static_cast<uint32>(OutStaticMesh->Vertices.size());
    uint32 BaseIndexOffset = static_cast<uint32>(OutStaticMesh->Indices.size());

    int VertexCount = InMesh->GetControlPointsCount();
    FbxVector4* ControlPoints = InMesh->GetControlPoints();

    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;

    int PolygonCount = InMesh->GetPolygonCount();
    TMap<int, TArray<uint32>> PolyIdxToVertexIndices;

    for (int PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
    {
        int PolySize = InMesh->GetPolygonSize(PolyIdx);
        if (PolySize < 3)
            continue;

        int NumTriangles = PolySize - 2;

        for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
        {
            TArray<uint32> TriangleIndices;

            int V0 = 0;
            int V1 = TriIdx + 2;
            int V2 = TriIdx + 1;

            for (int LocalVertIdx : {V0, V1, V2})
            {
                int ControlPointIdx = InMesh->GetPolygonVertex(PolyIdx, LocalVertIdx);

                FNormalVertex Vertex;

                FbxVector4 Pos = ControlPoints[ControlPointIdx];
                Vertex.pos = FVector(static_cast<float>(Pos[0]), static_cast<float>(-Pos[1]), static_cast<float>(Pos[2]));

                FbxVector4 Normal;
                if (InMesh->GetPolygonVertexNormal(PolyIdx, LocalVertIdx, Normal))
                {
                    Vertex.normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(-Normal[1]), static_cast<float>(Normal[2]));
                }

                FbxGeometryElementTangent* TangentElement = InMesh->GetElementTangent();
                if (TangentElement)
                {
                    int TangentIndex = -1;
                    int PolyVertIndex = InMesh->GetPolygonVertexIndex(PolyIdx) + LocalVertIdx;

                    if (TangentElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                    {
                        if (TangentElement->GetReferenceMode() == FbxGeometryElement::eDirect)
                        {
                            TangentIndex = PolyVertIndex;
                        }
                        else if (TangentElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
                        {
                            TangentIndex = TangentElement->GetIndexArray().GetAt(PolyVertIndex);
                        }
                    }
                    else if (TangentElement->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                    {
                        if (TangentElement->GetReferenceMode() == FbxGeometryElement::eDirect)
                        {
                            TangentIndex = ControlPointIdx;
                        }
                        else if (TangentElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
                        {
                            TangentIndex = TangentElement->GetIndexArray().GetAt(ControlPointIdx);
                        }
                    }

                    if (TangentIndex >= 0 && TangentIndex < TangentElement->GetDirectArray().GetCount())
                    {
                        FbxVector4 Tangent = TangentElement->GetDirectArray().GetAt(TangentIndex);
                        Vertex.Tangent = FVector4(
                            static_cast<float>(Tangent[0]),
                            static_cast<float>(-Tangent[1]),
                            static_cast<float>(Tangent[2]),
                            static_cast<float>(Tangent[3])
                        );
                    }
                    else
                    {
                        Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
                    }
                }
                else
                {
                    Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
                }

                FbxStringList UVSetNames;
                InMesh->GetUVSetNames(UVSetNames);
                if (UVSetNames.GetCount() > 0)
                {
                    FbxVector2 UV;
                    bool bUnmapped;
                    if (InMesh->GetPolygonVertexUV(PolyIdx, LocalVertIdx, UVSetNames[0], UV, bUnmapped))
                    {
                        Vertex.tex = FVector2D(static_cast<float>(UV[0]), 1.0f - static_cast<float>(UV[1]));
                    }
                }

                uint32 VertexIndex = static_cast<uint32>(Vertices.size());
                Vertices.Add(Vertex);
                TriangleIndices.Add(VertexIndex);
            }

            int StorageKey = PolyIdx * 100 + TriIdx;
            PolyIdxToVertexIndices.Add(StorageKey, TriangleIndices);
        }
    }

    FbxLayerElementMaterial* MaterialElement = InMesh->GetElementMaterial();
    TMap<int, TArray<int>> MaterialToTriangles;

    if (MaterialElement)
    {
        for (int PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
        {
            int PolySize = InMesh->GetPolygonSize(PolyIdx);
            if (PolySize < 3)
                continue;

            int MaterialIndex = 0;
            if (MaterialElement->GetMappingMode() == FbxLayerElement::eByPolygon)
            {
                if (MaterialElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                {
                    MaterialIndex = MaterialElement->GetIndexArray().GetAt(PolyIdx);
                }
            }

            if (!MaterialToTriangles.Contains(MaterialIndex))
            {
                MaterialToTriangles.Add(MaterialIndex, TArray<int>());
            }

            int NumTriangles = PolySize - 2;
            for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
            {
                int StorageKey = PolyIdx * 100 + TriIdx;
                MaterialToTriangles[MaterialIndex].Add(StorageKey);
            }
        }
    }
    else
    {
        TArray<int> AllTriangles;
        for (int PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
        {
            int PolySize = InMesh->GetPolygonSize(PolyIdx);
            if (PolySize < 3)
                continue;

            int NumTriangles = PolySize - 2;
            for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
            {
                int StorageKey = PolyIdx * 100 + TriIdx;
                AllTriangles.Add(StorageKey);
            }
        }
        MaterialToTriangles.Add(0, AllTriangles);
    }

    TArray<uint32> ReorderedIndices;
    uint32 CurrentStartIndex = BaseIndexOffset;

    TArray<int> SortedMaterialIndices;
    for (auto& Pair : MaterialToTriangles)
    {
        SortedMaterialIndices.Add(Pair.first);
    }
    SortedMaterialIndices.Sort();

    for (int MaterialIndex : SortedMaterialIndices)
    {
        TArray<int>& TriangleIndices = MaterialToTriangles[MaterialIndex];

        FGroupInfo NewGroup;
        NewGroup.StartIndex = CurrentStartIndex;
        NewGroup.IndexCount = 0;

        for (int StorageKey : TriangleIndices)
        {
            TArray<uint32>* VertexIndicesPtr = PolyIdxToVertexIndices.Find(StorageKey);
            if (!VertexIndicesPtr || VertexIndicesPtr->Num() != 3)
                continue;

            const TArray<uint32>& VertexIndices = *VertexIndicesPtr;
            ReorderedIndices.Add(VertexIndices[0] + BaseVertexOffset);
            ReorderedIndices.Add(VertexIndices[1] + BaseVertexOffset);
            ReorderedIndices.Add(VertexIndices[2] + BaseVertexOffset);
            NewGroup.IndexCount += 3;
        }

        if (MaterialElement && MaterialIndex < InMesh->GetNode()->GetMaterialCount())
        {
            FbxSurfaceMaterial* Material = InMesh->GetNode()->GetMaterial(MaterialIndex);
            if (Material)
            {
                int64 MaterialID = Material->GetUniqueID();
                if (const FMaterialInfo* FoundMatInfo = MaterialIDToInfoMap.Find(MaterialID))
                {
                    NewGroup.InitialMaterialName = FoundMatInfo->MaterialName;
                }
                else
                {
                    NewGroup.InitialMaterialName = Material->GetName();
                }
            }
        }
        else
        {
            NewGroup.InitialMaterialName = "DefaultMaterial";
        }

        OutStaticMesh->GroupInfos.Add(NewGroup);
        CurrentStartIndex += NewGroup.IndexCount;
    }

    OutStaticMesh->Vertices.insert(OutStaticMesh->Vertices.end(), Vertices.begin(), Vertices.end());
    OutStaticMesh->Indices.insert(OutStaticMesh->Indices.end(), ReorderedIndices.begin(), ReorderedIndices.end());
    OutStaticMesh->bHasMaterial = OutStaticMesh->bHasMaterial || (InMesh->GetElementMaterialCount() > 0);
}