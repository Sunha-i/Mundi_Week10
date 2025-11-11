#include "pch.h"
#include "SkeletalMeshViewportWidget.h"

#include "FViewportClient.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"
#include "RenderManager.h"
#include "D3D11RHI.h"
#include "SceneRenderer.h"
#include "SceneView.h"
#include "Renderer.h"
#include "CameraActor.h"
#include "CameraComponent.h"
#include "Skeleton.h"
#include "Bone.h"
#include "SkeletalMesh.h"

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
	WorldForPreviewManager.CreateWorldForPreviewScene();
	WorldForPreviewManager.SetDirectionalLight(
		{0.f, 90.f, 0.f}
	);

	// ViewportClient 생성 (내부적으로 Camera 생성)
	Viewport.SetViewportClient(new FViewportClient());
	Viewport.GetViewportClient()->SetWorld(
		WorldForPreviewManager.GetWorldForPreview()
	);

	// ViewportClient의 Camera를 PreviewScene에 등록
	ACameraActor* Camera = Viewport.GetViewportClient()->GetCamera();
	if (Camera)
	{
		Camera->SetActorLocation({0.f, -70.f, 200.f});
		Camera->SetRotationFromEulerAngles({0.f, 90.f, -90.f});
		WorldForPreviewManager.SetCamera(Camera);
	}

	Viewport.Resize(
		0,
		0,
		DEFAULT_VIEWPORT_WIDTH,
		DEFAULT_VIEWPORT_HEIGHT
	);

	// 고정 크기로 PreviewRenderTarget 생성
	if (!CreatePreviewRenderTarget(DEFAULT_VIEWPORT_WIDTH, DEFAULT_VIEWPORT_HEIGHT))
	{
		UE_LOG("[SkeletalMeshViewportWidget] Error: Failed to create preview render target");
	}
	else
	{
		UE_LOG("[SkeletalMeshViewportWidget] Preview render target created successfully (512x512)");
	}
}

USkeletalMeshViewportWidget::~USkeletalMeshViewportWidget()
{
	ReleasePreviewRenderTarget();
	WorldForPreviewManager.DestroyWorldForPreviewScene();

	// FViewportClient는 일반 C++ 클래스이므로 delete 사용
	delete Viewport.GetViewportClient();
	Viewport.SetViewportClient(nullptr);
}

void USkeletalMeshViewportWidget::SetSkeletalMeshToViewport(const FName& InTargetMeshName)
{
	TargetMeshName = InTargetMeshName;

	UE_LOG("[SkeletalMeshViewportWidget] Setting skeletal mesh: %s", TargetMeshName.ToString().c_str());

	UWorld* PreviewWorld = WorldForPreviewManager.GetWorldForPreview();
	if (!PreviewWorld)
	{
		UE_LOG("[SkeletalMeshViewportWidget] Error: Preview World is null");
		return;
	}

	// Actor 생성 및 Mesh 설정
	ASkeletalMeshActor* SkeletalMeshActor = NewObject<ASkeletalMeshActor>();

	USkeletalMeshComponent* MeshComp = SkeletalMeshActor->GetSkeletalMeshComponent();
	if (MeshComp)
	{
		MeshComp->SetSkeletalMesh(TargetMeshName.ToString());
	}
	else
	{
		UE_LOG("[SkeletalMeshViewportWidget] Error: SkeletalMeshComponent is null");
	}

	WorldForPreviewManager.SetActor(SkeletalMeshActor);
}

void USkeletalMeshViewportWidget::Update()
{
	const float DeltaSeconds = GWorld->GetDeltaTime(EDeltaTime::Unscaled);
	FViewportClient* Client = Viewport.GetViewportClient();
	if (Client)
	{
		Client->Tick(DeltaSeconds);
	}

	UWorld* PreviewWorld = WorldForPreviewManager.GetWorldForPreview();
	if (PreviewWorld)
	{
		PreviewWorld->Tick(DeltaSeconds);
	}
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

		// Skeleton 가져오기
		UWorld* PreviewWorld = WorldForPreviewManager.GetWorldForPreview();
		if (!PreviewWorld || PreviewWorld->GetActors().empty())
		{
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No skeleton loaded");
			ImGui::EndChild();
			return;
		}

		// SkeletalMeshActor 찾기
		ASkeletalMeshActor* SkeletalMeshActor = nullptr;
		for (AActor* Actor : PreviewWorld->GetActors())
		{
			if (ASkeletalMeshActor* SkelActor = Cast<ASkeletalMeshActor>(Actor))
			{
				SkeletalMeshActor = SkelActor;
				break;
			}
		}

		if (!SkeletalMeshActor)
		{
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No skeletal mesh actor");
			ImGui::EndChild();
			return;
		}

		USkeletalMeshComponent* MeshComp = SkeletalMeshActor->GetSkeletalMeshComponent();
		if (!MeshComp || !MeshComp->GetSkeletalMesh())
		{
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No skeletal mesh component");
			ImGui::EndChild();
			return;
		}

		FSkeletalMesh* SkeletalMesh = MeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset();
		USkeleton* Skeleton = SkeletalMesh->Skeleton;
		if (!Skeleton || !Skeleton->GetRoot())
		{
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No skeleton root");
			ImGui::EndChild();
			return;
		}

		// Root Bone부터 재귀적으로 렌더링
		RenderBoneNode(Skeleton->GetRoot());
	}
	ImGui::EndChild();
}

void USkeletalMeshViewportWidget::RenderBoneNode(UBone* Bone)
{
	if (!Bone) return;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

	// 선택된 Bone이면 하이라이트
	if (Bone == SelectedBone)
		flags |= ImGuiTreeNodeFlags_Selected;

	// 자식이 없으면 Leaf 플래그 추가
	if (Bone->GetChildren().empty())
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	bool node_open = ImGui::TreeNodeEx(Bone->GetName().ToString().c_str(), flags);

	// 클릭 감지
	if (ImGui::IsItemClicked())
	{
		SelectedBone = Bone;
	}

	// 자식이 있으면 재귀적으로 렌더링
	if (node_open && !Bone->GetChildren().empty())
	{
		for (UBone* Child : Bone->GetChildren())
		{
			RenderBoneNode(Child);
		}
		ImGui::TreePop();
	}
}

void USkeletalMeshViewportWidget::RenderViewportPanel(float Width, float Height)
{
	ImGui::BeginChild("SkeletalMeshViewport", ImVec2(Width, Height), true);
	{
		ImGui::Text("Skeletal Mesh Preview");
		ImGui::Separator();
		ImGui::Text("Mesh: %s", TargetMeshName.ToString().c_str());
		ImGui::Separator();

		// 디버그 정보 표시
		// ImGui::Text("Debug Info:");
		// ImGui::Text("- PreviewSRV: %s", PreviewSRV ? "OK" : "NULL");
		// ImGui::Text("- PreviewTexture: %s", PreviewTexture ? "OK" : "NULL");

		FViewportClient* ViewportClient = Viewport.GetViewportClient();
		ID3D11Texture2D* RenderedTexture = nullptr;

		// ImGui::Text("- ViewportClient: %s", ViewportClient ? "OK" : "NULL");

		RenderedTexture = ViewportClient->DrawToTexture(&Viewport);

		D3D11RHI* RHI = URenderManager::GetInstance().GetRenderer()->GetRHIDevice();

		// 조건 체크 상세
		bool bAllValid = PreviewSRV && RenderedTexture && PreviewTexture && RHI;

		if (bAllValid)
		{
			RHI->CopyTexture(PreviewTexture, RenderedTexture);

			// ImGui에 PreviewSRV 표시 (고정 크기)
			ImVec2 PreviewImageSize(
				static_cast<float>(PreviewTextureWidth),
				static_cast<float>(PreviewTextureHeight)
			);
			ImGui::Image((void*)PreviewSRV, PreviewImageSize);

			if (ImGui::IsItemHovered())
			{
				ImVec2 MousePos = ImGui::GetMousePos();
				ImVec2 ImagePos = ImGui::GetItemRectMin();
				int LocalX = static_cast<int>(MousePos.x - ImagePos.x);
				int LocalY = static_cast<int>(MousePos.y - ImagePos.y);

				ViewportClient->MouseMove(&Viewport, LocalX, LocalY);

				for (int i = 0; i < 3; ++i)    // Left click(0), Right click(1), Wheel click(2)
				{
					if (ImGui::IsMouseDown(i) && ImGui::IsItemClicked(i))
					{
						ViewportClient->MouseButtonDown(&Viewport, LocalX, LocalY, i);
					}
					else if (ImGui::IsMouseReleased(i))
					{
						ViewportClient->MouseButtonUp(&Viewport, LocalX, LocalY, i);
					}
				}
			}
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Cannot display: condition failed");
		}

		// 임시 텍스처 해제 (DrawToTexture에서 생성된 것)
		if (RenderedTexture)
		{
			RenderedTexture->Release();
		}
	}
	ImGui::EndChild();
}

void USkeletalMeshViewportWidget::RenderBoneInformationPanel(float Width, float Height)
{
	ImGui::BeginChild("BoneInformation", ImVec2(Width, Height), true);
	{
		ImGui::Text("Bone Information");
		ImGui::Separator();

		if (!SelectedBone)
		{
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No bone selected");
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click a bone in the hierarchy");
			ImGui::EndChild();
			return;
		}

		// Bone 이름
		ImGui::Text("Bone Name:");
		ImGui::Indent();
		ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "%s", SelectedBone->GetName().ToString().c_str());
		ImGui::Unindent();
		ImGui::Spacing();

		// Parent 이름 (Skeleton에서 찾기)
		ImGui::Text("Parent:");
		ImGui::Indent();

		UWorld* PreviewWorld = WorldForPreviewManager.GetWorldForPreview();
		UBone* ParentBone = nullptr;
		if (PreviewWorld && !PreviewWorld->GetActors().empty())
		{
			for (AActor* Actor : PreviewWorld->GetActors())
			{
				if (ASkeletalMeshActor* SkelActor = Cast<ASkeletalMeshActor>(Actor))
				{
					USkeletalMeshComponent* MeshComp = SkelActor->GetSkeletalMeshComponent();
					if (MeshComp && MeshComp->GetSkeletalMesh())
					{
						USkeleton* Skeleton = MeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset()->Skeleton;
						if (Skeleton)
						{
							// Root부터 재귀적으로 Parent 찾기
							Skeleton->ForEachBone([&](UBone* Bone) {
								for (UBone* Child : Bone->GetChildren())
								{
									if (Child == SelectedBone)
									{
										ParentBone = Bone;
										return;
									}
								}
							});
						}
					}
					break;
				}
			}
		}

		ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "%s", ParentBone ? ParentBone->GetName().ToString().c_str() : "(Root)");
		ImGui::Unindent();
		ImGui::Spacing();

		// Children 목록
		ImGui::Text("Children:");
		ImGui::Indent();
		if (SelectedBone->GetChildren().empty())
		{
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(None)");
		}
		else
		{
			ImGui::BeginChild("ChildrenList", ImVec2(0, 80), true);
			for (UBone* Child : SelectedBone->GetChildren())
			{
				ImGui::BulletText("%s", Child->GetName().ToString().c_str());
			}
			ImGui::EndChild();
		}
		ImGui::Unindent();
		ImGui::Spacing();

		// BindPose
		ImGui::Text("Bind Pose:");
		ImGui::Indent();
		const FTransform& BindPose = SelectedBone->GetRelativeBindPose();
		FVector BindLoc = BindPose.Translation;
		FVector BindRot = BindPose.Rotation.ToEulerZYXDeg();
		FVector BindScale = BindPose.Scale3D;
		ImGui::Text("Location: (%.2f, %.2f, %.2f)", BindLoc.X, BindLoc.Y, BindLoc.Z);
		ImGui::Text("Rotation: (%.2f, %.2f, %.2f)", BindRot.X, BindRot.Y, BindRot.Z);
		ImGui::Text("Scale: (%.2f, %.2f, %.2f)", BindScale.X, BindScale.Y, BindScale.Z);
		ImGui::Unindent();
		ImGui::Spacing();

		// Relative Transform (편집 가능)
		ImGui::Text("Relative Transform:");
		ImGui::Indent();

		FTransform RelTransform = SelectedBone->GetRelativeTransform();
		FVector RelLoc = RelTransform.Translation;
		FVector RelRot = RelTransform.Rotation.ToEulerZYXDeg();
		FVector RelScale = RelTransform.Scale3D;

		bool bChanged = false;

		ImGui::Text("Location:");
		ImGui::PushID("Location");
		bChanged |= ImGui::DragFloat("X##Loc", &RelLoc.X, 0.1f);
		bChanged |= ImGui::DragFloat("Y##Loc", &RelLoc.Y, 0.1f);
		bChanged |= ImGui::DragFloat("Z##Loc", &RelLoc.Z, 0.1f);
		ImGui::PopID();

		ImGui::Text("Rotation:");
		ImGui::PushID("Rotation");
		bChanged |= ImGui::DragFloat("X##Rot", &RelRot.X, 0.5f);
		bChanged |= ImGui::DragFloat("Y##Rot", &RelRot.Y, 0.5f);
		bChanged |= ImGui::DragFloat("Z##Rot", &RelRot.Z, 0.5f);
		ImGui::PopID();

		ImGui::Text("Scale:");
		ImGui::PushID("Scale");
		bChanged |= ImGui::DragFloat("X##Scl", &RelScale.X, 0.01f);
		bChanged |= ImGui::DragFloat("Y##Scl", &RelScale.Y, 0.01f);
		bChanged |= ImGui::DragFloat("Z##Scl", &RelScale.Z, 0.01f);
		ImGui::PopID();

		// 값이 변경되면 적용
		if (bChanged)
		{
			FTransform NewTransform(RelLoc, FQuat::MakeFromEulerZYX(RelRot), RelScale);
			SelectedBone->SetRelativeTransform(NewTransform);
		}

		ImGui::Unindent();
	}
	ImGui::EndChild();
}

bool USkeletalMeshViewportWidget::CreatePreviewRenderTarget(uint32 Width, uint32 Height)
{
	D3D11RHI* RHI = URenderManager::GetInstance().GetRenderer()->GetRHIDevice();
	if (!RHI)
		return false;

	// D3D11RHI의 범용 함수 사용
	HRESULT hr = RHI->CreateTexture2DWithSRV(
		Width, Height,
		DXGI_FORMAT_R8G8B8A8_UNORM,  // SceneColor와 동일한 포맷
		&PreviewTexture,
		&PreviewSRV
	);

	if (SUCCEEDED(hr))
	{
		PreviewTextureWidth = Width;
		PreviewTextureHeight = Height;
		return true;
	}

	return false;
}

void USkeletalMeshViewportWidget::ReleasePreviewRenderTarget()
{
	D3D11RHI* RHI = URenderManager::GetInstance().GetRenderer()->GetRHIDevice();
	if (RHI)
	{
		RHI->ReleaseTexture2DWithSRV(&PreviewTexture, &PreviewSRV);
	}

	PreviewTextureWidth = 0;
	PreviewTextureHeight = 0;
}