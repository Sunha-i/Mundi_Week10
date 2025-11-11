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

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
	WorldForPreviewManager.CreateWorldForPreviewScene();
	WorldForPreviewManager.SetDirectionalLight(
		{30.f, 0.f, 30.f}
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
		Camera->SetActorLocation({-5.f, 0.f, 3.f});
		Camera->SetRotationFromEulerAngles(FVector::Zero());
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
		UE_LOG("[SkeletalMeshViewportWidget] SkeletalMeshComponent mesh set");
	}
	else
	{
		UE_LOG("[SkeletalMeshViewportWidget] Error: SkeletalMeshComponent is null");
	}

	// World에 Actor 추가 (자동으로 RegisterAllComponents 호출됨)
	WorldForPreviewManager.SetActor(SkeletalMeshActor);

	// 디버그: Actor와 Component 상태 확인
	UE_LOG("[SkeletalMeshViewportWidget] Preview World has %d actors", PreviewWorld->GetActors().size());

	for (AActor* Actor : PreviewWorld->GetActors())
	{
		UE_LOG("[SkeletalMeshViewportWidget] Actor: %s, Components: %d",
			Actor->GetName().c_str(),
			Actor->GetOwnedComponents().size());

		for (UActorComponent* Comp : Actor->GetOwnedComponents())
		{
			UE_LOG("[SkeletalMeshViewportWidget]   - Component: %s, Registered: %s",
				Comp->GetName().c_str(),
				Comp->IsRegistered() ? "YES" : "NO");
		}
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

		// 디버그 정보 표시
		ImGui::Text("Debug Info:");
		ImGui::Text("- PreviewSRV: %s", PreviewSRV ? "OK" : "NULL");
		ImGui::Text("- PreviewTexture: %s", PreviewTexture ? "OK" : "NULL");

		FViewportClient* ViewportClient = Viewport.GetViewportClient();
		ID3D11Texture2D* RenderedTexture = nullptr;

		ImGui::Text("- ViewportClient: %s", ViewportClient ? "OK" : "NULL");

		if (ViewportClient)
		{
			// DrawToTexture는 임시 텍스처를 생성하고 반환함 (사용 후 Release 필요)
			RenderedTexture = ViewportClient->DrawToTexture(&Viewport);
			ImGui::Text("- RenderedTexture: %s", RenderedTexture ? "OK" : "NULL");
		}

		D3D11RHI* RHI = URenderManager::GetInstance().GetRenderer()->GetRHIDevice();
		ImGui::Text("- RHI: %s", RHI ? "OK" : "NULL");
		ImGui::Separator();

		// 조건 체크 상세
		bool bAllValid = PreviewSRV && RenderedTexture && PreviewTexture && RHI;
		ImGui::Text("All conditions valid: %s", bAllValid ? "YES" : "NO");

		if (bAllValid)
		{
			UE_LOG("[RenderViewportPanel] Copying and displaying texture");
			RHI->CopyTexture(PreviewTexture, RenderedTexture);

			// ImGui에 PreviewSRV 표시 (고정 크기)
			ImVec2 PreviewImageSize(
				static_cast<float>(PreviewTextureWidth),
				static_cast<float>(PreviewTextureHeight)
			);
			ImGui::Image((void*)PreviewSRV, PreviewImageSize);
			ImGui::Text("Image displayed");
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
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(Coming soon...)");
		// TODO: Selected bone information will be displayed here
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