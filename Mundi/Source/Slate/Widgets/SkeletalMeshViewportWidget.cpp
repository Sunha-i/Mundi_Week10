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
#include "LineComponent.h"

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
	WorldForPreviewManager.CreateWorldForPreviewScene();
	WorldForPreviewManager.SetDirectionalLight({ 0.f, 0.f, 180.f }, { 0.f,0.f,-90.f });
	WorldForPreviewManager.SetGizmo();

	// ViewportClient 생성 (내부적으로 Camera 생성)
	Viewport.SetViewportClient(new FViewportClient());
	Viewport.GetViewportClient()->SetWorld(
		WorldForPreviewManager.GetWorldForPreview()
	);

	// ViewportClient의 Camera를 PreviewScene에 등록
	PreviewCamera = Viewport.GetViewportClient()->GetCamera();
	if (PreviewCamera)
	{
		PreviewCamera->SetActorLocation({1.7f, 0.f, 0.7f});
		PreviewCamera->SetRotationFromEulerAngles({0.f, 0.f, 180.f});
		WorldForPreviewManager.SetCamera(PreviewCamera);
	}
	bIsDraggingViewport = false;

	Viewport.Resize(0, 0,
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
	ClearSkeletonOverlay(true);
	if (PreviewActor)
	{
		if (UWorld* PreviewWorld = WorldForPreviewManager.GetWorldForPreview())
		{
			PreviewWorld->RemoveEditorActor(PreviewActor);
		}
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}

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

	if (PreviewActor)
	{
		PreviewWorld->RemoveEditorActor(PreviewActor);
		ClearSkeletonOverlay(true);
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}

	WorldForPreviewManager.SetSkelMeshActor(TargetMeshName.ToString());
	ASkeletalMeshActor* SkeletalMeshActor = WorldForPreviewManager.GetSkelMeshActor();
	PreviewWorld->AddEditorActor(SkeletalMeshActor);
	PreviewActor = SkeletalMeshActor;
	MarkSkeletonOverlayDirty();
	UpdateGizmoVisibility();
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

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;

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
		MarkSkeletonOverlayDirty(); // 선택 변경 시 스켈레톤 오버레이 다시 그리기
		UpdateGizmoTransform();
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

		// Switch mode via space input
		if (ImGui::IsKeyPressed(ImGuiKey_Space))
		{
			int GizmoModeIndex = static_cast<int>(CurrentGizmoMode);
			GizmoModeIndex = (GizmoModeIndex + 1) % static_cast<int>(EGizmoMode::Select);
			CurrentGizmoMode = static_cast<EGizmoMode>(GizmoModeIndex);
			UpdateGizmoVisibility();
		}

		const bool bHasMeshLoaded = HasLoadedSkeletalMesh();
		ImGui::BeginDisabled(!bHasMeshLoaded);
		bool bOverlayToggle = bShowSkeletonOverlay;
		if (ImGui::Checkbox("Skeleton Overlay", &bOverlayToggle))
		{
			ToggleSkeletonOverlay(bOverlayToggle);
		}
		ImGui::EndDisabled();

		if (!bHasMeshLoaded)
		{
			ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Load a skeletal mesh to enable overlays.");
		}

		ImGui::Separator();

		if (bShowSkeletonOverlay)
		{
			UpdateSkeletonOverlayIfNeeded();
		}

		// 디버그 정보 표시
		// ImGui::Text("Debug Info:");
		// ImGui::Text("- PreviewSRV: %s", PreviewSRV ? "OK" : "NULL");
		// ImGui::Text("- PreviewTexture: %s", PreviewTexture ? "OK" : "NULL");

		// +-+-+ Dynamic Resizing +-+-+
		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		float newWidth = viewportPanelSize.x;
		float newHeight = viewportPanelSize.y;

		// 크기가 변경된 경우, 렌더 타겟 재생성
		if (newWidth > 0 && newHeight > 0 && (PreviewTextureWidth != static_cast<uint32>(newWidth) || PreviewTextureHeight != static_cast<uint32>(newHeight)))
		{
			ReleasePreviewRenderTarget();
			CreatePreviewRenderTarget(static_cast<uint32>(newWidth), static_cast<uint32>(newHeight));
			Viewport.Resize(0, 0, static_cast<uint32>(newWidth), static_cast<uint32>(newHeight));
		}

		FViewportClient* ViewportClient = Viewport.GetViewportClient();
		// ImGui::Text("- ViewportClient: %s", ViewportClient ? "OK" : "NULL");

		ID3D11Texture2D* RenderedTexture = nullptr;
		RenderedTexture = ViewportClient->DrawToTexture(&Viewport);

		D3D11RHI* RHI = URenderManager::GetInstance().GetRenderer()->GetRHIDevice();

		// 조건 체크 상세
		bool bAllValid = PreviewSRV && RenderedTexture && PreviewTexture && RHI;

		if (bAllValid)
		{
			RHI->CopyTexture(PreviewTexture, RenderedTexture);

			// ImGui에 PreviewSRV 표시 (동적 크기)
			ImGui::Image((void*)PreviewSRV, ImVec2(newWidth, newHeight));

			// +-+-+ Gizmo Interaction Logic +-+-+
			if (SelectedBone)
			{
				AGizmoActor* Gizmo = WorldForPreviewManager.GetGizmo();
				bool bIsViewportHovered = ImGui::IsItemHovered();

				// 1. Hovering (Picking)
				if (bIsViewportHovered && !bIsGizmoDragging)
				{
					ImVec2 Min = ImGui::GetItemRectMin();
					ImVec2 Max = ImGui::GetItemRectSize();
					ImVec2 MouseAbs = ImGui::GetMousePos();
					FVector2D ViewportMousePos(MouseAbs.x - Min.x, MouseAbs.y - Min.y);
					FVector2D ViewportSize(Max.x, Max.y);

					HoveredGizmoAxis = CPickingSystem::IsHoveringGizmoForViewport(
						Gizmo, PreviewCamera, ViewportMousePos, ViewportSize,
						FVector2D(0, 0), &Viewport, DragImpactPoint
					);

					UE_LOG("Hovered Axis: %d", HoveredGizmoAxis);
					UpdateGizmoVisibility();
				}

				// 2. Drag Start
				if (bIsViewportHovered && HoveredGizmoAxis > 0 && ImGui::IsMouseClicked(0))
				{
					bIsGizmoDragging = true;
					DraggingGizmoAxis = HoveredGizmoAxis;
					DragStartMousePosition = FVector2D(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
					DragStartBoneTransfrom = SelectedBone->GetWorldTransform();

					if (CurrentGizmoMode == EGizmoMode::Rotate)
					{
						// AGizmoActor::ProcessGizmoDragging logic
						FVector WorldAxis(0.f), LocalAxisVector(0.f);
						switch (DraggingGizmoAxis) {
						case 1: LocalAxisVector = FVector(1, 0, 0); break;
						case 2: LocalAxisVector = FVector(0, 1, 0); break;
						case 3: LocalAxisVector = FVector(0, 0, 1); break;
						}

						if (CurrentGizmoSpace == EGizmoSpace::World)
						{
							WorldAxis = LocalAxisVector;
						}
						else if (CurrentGizmoSpace == EGizmoSpace::Local)
						{
							WorldAxis = DragStartBoneTransfrom.Rotation.RotateVector(LocalAxisVector);
						}

						FVector ClickVector = DragImpactPoint - DragStartBoneTransfrom.Translation;
						FVector Tangent3D = FVector::Cross(WorldAxis, ClickVector);
						float RightDot = FVector::Dot(Tangent3D, PreviewCamera->GetRight());
						float UpDot = FVector::Dot(Tangent3D, PreviewCamera->GetUp());
						DragScreenVector = FVector2D(RightDot, -UpDot);
						DragScreenVector = DragScreenVector.GetNormalized();
					}
				}
			}
			else
			{
				HoveredGizmoAxis = 0;
				UpdateGizmoVisibility();
			}

			// 3. Drag Continue & End
			if (bIsGizmoDragging)
			{
				if (ImGui::IsMouseDown(0))
				{
					ASkeletalMeshActor* SkelActor = GetPreviewActor();
					AGizmoActor* Gizmo = WorldForPreviewManager.GetGizmo();
					if (!SkelActor || !Gizmo)	return;

					FVector2D CurrentMousePosition(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
					FVector2D MouseOffset = CurrentMousePosition - DragStartMousePosition;

					FTransform NewWorldTransform = Gizmo->CalculateDragTransform(
						DragStartBoneTransfrom, MouseOffset, PreviewCamera, &Viewport,
					    CurrentGizmoMode, CurrentGizmoSpace, DraggingGizmoAxis, DragScreenVector
					);

					// Get the parent bone's world transform
					FTransform ParentWorldTransform;
					if (UBone* ParentBone = SelectedBone->GetParent())
					{
						ParentWorldTransform = ParentBone->GetWorldTransform();
					}
					else  // Root bone
					{
						if (USkeletalMeshComponent* MeshComp = SkelActor->GetSkeletalMeshComponent())
						{
							ParentWorldTransform = MeshComp->GetWorldTransform();
						}
					}

					FTransform NewRelativeTransform = ParentWorldTransform.GetRelativeTransform(NewWorldTransform);
					SelectedBone->SetRelativeTransform(NewRelativeTransform);

					// Update gizmo transform immediately for responsiveness
					UpdateGizmoTransform();

					// Throttle expensive operations: only update every N frames for real-time feedback
					if (++OverlayUpdateFrameCounter >= OVERLAY_UPDATE_INTERVAL)
					{
						MarkSkeletonOverlayDirty();
						SkelActor->GetSkeletalMeshComponent()->GetSkeletalMesh()->MarkAsDirty();
						OverlayUpdateFrameCounter = 0;
					}
				}

				if (ImGui::IsMouseReleased(0))
				{
					bIsGizmoDragging = false;
					DraggingGizmoAxis = 0;
					UpdateGizmoVisibility();

					// Final update when drag ends to ensure everything is in sync
					MarkSkeletonOverlayDirty();
					OverlayUpdateFrameCounter = 0;  // Reset counter for next drag
					ASkeletalMeshActor* SkelActor = GetPreviewActor();
					if (SkelActor)
					{
						SkelActor->GetSkeletalMeshComponent()->GetSkeletalMesh()->MarkAsDirty();
					}
				}
			}

			// 1) 드래그 시작 조건: ImGui::Image 위에서 우클릭을 시작했을 때
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))		// 1: MouseRight (Only process RT)
			{
				bIsDraggingViewport = true;
			}

			// 2) 드래그 종료 조건: 마우스 우클릭 버튼을 (어디서든) 뗐을 때
			if (ImGui::IsMouseReleased(1))
			{
				bIsDraggingViewport = false;
			}

			// 3) 드래그 진행: bIsDraggingViewport가 true일 때만 회전/이동 처리
			if (bIsDraggingViewport)
			{
				// Camera rotation
				ImVec2 MouseDelta = ImGui::GetIO().MouseDelta;
				if (MouseDelta.x != 0.0f || MouseDelta.y != 0.0f)
				{
					if (PreviewCamera)
					{
						PreviewCamera->ApplyRotationInput(FVector2D(MouseDelta.x, MouseDelta.y));
					}
				}

				// Camera movement
				FVector MoveDirection = FVector::Zero();
				if (ImGui::IsKeyDown(ImGuiKey_W)) MoveDirection.X += 1.0f;
				if (ImGui::IsKeyDown(ImGuiKey_S)) MoveDirection.X -= 1.0f;
				if (ImGui::IsKeyDown(ImGuiKey_D)) MoveDirection.Y += 1.0f;
				if (ImGui::IsKeyDown(ImGuiKey_A)) MoveDirection.Y -= 1.0f;
				if (ImGui::IsKeyDown(ImGuiKey_E)) MoveDirection.Z += 1.0f;
				if (ImGui::IsKeyDown(ImGuiKey_Q)) MoveDirection.Z -= 1.0f;
				if (!MoveDirection.IsZero())
				{
					if (PreviewCamera)
					{
						float DeltaSeconds = GWorld->GetDeltaTime(EDeltaTime::Unscaled);
						PreviewCamera->ApplyMovementInput(MoveDirection, DeltaSeconds);
					}
				}
			}

			// 4) 줌 처리: 드래그 상태와 상관없이, ImGui::Image 호버 시에만 처리
			if (ImGui::IsItemHovered())
			{
				// Camera zoom
				float wheel = ImGui::GetIO().MouseWheel;
				if (wheel != 0.0f)
				{
					if (PreviewCamera)
					{
						float DeltaSeconds = GWorld->GetDeltaTime(EDeltaTime::Unscaled);
						PreviewCamera->ApplyZoomInput(wheel, DeltaSeconds);
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
	const float padding = ImGui::GetStyle().WindowPadding.x;
	ImGui::BeginChild("BoneInformation", ImVec2(Width - padding * 2, Height), true);
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

		UBone* ParentBone = SelectedBone->GetParent();

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
		FQuat RelQuat = RelTransform.Rotation;
		FVector RelScale = RelTransform.Scale3D;

		// 하이브리드 방식: Euler 각 직접 수정 + Delta만 Quaternion 적용
		static FVector EditableEuler(0, 0, 0);
		static FVector PreviousEuler(0, 0, 0);
		static UBone* LastSelectedBone = nullptr;

		// Bone이 바뀌면 Euler 각 초기화
		if (LastSelectedBone != SelectedBone)
		{
			EditableEuler = RelQuat.ToEulerZYXDeg();
			PreviousEuler = EditableEuler;
			LastSelectedBone = SelectedBone;
		}

		bool bLocationChanged = false;
		bool bRotationChanged = false;
		bool bScaleChanged = false;

		ImGui::Text("Location:");
		ImGui::PushID("Location");
		bLocationChanged |= ImGui::DragFloat("X##Loc", &RelLoc.X, 0.1f);
		bLocationChanged |= ImGui::DragFloat("Y##Loc", &RelLoc.Y, 0.1f);
		bLocationChanged |= ImGui::DragFloat("Z##Loc", &RelLoc.Z, 0.1f);
		ImGui::PopID();

		ImGui::Text("Rotation:");
		ImGui::PushID("Rotation");

		bool bRotXChanged = ImGui::DragFloat("X (Roll)##Rot", &EditableEuler.X, 0.5f);
		bool bRotYChanged = ImGui::DragFloat("Y (Pitch)##Rot", &EditableEuler.Y, 0.5f);
		bool bRotZChanged = ImGui::DragFloat("Z (Yaw)##Rot", &EditableEuler.Z, 0.5f);

		// Euler 각이 변경되면 Delta만큼 Quaternion 증분 회전
		if (bRotXChanged || bRotYChanged || bRotZChanged)
		{
			// 변화량(Delta) 계산
			FVector DeltaEuler = EditableEuler - PreviousEuler;

			// Delta를 Quaternion 증분 회전으로 변환
			FQuat DeltaQuat = FQuat::Identity();

			if (std::fabs(DeltaEuler.X) > 0.01f)
			{
				FQuat RotX = FQuat::FromAxisAngle(FVector(1, 0, 0), DegreesToRadians(DeltaEuler.X));
				DeltaQuat = DeltaQuat * RotX;
			}
			if (std::fabs(DeltaEuler.Y) > 0.01f)
			{
				FQuat RotY = FQuat::FromAxisAngle(FVector(0, 1, 0), DegreesToRadians(DeltaEuler.Y));
				DeltaQuat = DeltaQuat * RotY;
			}
			if (std::fabs(DeltaEuler.Z) > 0.01f)
			{
				FQuat RotZ = FQuat::FromAxisAngle(FVector(0, 0, 1), DegreesToRadians(DeltaEuler.Z));
				DeltaQuat = DeltaQuat * RotZ;
			}

			// 현재 Quaternion에 증분 적용 (Local Space)
			RelQuat = RelQuat * DeltaQuat;
			RelQuat.Normalize();

			// EditableEuler는 사용자 입력값을 유지 (Quaternion에서 재추출하지 않음)
			// 이렇게 하면 UI 표시가 사용자 입력과 일치하게 유지됨
			PreviousEuler = EditableEuler;

			bRotationChanged = true;
		}

		// Reset 버튼
		if (ImGui::Button("Reset Rotation"))
		{
			RelQuat = FQuat::Identity();
			EditableEuler = FVector(0, 0, 0);
			PreviousEuler = FVector(0, 0, 0);
			bRotationChanged = true;
		}

		// 현재 Quaternion 정보 표시
		ImGui::TextDisabled("Quat: (%.3f, %.3f, %.3f, %.3f)",
			RelQuat.X, RelQuat.Y, RelQuat.Z, RelQuat.W);

		ImGui::PopID();

		ImGui::Text("Scale:");
		ImGui::PushID("Scale");
		bScaleChanged |= ImGui::DragFloat("X##Scl", &RelScale.X, 0.01f);
		bScaleChanged |= ImGui::DragFloat("Y##Scl", &RelScale.Y, 0.01f);
		bScaleChanged |= ImGui::DragFloat("Z##Scl", &RelScale.Z, 0.01f);
		ImGui::PopID();

		ASkeletalMeshActor* SkelActor = WorldForPreviewManager.GetSkelMeshActor();

		// 값이 변경되면 적용
		if (bLocationChanged || bRotationChanged || bScaleChanged)
		{
			FTransform NewTransform(RelLoc, RelQuat, RelScale);
			SelectedBone->SetRelativeTransform(NewTransform);

			// Check if any ImGui widget is currently being actively dragged
			bool bIsAnyWidgetActive = ImGui::IsAnyItemActive();

			if (bIsAnyWidgetActive)
			{
				// Throttle expensive operations during slider drag for real-time feedback
				if (++OverlayUpdateFrameCounter >= OVERLAY_UPDATE_INTERVAL)
				{
					SkelActor->GetSkeletalMeshComponent()->GetSkeletalMesh()->MarkAsDirty();
					MarkSkeletonOverlayDirty();
					OverlayUpdateFrameCounter = 0;
				}
			}
			else
			{
				// Final update when drag ends
				SkelActor->GetSkeletalMeshComponent()->GetSkeletalMesh()->MarkAsDirty();
				MarkSkeletonOverlayDirty();
				OverlayUpdateFrameCounter = 0;
			}
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

void USkeletalMeshViewportWidget::UpdateGizmoVisibility()
{
	AGizmoActor* Gizmo = WorldForPreviewManager.GetGizmo();
	if (!Gizmo)    return;

	Gizmo->SetMode(CurrentGizmoMode);
	bool bHasSelection = (SelectedBone != nullptr);
	Gizmo->ApplyGizmoVisualState(bHasSelection, CurrentGizmoMode, HoveredGizmoAxis);
}

void USkeletalMeshViewportWidget::UpdateGizmoTransform()
{
	AGizmoActor* Gizmo = WorldForPreviewManager.GetGizmo();
	if (!Gizmo)    return;

	if (!SelectedBone)	return;
	const FTransform BoneWorldTransform = SelectedBone->GetWorldTransform();
	
	Gizmo->SetActorLocation(BoneWorldTransform.Translation);
	Gizmo->SetActorRotation(BoneWorldTransform.Rotation);
}

bool USkeletalMeshViewportWidget::HasLoadedSkeletalMesh() const
{
	if (!PreviewActor)
	{
		return false;
	}

	USkeletalMeshComponent* MeshComp = PreviewActor->GetSkeletalMeshComponent();
	if (!MeshComp)
	{
		return false;
	}

	USkeletalMesh* SkeletalMesh = MeshComp->GetSkeletalMesh();
	if (!SkeletalMesh)
	{
		return false;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
	return MeshAsset && MeshAsset->Skeleton && MeshAsset->Skeleton->GetRoot();
}

void USkeletalMeshViewportWidget::ToggleSkeletonOverlay(bool bEnable)
{
	if (bShowSkeletonOverlay == bEnable)
	{
		return;
	}

	bShowSkeletonOverlay = bEnable;
	if (bShowSkeletonOverlay)
	{
		MarkSkeletonOverlayDirty();
	}
	else
	{
		ClearSkeletonOverlay(false);
	}
}

void USkeletalMeshViewportWidget::UpdateSkeletonOverlayIfNeeded()
{
	if (!bShowSkeletonOverlay)
	{
		return;
	}

	ASkeletalMeshActor* SkeletalActor = GetPreviewActor();
	if (!SkeletalActor)
	{
		return;
	}

	// Ensure overlay component exists
	ULineComponent* SkeletonLineComponent = nullptr;
	if (UWorld* PreviewWorld = WorldForPreviewManager.GetWorldForPreview())
	{
		SkeletonLineComponent = SkeletalActor->EnsureSkeletonOverlay(PreviewWorld);
	}
	if (!SkeletonLineComponent)
	{
		return;
	}

	// Show existing lines if not dirty
	if (!bSkeletonLinesDirty && SkeletonLineComponent->GetLineCount() > 0)
	{
		SkeletonLineComponent->SetLineVisible(true);
		return;
	}

	// Delegate skeleton building to the actor (선택된 본 정보 전달)
	SkeletalActor->BuildSkeletonOverlay(SelectedBone);
	bSkeletonLinesDirty = false;
}

void USkeletalMeshViewportWidget::ClearSkeletonOverlay(bool bReleaseComponent)
{
	if (ASkeletalMeshActor* SkelActor = GetPreviewActor())
	{
		SkelActor->ClearSkeletonOverlay(bReleaseComponent);
	}
	bSkeletonLinesDirty = true;
}

void USkeletalMeshViewportWidget::MarkSkeletonOverlayDirty()
{
	bSkeletonLinesDirty = true;
}
