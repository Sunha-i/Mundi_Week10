#pragma once
#include "ResourceBase.h"

struct FBoneInfo
{
	FString Name;
	int32 ParentIndex;
	FMatrix BindPose;
	FMatrix InverseBindpose;
};

struct FSkinnedVertex
{
	FVector Position;
	FVector Normal;
	FVector2D TexCoord;

	int BoneIndices[4];
	float BoneWeights[4];
};

struct FSkeletalMesh
{
	FString PathFileName;
	FString CacheFilePath;	// Cached source path (ex: DerivedDataCache/character.skm.bin)

	TArray<FSkinnedVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FBoneInfo> Bones;

	bool bHasSkinning = false;
	bool bHasNormals = false;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalMesh& Mesh)
	{
		if (Ar.IsSaving())
		{
			Serialization::WriteString(Ar, Mesh.PathFileName);
			Serialization::WriteArray(Ar, Mesh.Vertices);
			Serialization::WriteArray(Ar, Mesh.Indices);
			Serialization::WriteArray(Ar, Mesh.Bones);
			Ar << Mesh.bHasSkinning;
			Ar << Mesh.bHasNormals;
		}
		else if (Ar.IsLoading())
		{
			Serialization::ReadString(Ar, Mesh.PathFileName);
			Serialization::ReadArray(Ar, Mesh.Vertices);
			Serialization::ReadArray(Ar, Mesh.Indices);
			Serialization::ReadArray(Ar, Mesh.Bones);
			Ar << Mesh.bHasSkinning;
			Ar << Mesh.bHasNormals;
		}
		return Ar;
	}
};

class USkeletalMesh : public UResourceBase
{
public:
	DECLARE_CLASS(USkeletalMesh, UResourceBase)

	void Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType = EVertexLayoutType::PositionNormalTexSkinned);
	void Release();

	ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer; }
	ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }

	uint32 GetVertexCount() const { return VertexCount; }
	uint32 GetIndexCount() const { return IndexCount; }

private:
	void CreateVertexBuffer(FSkeletalMesh* InStaticMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType);
	void CreateIndexBuffer(FSkeletalMesh* InStaticMesh, ID3D11Device* InDevice);
	void ReleaseResources();

	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
	uint32 VertexCount = 0;
	uint32 IndexCount = 0;

	FString CacheFilePath;  // Cached Source Path (ex: DerivedDataCache/human.skm.bin)
	FSkeletalMesh* SkeletalMeshAsset = nullptr;
};