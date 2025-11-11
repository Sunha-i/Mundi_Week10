#pragma once
#include "Widgets/Widget.h"
#include "FPreviewScene.h"
#include "FViewport.h"

class USkeletalMesh;
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

	// PreviewScene의 카메라
	ACameraActor* PreviewCamera;

	// Preview용 독립 RenderTarget
	ID3D11Texture2D* PreviewTexture = nullptr;
	ID3D11ShaderResourceView* PreviewSRV = nullptr;
	uint32 PreviewTextureWidth = 0;
	uint32 PreviewTextureHeight = 0;

	// 선택된 Bone
	class UBone* SelectedBone = nullptr;
};