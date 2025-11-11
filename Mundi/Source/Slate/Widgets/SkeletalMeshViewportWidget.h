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
public:
	FName TargetMeshName{};
	FPreviewScene WorldForPreviewManager;
	FViewport Viewport;

private:
	void RenderBoneHierarchyPanel(float Width, float Height);
	void RenderViewportPanel(float Width, float Height);
	void RenderBoneInformationPanel(float Width, float Height);
private:
	inline const static uint32 DEFAULT_VIEWPORT_WIDTH = 512;
	inline const static uint32 DEFAULT_VIEWPORT_HEIGHT = 512;
};