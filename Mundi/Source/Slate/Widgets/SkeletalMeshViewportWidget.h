#pragma once
#include "Widgets/Widget.h"
#include "FPreviewScene.h"
#include "FViewport.h"

class USkeletalMesh;
class ASkeletalMeshActor;
class USkeletalMeshComponent;
class ULineComponent;

class USkeletalMeshViewportWidget : public UWidget
{
public:
	DECLARE_CLASS(USkeletalMeshViewportWidget, UWidget)

	USkeletalMeshViewportWidget();
	~USkeletalMeshViewportWidget();

	void SetSkeletalMeshToViewport(const FName& InTargetMeshName);

	void RenderWidget() override;

private:
	void RenderBoneHierarchyPanel(float Width, float Height);
	void RenderViewportPanel(float Width, float Height);
	void RenderBoneInformationPanel(float Width, float Height);

	// Bone Hierarchy 재귀 렌더링
	void RenderBoneNode(class UBone* Bone);

	// Skeleton overlay helpers
	void ToggleSkeletonOverlay(bool bEnable);
	void UpdateSkeletonOverlayIfNeeded();
	void EnsureSkeletonLineComponent(class USkeletalMeshComponent* MeshComponent);
	void BuildSkeletonLinesRecursive(class UBone* Bone, const FTransform& ComponentWorldInverse);
	void ClearSkeletonOverlay(bool bReleaseComponent);
	void MarkSkeletonOverlayDirty();
	ASkeletalMeshActor* GetPreviewActor() const { return PreviewActor; }
	bool HasLoadedSkeletalMesh() const;

	// Preview RenderTarget 관리
	bool CreatePreviewRenderTarget(uint32 Width, uint32 Height);
	void ReleasePreviewRenderTarget();

public:
	FName TargetMeshName{};
	FPreviewScene WorldForPreviewManager;
	FViewport Viewport;

private:
	inline const static uint32 DEFAULT_VIEWPORT_WIDTH = 512;
	inline const static uint32 DEFAULT_VIEWPORT_HEIGHT = 512;

	bool bShowSkeletonOverlay = false;
	bool bSkeletonLinesDirty = false;

	ASkeletalMeshActor* PreviewActor = nullptr;
	ULineComponent* SkeletonLineComponent = nullptr;

	// Preview용 독립 RenderTarget
	ID3D11Texture2D* PreviewTexture = nullptr;
	ID3D11ShaderResourceView* PreviewSRV = nullptr;
	uint32 PreviewTextureWidth = 0;
	uint32 PreviewTextureHeight = 0;

	// 선택된 Bone
	class UBone* SelectedBone = nullptr;
};
