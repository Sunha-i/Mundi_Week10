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
	if (TargetMesh)
	{
		Config.WindowTitle = "Skeletal Mesh Viewer: " + TargetMesh->GetName();
	}
	else
	{
		Config.WindowTitle = "Skeletal Mesh Viewer: (None)";
	}
	Config.UpdateWindowFlags();
	SetConfig(Config);

	USkeletalMeshViewportWidget* ViewportWidget = NewObject<USkeletalMeshViewportWidget>();
	ViewportWidget->TargetMesh = TargetMesh;
	AddWidget(ViewportWidget);
}
