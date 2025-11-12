#include "pch.h"
#include "SkeletalMeshViewerWindow.h"
#include "Widgets/SkeletalMeshViewportWidget.h"

IMPLEMENT_CLASS(USkeletalMeshViewerWindow)

USkeletalMeshViewerWindow::USkeletalMeshViewerWindow()
{
}

void USkeletalMeshViewerWindow::Initialize()
{
	Super::Initialize();

	FUIWindowConfig Config = InitConfig;
	if (!TargetMeshName.Empty())
	{
		// ImGui는 ##뒤의 ID로 윈도우를 구분하므로, 고유한 ID 추가
		Config.WindowTitle = "Skeletal Mesh Viewer: " + TargetMeshName.ToString() + "##" + std::to_string(GetWindowID());
	}
	else
	{
		Config.WindowTitle = "Skeletal Mesh Viewer: (None)##" + std::to_string(GetWindowID());
	}
	Config.UpdateWindowFlags();
	SetConfig(Config);

	USkeletalMeshViewportWidget* ViewportWidget = NewObject<USkeletalMeshViewportWidget>();
	ViewportWidget->SetSkeletalMeshToViewport(TargetMeshName);
	AddWidget(ViewportWidget);
}

void USkeletalMeshViewerWindow::Cleanup()
{
	// X 버튼으로 창을 닫지 않을 때의 크래시를 방지하기 위해
	// UUIWindow의 소멸자에서 수행될 로직을 여기서 미리 실행
	// Cleanup은 전역 소멸이 시작되기 전, UIManager에 의해 안전한 시점에 호출될 것
	for (UWidget* Widget : GetWidgets())
	{
		if (Widget)
		{
			// 자식 위젯(USkeletalMeshViewportWidget)을 명시적으로 파괴하여
			// 프리뷰 월드와 그 액터들이 안전하게 정리되도록 함
			ObjectFactory::DeleteObject(Widget);
		}
	}

	// 참고) 이 함수는 Widgets 배열을 직접 비울 수 없음
	// 나중에 UUIWindow의 소멸자가 이미 파괴된 위젯에 대해 DeleteObject를 다시 호출하게 되지만,
	// ObjectFactory가 중복 삭제를 처리해줄 것으로 기대..
	Super::Cleanup();
}
