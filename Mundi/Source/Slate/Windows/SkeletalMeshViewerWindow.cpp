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
		Config.WindowTitle = "Skeletal Mesh Viewer: " + TargetMeshName.ToString();
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
