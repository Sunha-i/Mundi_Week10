#include "pch.h"
#include "FbxManager.h"
#include "FbxDebugLog.h"
#include "Bone.h"
#include "SkeletalMesh.h"

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
{
    // SDK 관리자 초기화. 이 객체는 메모리 관리를 처리함.
    SdkManager = FbxManager::Create();

    // IO 설정 객체를 생성한다.
    ios = FbxIOSettings::Create(SdkManager, IOSROOT);
    SdkManager->SetIOSettings( ios );

    // SDK 관리자를 사용하여 Importer를 생성한다.
    FbxImporter* Importer = FbxImporter::Create(SdkManager, "");

    // // 첫 번째 인수를 Importer의 파일명으로 사용한다.
    // if (!Importer->Initialize(FileName, -1, SdkManager->GetIOSettings()))
    // {
    //     UE_LOG("Call to FbxImporter::Initialize() failed.\n");
    //     UE_LOG("Error returned: %s\n\n", Importer->GetStatus().GetErrorString());
    //     assert(false);
    // }

    // 가져온 파일로 채울 수 있도록 새 씬을 생성한다.
    //FbxScene* Scene = FbxScene::Create(SdkManager, "myScene");

    // 파일의 내용을 씬으로 가져온다.
    //Importer->Import(Scene);

    // 파일을 가져왔으므로 임포터를 제거한다.
    //Importer->Destroy();

    // 씬의 노드와 그 속성을 재귀적으로 출력한다.
    //FbxNode* RootNode = Scene->GetRootNode();
    //FbxDebugLog::PrintAllChildNodes(RootNode);
}

void FFbxManager::Shutdown()
{
    // SDK 관리자와 그것이 처리하던 다른 모든 객체를 소멸시킨다.
    SdkManager->Destroy();
}

FFbxManager& FFbxManager::GetInstance()
{
    static FFbxManager FbxManager;
    return FbxManager;
}

void FFbxManager::Preload()
{
    ;
}

void FFbxManager::Clear()
{
    for (auto Iter = FbxSkeletalMeshMap.begin(); Iter != FbxSkeletalMeshMap.end(); Iter++)
    {
        delete Iter->second;
    }

    FbxSkeletalMeshMap.clear();
}

FSkeletalMesh* FFbxManager::LoadFbxSkeletalMeshAsset(const FString& PathFileName)
{
    FString NormalizedPathStr = NormalizePath(PathFileName);

    if (FSkeletalMesh** It = FbxSkeletalMeshMap.Find(NormalizedPathStr))
    {
        return *It;
    }

    std::filesystem::path Path(NormalizedPathStr);

    // 2. 파일 경로 설정
    FString Extension = Path.extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (Extension != ".fbx")
    {
        UE_LOG("this file is not fbx!: %s", NormalizedPathStr.c_str());
        return nullptr;
    }

    // FBX Importer 생성
    FbxImporter* LocalImporter = FbxImporter::Create(SdkManager, "");
    if (!LocalImporter->Initialize(NormalizedPathStr.c_str(), -1, SdkManager->GetIOSettings()))
    {
        UE_LOG("Failed to initialize FBX Importer for: %s", NormalizedPathStr.c_str());
        UE_LOG("Error: %s", LocalImporter->GetStatus().GetErrorString());
        LocalImporter->Destroy();
        return nullptr;
    }

    // Scene 생성 및 Import
    FbxScene* Scene = FbxScene::Create(SdkManager, "ImportScene");
    if (!LocalImporter->Import(Scene))
    {
        UE_LOG("Failed to import FBX scene: %s", NormalizedPathStr.c_str());
        LocalImporter->Destroy();
        Scene->Destroy();
        return nullptr;
    }
    LocalImporter->Destroy();

    // FBX Scene의 좌표계 정보 확인
    FbxAxisSystem SceneAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
    int UpSign, FrontSign, CoordSign;
    FbxAxisSystem::EUpVector UpVector = SceneAxisSystem.GetUpVector(UpSign);
    FbxAxisSystem::EFrontVector FrontVector = SceneAxisSystem.GetFrontVector(FrontSign);
    FbxAxisSystem::ECoordSystem CoordSystem = SceneAxisSystem.GetCoorSystem();

    UE_LOG("[FBX Coordinate System] UpVector=%d UpSign=%d, FrontVector=%d FrontSign=%d, CoordSystem=%d (0=RH, 1=LH)",
        (int)UpVector, UpSign, (int)FrontVector, FrontSign, (int)CoordSystem);

    // ConvertScene을 사용하지 않고 수동 변환 (OBJ와 동일한 방식)
    // OBJ 로더와 정확히 같은 변환을 적용하기 위해 ConvertScene 비활성화

    // FSkeletalMesh 생성
    FSkeletalMesh* NewSkeletalMesh = new FSkeletalMesh;
    NewSkeletalMesh->PathFileName = NormalizedPathStr;

    // Scene의 모든 Material에서 텍스처 정보 수집 및 UMaterial 생성
    // Material UniqueID -> FMaterialInfo 매핑
    TMap<int64, FMaterialInfo> MaterialIDToInfoMap;
    UE_LOG("[FBX Scene] Collecting materials and textures...");

    int MaterialCount = Scene->GetMaterialCount();
    UE_LOG("[FBX Scene] Total material count: %d", MaterialCount);

    // FBX 파일 디렉토리 (상대경로 처리용)
    std::filesystem::path FbxDir = std::filesystem::path(NormalizedPathStr).parent_path();
    FString FbxDirStr = NormalizePath(FbxDir.string());

    // Lambda: 텍스처 경로 추출 헬퍼 함수
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
                const char* RelativeFileName = FileTexture->GetRelativeFileName();

                if (FileName && strlen(FileName) > 0)
                {
                    TexturePath = FileName;
                }
                else if (RelativeFileName && strlen(RelativeFileName) > 0)
                {
                    TexturePath = RelativeFileName;
                }

                // 상대경로 처리
                if (!TexturePath.empty())
                {
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

        // Layered Texture 확인
        if (TexturePath.empty())
        {
            int LayeredTextureCount = Prop.GetSrcObjectCount<FbxLayeredTexture>();
            if (LayeredTextureCount > 0)
            {
                FbxLayeredTexture* LayeredTexture = Prop.GetSrcObject<FbxLayeredTexture>(0);
                if (LayeredTexture)
                {
                    int TexCount = LayeredTexture->GetSrcObjectCount<FbxTexture>();
                    for (int j = 0; j < TexCount; j++)
                    {
                        FbxFileTexture* FileTexture = FbxCast<FbxFileTexture>(LayeredTexture->GetSrcObject<FbxTexture>(j));
                        if (FileTexture)
                        {
                            const char* FileName = FileTexture->GetFileName();
                            if (FileName && strlen(FileName) > 0)
                            {
                                TexturePath = NormalizePath(FString(FileName));
                                break;
                            }
                        }
                    }
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

        FString MaterialName = Material->GetName();
        int64 MaterialID = Material->GetUniqueID();
        UE_LOG("[FBX Material %d] Name='%s', UniqueID=%lld", i, MaterialName.c_str(), MaterialID);

        // FMaterialInfo 생성
        FMaterialInfo MatInfo;
        MatInfo.MaterialName = MaterialName;

        // 1. Diffuse (Albedo) - 가장 중요
        FbxProperty DiffuseProp = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
        if (DiffuseProp.IsValid())
        {
            // Color 값
            FbxDouble3 DiffuseColor = DiffuseProp.Get<FbxDouble3>();
            MatInfo.DiffuseColor = FVector(static_cast<float>(DiffuseColor[0]),
                                           static_cast<float>(DiffuseColor[1]),
                                           static_cast<float>(DiffuseColor[2]));

            // Texture
            MatInfo.DiffuseTextureFileName = ExtractTexturePath(DiffuseProp);
            if (!MatInfo.DiffuseTextureFileName.empty())
            {
                UE_LOG("[FBX Material] Diffuse Texture: %s", MatInfo.DiffuseTextureFileName.c_str());
            }
        }

        // 2. Normal Map
        FbxProperty NormalProp = Material->FindProperty(FbxSurfaceMaterial::sNormalMap);
        if (NormalProp.IsValid())
        {
            MatInfo.NormalTextureFileName = ExtractTexturePath(NormalProp);
            if (!MatInfo.NormalTextureFileName.empty())
            {
                UE_LOG("[FBX Material] Normal Map: %s", MatInfo.NormalTextureFileName.c_str());
            }
        }

        // Bump로도 시도
        if (MatInfo.NormalTextureFileName.empty())
        {
            FbxProperty BumpProp = Material->FindProperty(FbxSurfaceMaterial::sBump);
            if (BumpProp.IsValid())
            {
                MatInfo.NormalTextureFileName = ExtractTexturePath(BumpProp);
                if (!MatInfo.NormalTextureFileName.empty())
                {
                    UE_LOG("[FBX Material] Bump Map (as Normal): %s", MatInfo.NormalTextureFileName.c_str());
                }
            }
        }

        // 3. Specular
        FbxProperty SpecularProp = Material->FindProperty(FbxSurfaceMaterial::sSpecular);
        if (SpecularProp.IsValid())
        {
            FbxDouble3 SpecularColor = SpecularProp.Get<FbxDouble3>();
            MatInfo.SpecularColor = FVector(static_cast<float>(SpecularColor[0]),
                                            static_cast<float>(SpecularColor[1]),
                                            static_cast<float>(SpecularColor[2]));

            MatInfo.SpecularTextureFileName = ExtractTexturePath(SpecularProp);
            if (!MatInfo.SpecularTextureFileName.empty())
            {
                UE_LOG("[FBX Material] Specular Texture: %s", MatInfo.SpecularTextureFileName.c_str());
            }
        }

        // 4. Emissive
        FbxProperty EmissiveProp = Material->FindProperty(FbxSurfaceMaterial::sEmissive);
        if (EmissiveProp.IsValid())
        {
            FbxDouble3 EmissiveColor = EmissiveProp.Get<FbxDouble3>();
            MatInfo.EmissiveColor = FVector(static_cast<float>(EmissiveColor[0]),
                                            static_cast<float>(EmissiveColor[1]),
                                            static_cast<float>(EmissiveColor[2]));

            MatInfo.EmissiveTextureFileName = ExtractTexturePath(EmissiveProp);
            if (!MatInfo.EmissiveTextureFileName.empty())
            {
                UE_LOG("[FBX Material] Emissive Texture: %s", MatInfo.EmissiveTextureFileName.c_str());
            }
        }

        // 5. Ambient
        FbxProperty AmbientProp = Material->FindProperty(FbxSurfaceMaterial::sAmbient);
        if (AmbientProp.IsValid())
        {
            FbxDouble3 AmbientColor = AmbientProp.Get<FbxDouble3>();
            MatInfo.AmbientColor = FVector(static_cast<float>(AmbientColor[0]),
                                           static_cast<float>(AmbientColor[1]),
                                           static_cast<float>(AmbientColor[2]));

            MatInfo.AmbientTextureFileName = ExtractTexturePath(AmbientProp);
        }

        // 6. Transparency
        FbxProperty TransparentProp = Material->FindProperty(FbxSurfaceMaterial::sTransparentColor);
        if (TransparentProp.IsValid())
        {
            MatInfo.TransparencyTextureFileName = ExtractTexturePath(TransparentProp);
        }

        // 7. Shininess (Specular Exponent)
        FbxProperty ShininessProp = Material->FindProperty(FbxSurfaceMaterial::sShininess);
        if (ShininessProp.IsValid())
        {
            MatInfo.SpecularExponent = static_cast<float>(ShininessProp.Get<FbxDouble>());
        }

        // MaterialIDToInfoMap에 저장
        MaterialIDToInfoMap.Add(MaterialID, MatInfo);
        UE_LOG("[FBX Material] Registered MaterialInfo for '%s'", MaterialName.c_str());
    }

    // 모든 FMaterialInfo를 UMaterial로 변환하여 ResourceManager에 등록
    UShader* DefaultShader = nullptr;
    UMaterial* DefaultMaterial = UResourceManager::GetInstance().GetDefaultMaterial();
    if (DefaultMaterial)
    {
        DefaultShader = DefaultMaterial->GetShader();
    }

    if (!DefaultShader)
    {
        UE_LOG("[FBX Material] WARNING: Default shader not found!");
    }

    for (auto& Pair : MaterialIDToInfoMap)
    {
        const FMaterialInfo& MatInfo = Pair.second;

        // 이미 등록된 Material인지 확인
        if (!UResourceManager::GetInstance().Get<UMaterial>(MatInfo.MaterialName))
        {
            UMaterial* NewMaterial = NewObject<UMaterial>();
            NewMaterial->SetMaterialInfo(MatInfo);

            if (DefaultMaterial)
            {
                NewMaterial->SetShaderMacros(DefaultMaterial->GetShaderMacros());
            }

            if (!NewMaterial->GetShader() && DefaultShader)
            {
                NewMaterial->SetShader(DefaultShader);
            }

            UResourceManager::GetInstance().Add<UMaterial>(MatInfo.MaterialName, NewMaterial);
            UE_LOG("[FBX Material] Created and registered UMaterial: '%s'", MatInfo.MaterialName.c_str());

            // 등록 직후 확인
            UMaterial* TestGet = UResourceManager::GetInstance().Get<UMaterial>(MatInfo.MaterialName);
            if (TestGet)
            {
                UE_LOG("[FBX Material] Verification: Material '%s' found in ResourceManager", MatInfo.MaterialName.c_str());
            }
            else
            {
                UE_LOG("[FBX Material] ERROR: Material '%s' NOT found after registration!", MatInfo.MaterialName.c_str());
            }
        }
        else
        {
            UE_LOG("[FBX Material] Material '%s' already exists in ResourceManager", MatInfo.MaterialName.c_str());
        }
    }

    // Root 노드 가져오기
    FbxNode* RootNode = Scene->GetRootNode();
    if (!RootNode)
    {
        UE_LOG("No root node in FBX scene: %s", NormalizedPathStr.c_str());
        Scene->Destroy();
        delete NewSkeletalMesh;
        return nullptr;
    }

    // Skeleton 추출 (첫 번째 Skeleton 노드 찾기)
    UBone* RootBone = nullptr;
    for (int i = 0; i < RootNode->GetChildCount(); i++)
    {
        FbxNode* ChildNode = RootNode->GetChild(i);
        FbxNodeAttribute* Attr = ChildNode->GetNodeAttribute();

        if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
        {
            RootBone = ProcessSkeletonNode(ChildNode);
            break;
        }
    }

    // Skeleton이 발견되었으면 USkeleton 생성
    if (RootBone)
    {
        NewSkeletalMesh->Skeleton = NewObject<USkeleton>();
        NewSkeletalMesh->Skeleton->SetRoot(RootBone);
    }
    else
    {
        // Skeleton이 없는 경우 경고 (Static Mesh로 처리될 수 있음)
        UE_LOG("[Warning] No skeleton found in FBX file: %s", NormalizedPathStr.c_str());
    }

    // Mesh 데이터 추출 (모든 Mesh 노드 찾기)
    ProcessMeshNode(RootNode, NewSkeletalMesh, MaterialIDToInfoMap);

    // 맵에 저장
    FbxSkeletalMeshMap.Add(NormalizedPathStr, NewSkeletalMesh);

    Scene->Destroy();
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

    // 2) 없으면 새로 로드 (정규화된 경로 사용)
    USkeletalMesh* SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(NormalizedPathStr);

    UE_LOG("USkeletalMesh(filename: \'%s\') is successfully created!", NormalizedPathStr.c_str());
    return SkeletalMesh;
}

// Helper: Skeleton 노드를 재귀적으로 처리하여 UBone 트리 생성
UBone* FFbxManager::ProcessSkeletonNode(FbxNode* InNode, UBone* InParent)
{
    if (!InNode)
        return nullptr;

    // FbxNode Transform을 FTransform으로 변환
    FbxAMatrix LocalTransform = InNode->EvaluateLocalTransform();
    FTransform BoneTransform = ConvertFbxTransform(LocalTransform);

    // UBone 생성 (생성자로 Name과 Transform 초기화)
    FName BoneName(InNode->GetName());
    UBone* NewBone = new UBone(BoneName, BoneTransform);
    // GUObjectArray에 등록
    ObjectFactory::AddToGUObjectArray(UBone::StaticClass(), NewBone);
    // BindPose는 초기 Transform과 동일하게 설정
    NewBone->SetRelativeBindPoseTransform(BoneTransform);

    // 부모 관계 설정
    if (InParent)
    {
        InParent->AddChild(NewBone);
        NewBone->SetParent(InParent);
    }

    // 자식 Skeleton 노드 재귀 처리
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

// Helper: Mesh 노드를 재귀적으로 찾아서 처리
void FFbxManager::ProcessMeshNode(FbxNode* InNode, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap)
{
    if (!InNode || !OutSkeletalMesh)
        return;

    // 현재 노드가 Mesh인지 확인
    FbxNodeAttribute* Attr = InNode->GetNodeAttribute();
    if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
        FbxMesh* Mesh = InNode->GetMesh();
        if (Mesh)
        {
            ExtractMeshData(Mesh, OutSkeletalMesh, MaterialIDToInfoMap);
        }
    }

    // 자식 노드 재귀 처리
    for (int i = 0; i < InNode->GetChildCount(); i++)
    {
        ProcessMeshNode(InNode->GetChild(i), OutSkeletalMesh, MaterialIDToInfoMap);
    }
}

// Helper: FbxMesh에서 Mesh 데이터 및 Skinning 정보 추출
void FFbxManager::ExtractMeshData(FbxMesh* InMesh, FSkeletalMesh* OutSkeletalMesh, const TMap<int64, FMaterialInfo>& MaterialIDToInfoMap)
{
    if (!InMesh || !OutSkeletalMesh)
        return;

    // 기존 버텍스/인덱스 개수 저장 (여러 메시 병합을 위한 오프셋)
    uint32 BaseVertexOffset = static_cast<uint32>(OutSkeletalMesh->Vertices.size());
    uint32 BaseIndexOffset = static_cast<uint32>(OutSkeletalMesh->Indices.size());

    // Vertex 데이터 추출
    int VertexCount = InMesh->GetControlPointsCount();
    FbxVector4* ControlPoints = InMesh->GetControlPoints();

    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;

    // Polygon (Triangle) 순회
    int PolygonCount = InMesh->GetPolygonCount();

    // PolyIdx -> 각 정점의 실제 인덱스 매핑 (3개씩)
    TMap<int, TArray<uint32>> PolyIdxToVertexIndices;
    int ProcessedTriangleCount = 0;

    // 폴리곤 타입 통계
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

        // 3개 미만은 유효하지 않음
        if (PolySize < 3)
        {
            UE_LOG("[Warning] Invalid polygon with %d vertices at index %d - skipping", PolySize, PolyIdx);
            continue;
        }

        // Fan Triangulation: N-gon을 (N-2)개의 삼각형으로 분할
        // Triangle: 1개, Quad: 2개, Pentagon: 3개, Hexagon: 4개, ...
        // 분할 패턴: (0,1,2), (0,2,3), (0,3,4), ...
        int NumTriangles = PolySize - 2;

        for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
        {
            TArray<uint32> TriangleIndices;

            // Fan Triangulation: 모든 삼각형이 버텍스 0을 공유
            // 삼각형 0: (0, 1, 2)
            // 삼각형 1: (0, 2, 3)
            // 삼각형 2: (0, 3, 4)
            // ...
            // Y축 반전으로 좌표계가 RH->LH로 변환되므로 winding order도 반전 (0,2,1)
            int V0 = 0;
            int V1 = TriIdx + 2;  // 반전: 1과 2를 바꿈
            int V2 = TriIdx + 1;

            for (int LocalVertIdx : {V0, V1, V2})
            {
                int ControlPointIdx = InMesh->GetPolygonVertex(PolyIdx, LocalVertIdx);

                FNormalVertex Vertex;

                // Position - Z-Up RH to Z-Up LH: Y축 반전
                FbxVector4 Pos = ControlPoints[ControlPointIdx];
                Vertex.pos = FVector(static_cast<float>(Pos[0]),
                                     static_cast<float>(-Pos[1]),  // Y축 반전
                                     static_cast<float>(Pos[2]));

                // Normal - Z-Up RH to Z-Up LH: Y축 반전
                FbxVector4 Normal;
                if (InMesh->GetPolygonVertexNormal(PolyIdx, LocalVertIdx, Normal))
                {
                    Vertex.normal = FVector(static_cast<float>(Normal[0]),
                                            static_cast<float>(-Normal[1]),  // Y축 반전
                                            static_cast<float>(Normal[2]));
                }

                // UV - DirectX는 V축이 위에서 아래로 증가
                FbxStringList UVSetNames;
                InMesh->GetUVSetNames(UVSetNames);
                if (UVSetNames.GetCount() > 0)
                {
                    FbxVector2 UV;
                    bool bUnmapped;
                    if (InMesh->GetPolygonVertexUV(PolyIdx, LocalVertIdx, UVSetNames[0], UV, bUnmapped))
                    {
                        Vertex.tex = FVector2D(static_cast<float>(UV[0]),
                                               1.0f - static_cast<float>(UV[1]));
                    }
                }

                // 버텍스 추가 및 인덱스 저장
                uint32 VertexIndex = static_cast<uint32>(Vertices.size());
                Vertices.Add(Vertex);
                TriangleIndices.Add(VertexIndex);
            }

            // 이 삼각형의 3개 인덱스를 저장
            // Quad의 경우 두 삼각형에 대해 다른 키 사용 (PolyIdx * 100 + TriIdx)
            int StorageKey = PolyIdx * 100 + TriIdx;
            PolyIdxToVertexIndices.Add(StorageKey, TriangleIndices);
            ProcessedTriangleCount++;
        }
    }


    // Material별로 SubMesh(Section) 분리
    FbxLayerElementMaterial* MaterialElement = InMesh->GetElementMaterial();
    TMap<int, TArray<int>> MaterialToTriangles; // Material Index -> Triangle Indices (PolyIdx)

    if (MaterialElement)
    {
        // 각 Triangle이 어느 Material에 속하는지 매핑
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

            // Fan Triangulation: N-gon → (N-2)개의 삼각형
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
        // Material이 없으면 모든 삼각형을 하나의 SubMesh로 처리
        TArray<int> AllTriangles;
        for (int PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
        {
            int PolySize = InMesh->GetPolygonSize(PolyIdx);
            if (PolySize < 3)
                continue;

            // Fan Triangulation: N-gon → (N-2)개의 삼각형
            int NumTriangles = PolySize - 2;
            for (int TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
            {
                int StorageKey = PolyIdx * 100 + TriIdx;
                AllTriangles.Add(StorageKey);
            }
        }
        MaterialToTriangles.Add(0, AllTriangles);
    }

    // Material Index로 정렬 (TMap은 순서 보장 안 됨)
    TArray<int> SortedMaterialIndices;
    for (auto& Pair : MaterialToTriangles)
    {
        SortedMaterialIndices.Add(Pair.first);
    }
    SortedMaterialIndices.Sort();

    // Bone Map 생성 (Skeleton에서 이름으로 Bone 찾기용)
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

    // Material별로 Indices 재구성 + Flesh 생성 (정렬된 순서로)
    TArray<uint32> ReorderedIndices;
    ReorderedIndices.reserve(Indices.Num());
    uint32 CurrentStartIndex = BaseIndexOffset; // 기존 인덱스 버퍼 크기만큼 오프셋

    for (int MaterialIndex : SortedMaterialIndices)
    {
        TArray<int>& TriangleIndices = MaterialToTriangles[MaterialIndex];

        FFlesh NewFlesh;
        NewFlesh.StartIndex = CurrentStartIndex;
        NewFlesh.IndexCount = 0; // 실제로 추가된 인덱스 개수로 계산

        // 각 Triangle의 인덱스를 순서대로 추가
        for (int StorageKey : TriangleIndices)
        {
            // StorageKey로 3개 버텍스 인덱스 가져오기
            TArray<uint32>* VertexIndicesPtr = PolyIdxToVertexIndices.Find(StorageKey);
            if (!VertexIndicesPtr || VertexIndicesPtr->Num() != 3)
                continue; // 이 삼각형은 처리되지 않았음

            const TArray<uint32>& VertexIndices = *VertexIndicesPtr;

            // BaseVertexOffset 적용하여 인덱스 추가
            ReorderedIndices.Add(VertexIndices[0] + BaseVertexOffset);
            ReorderedIndices.Add(VertexIndices[1] + BaseVertexOffset);
            ReorderedIndices.Add(VertexIndices[2] + BaseVertexOffset);
            NewFlesh.IndexCount += 3;
        }

        // Material 이름 설정 (UniqueID 기반 매칭)
        if (MaterialElement && MaterialIndex < InMesh->GetNode()->GetMaterialCount())
        {
            FbxSurfaceMaterial* Material = InMesh->GetNode()->GetMaterial(MaterialIndex);
            if (Material)
            {
                FString MaterialName = Material->GetName();
                int64 MaterialID = Material->GetUniqueID();

                // Scene에서 수집한 MaterialIDToInfoMap에서 UniqueID로 검색
                if (const FMaterialInfo* FoundMatInfo = MaterialIDToInfoMap.Find(MaterialID))
                {
                    // Material 이름 사용 (UResourceManager에 이미 등록됨)
                    NewFlesh.InitialMaterialName = FoundMatInfo->MaterialName;
                    UE_LOG("[Flesh Material] Assigned Material '%s' to Flesh (StartIndex=%d, IndexCount=%d)",
                           NewFlesh.InitialMaterialName.c_str(), NewFlesh.StartIndex, NewFlesh.IndexCount);
                }
                else
                {
                    // 못 찾은 경우 Material 이름 그대로 사용
                    NewFlesh.InitialMaterialName = MaterialName;
                    UE_LOG("[Flesh Material] Material[%lld]='%s' not found in map - using name directly",
                           MaterialID, MaterialName.c_str());
                }
            }
        }
        else
        {
            // Material이 없으면 기본 Material 사용
            NewFlesh.InitialMaterialName = "DefaultMaterial";
            UE_LOG("[Flesh Material] No material assigned - using DefaultMaterial");
        }

        // Skinning 데이터 추출
        ExtractSkinningData(InMesh, NewFlesh, BoneMap);

        OutSkeletalMesh->Fleshes.Add(NewFlesh);
        CurrentStartIndex += NewFlesh.IndexCount;
    }

    // FSkeletalMesh 기본 데이터 추가 (여러 메시 병합 지원)
    OutSkeletalMesh->Vertices.insert(OutSkeletalMesh->Vertices.end(), Vertices.begin(), Vertices.end());
    OutSkeletalMesh->Indices.insert(OutSkeletalMesh->Indices.end(), ReorderedIndices.begin(), ReorderedIndices.end());
    OutSkeletalMesh->bHasMaterial = OutSkeletalMesh->bHasMaterial || (InMesh->GetElementMaterialCount() > 0);
}

// Helper: Skinning 정보 추출
void FFbxManager::ExtractSkinningData(FbxMesh* InMesh, FFlesh& OutFlesh, const TMap<FString, UBone*>& BoneMap)
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

            // Weight 계산 (모든 Control Point의 평균 Weight)
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

// Helper: FbxAMatrix를 FTransform으로 변환
FTransform FFbxManager::ConvertFbxTransform(const FbxAMatrix& InMatrix)
{
    FTransform Result;

    // Translation
    FbxVector4 Translation = InMatrix.GetT();
    Result.Translation = FVector(static_cast<float>(Translation[0]),
                                  static_cast<float>(Translation[1]),
                                  static_cast<float>(Translation[2]));

    // Rotation (Quaternion)
    FbxQuaternion Rotation = InMatrix.GetQ();
    Result.Rotation = FQuat(static_cast<float>(Rotation[0]),
                            static_cast<float>(Rotation[1]),
                            static_cast<float>(Rotation[2]),
                            static_cast<float>(Rotation[3]));

    // Scale
    FbxVector4 Scale = InMatrix.GetS();
    Result.Scale3D = FVector(static_cast<float>(Scale[0]),
                             static_cast<float>(Scale[1]),
                             static_cast<float>(Scale[2]));

    return Result;
}