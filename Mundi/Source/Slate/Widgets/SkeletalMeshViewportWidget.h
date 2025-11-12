#pragma once
#include "Widgets/Widget.h"
#include "SkelMeshPreviewScene.h"
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
    void ClearSkeletonOverlay(bool bReleaseComponent);
    void MarkSkeletonOverlayDirty();
	ASkeletalMeshActor* GetPreviewActor() const { return PreviewActor; }
	bool HasLoadedSkeletalMesh() const;

	// Preview RenderTarget 관리
	bool CreatePreviewRenderTarget(uint32 Width, uint32 Height);
	void ReleasePreviewRenderTarget();
	
	// About Gizmo
	void UpdateGizmoVisibility();
	void UpdateGizmoTransform();

	// Bone picking
	UBone* PickBoneFromViewport(const FVector2D& ViewportMousePos, const FVector2D& ViewportSize);
	void CollectAllBones(UBone* Bone, TArray<UBone*>& OutBones);

	// Picking thresholds
	float GetJointPickRadius() const { return 0.05f; }		// Joint selection radius
	float GetBoneLinePickThreshold() const { return 0.03f; }  // Bone line selection distance threshold

public:
	FName TargetMeshName{};
	FSkeletalMeshPreviewScene WorldForPreviewManager;
	FViewport Viewport;

private:
	inline const static uint32 DEFAULT_VIEWPORT_WIDTH = 512;
	inline const static uint32 DEFAULT_VIEWPORT_HEIGHT = 512;

	bool bShowSkeletonOverlay = false;
    bool bSkeletonLinesDirty = false;

    ASkeletalMeshActor* PreviewActor = nullptr;
	// PreviewScene의 카메라
	ACameraActor* PreviewCamera;
	bool bIsDraggingViewport;	// store dragging status

	// Preview용 독립 RenderTarget
	ID3D11Texture2D* PreviewTexture = nullptr;
	ID3D11ShaderResourceView* PreviewSRV = nullptr;
	uint32 PreviewTextureWidth = 0;
	uint32 PreviewTextureHeight = 0;

	// SelectedInfo
	class UBone* SelectedBone = nullptr;
	EGizmoMode CurrentGizmoMode = EGizmoMode::Translate;
	EGizmoSpace CurrentGizmoSpace = EGizmoSpace::Local;

	// GizmoInteractionStatus
	bool bIsGizmoDragging = false;
	uint32 HoveredGizmoAxis = 0;
	uint32 DraggingGizmoAxis = 0;

	// DragStartStatusInfo
	FTransform DragStartBoneTransfrom;
	FVector2D DragStartMousePosition;
	FVector DragImpactPoint;
	FVector2D DragScreenVector;

	// Performance optimization: throttle skeleton overlay updates during drag
	int OverlayUpdateFrameCounter = 0;
	const int OVERLAY_UPDATE_INTERVAL = 5;  // Update every 5 frames during drag
};
