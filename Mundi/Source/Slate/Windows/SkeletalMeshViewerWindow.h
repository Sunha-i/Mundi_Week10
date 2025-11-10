#pragma once
#include "Windows/UIWindow.h"

class USkeletalMesh;
class USkeletalMeshViewerWindow : public UUIWindow
{
public:
	DECLARE_CLASS(USkeletalMeshViewerWindow, UUIWindow)

	USkeletalMeshViewerWindow();

	void Initialize() override;

public:
	FUIWindowConfig InitConfig;
	USkeletalMesh* TargetMesh = nullptr;
};