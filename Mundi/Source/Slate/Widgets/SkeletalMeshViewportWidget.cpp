#include "pch.h"
#include "SkeletalMeshViewportWidget.h"

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
}

void USkeletalMeshViewportWidget::RenderWidget()
{
	if (TargetMesh)
	{
		ImGui::Text("Viewing Mesh: %s", TargetMesh->GetFilePath().c_str());
		ImGui::Separator();
		ImGui::Text("Viewport will be here.");
	}
	else
	{
		ImGui::Text("No mesh specified for this viewer.");
	}
}