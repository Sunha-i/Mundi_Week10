#include "pch.h"
#include "SkeletalMeshViewportWidget.h"

#include "FViewportClient.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
	WorldForPreviewManager.CreateWorldForPreviewScene();
	WorldForPreviewManager.SetCamera(
		{-5.f, 0.f, 0.f},
		FVector::Zero()
	);
	WorldForPreviewManager.SetDirectionalLight(
		{30.f, 0.f, 30.f}
	);

	Viewport.SetViewportClient(NewObject<FViewportClient>());
	Viewport.GetViewportClient()->SetWorld(
		WorldForPreviewManager.GetWorldForPreview()
	);
	Viewport.Resize(
		0,
		0,
		DEFAULT_VIEWPORT_WIDTH,
		DEFAULT_VIEWPORT_HEIGHT
	);
}

USkeletalMeshViewportWidget::~USkeletalMeshViewportWidget()
{
	WorldForPreviewManager.DestroyWorldForPreviewScene();
	DeleteObject(Viewport.GetViewportClient());
}

void USkeletalMeshViewportWidget::SetSkeletalMeshToViewport(const FName& InTargetMeshName)
{
	TargetMeshName = InTargetMeshName;

	ASkeletalMeshActor* SkeletalMeshActor = NewObject<ASkeletalMeshActor>();
	SkeletalMeshActor->GetSkeletalMeshComponent()->SetSkeletalMesh(
		TargetMeshName.ToString()
	);
	WorldForPreviewManager.SetActor(SkeletalMeshActor);
}

void USkeletalMeshViewportWidget::RenderWidget()
{
	if (TargetMeshName.Empty())
	{
		ImGui::Text("No mesh specified for this viewer.");
		return;
	}

	// 전체 사용 가능한 영역 크기 가져오기
	ImVec2 AvailableSize = ImGui::GetContentRegionAvail();

	// 레이아웃 설정: 좌측 25%, 중앙 50%, 우측 25%
	float LeftWidth = AvailableSize.x * 0.25f;
	float CenterWidth = AvailableSize.x * 0.5f;
	float RightWidth = AvailableSize.x * 0.25f;

	// 좌측 패널 - Bone Hierarchy
	RenderBoneHierarchyPanel(LeftWidth, AvailableSize.y);
	ImGui::SameLine();

	// 중앙 패널 - Viewport
	RenderViewportPanel(CenterWidth, AvailableSize.y);
	ImGui::SameLine();

	// 우측 패널 - Bone Information
	RenderBoneInformationPanel(RightWidth, AvailableSize.y);
}

void USkeletalMeshViewportWidget::RenderBoneHierarchyPanel(float Width, float Height)
{
	ImGui::BeginChild("BoneHierarchy", ImVec2(Width, Height), true);
	{
		ImGui::Text("Bone Hierarchy");
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(Coming soon...)");
		// TODO: Bone hierarchy tree will be rendered here
	}
	ImGui::EndChild();
}

void USkeletalMeshViewportWidget::RenderViewportPanel(float Width, float Height)
{
	ImGui::BeginChild("SkeletalMeshViewport", ImVec2(Width, Height), true);
	{
		ImGui::Text("Skeletal Mesh Preview");
		ImGui::Separator();
		ImGui::Text("Mesh: %s", TargetMeshName.ToString().c_str());
		ImGui::Separator();

		// 렌더링 영역 (나중에 렌더 타겟 표시)
		ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
		ImGui::BeginChild("ViewportRenderArea", ViewportSize, false);
		{
			ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Viewport Render Target");
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(Will render skeletal mesh here)");
			ImGui::Text("Size: %.0f x %.0f", ViewportSize.x, ViewportSize.y);
		}
		ImGui::EndChild();
	}
	ImGui::EndChild();
}

void USkeletalMeshViewportWidget::RenderBoneInformationPanel(float Width, float Height)
{
	ImGui::BeginChild("BoneInformation", ImVec2(Width, Height), true);
	{
		ImGui::Text("Bone Information");
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(Coming soon...)");
		// TODO: Selected bone information will be displayed here
	}
	ImGui::EndChild();
}