#pragma once
#include "Widgets/Widget.h"

class USkeletalMesh;
class USkeletalMeshViewportWidget : public UWidget
{
public:
	DECLARE_CLASS(USkeletalMeshViewportWidget, UWidget)

	USkeletalMeshViewportWidget();

	void RenderWidget() override;

public:
	USkeletalMesh* TargetMesh = nullptr;
};