#pragma once
#include "Windows/UIWindow.h"

class USkeletalMesh;
class USkeletalMeshViewerWindow : public UUIWindow
{
public:
	DECLARE_CLASS(USkeletalMeshViewerWindow, UUIWindow)

	USkeletalMeshViewerWindow();

	void Initialize() override;
	void Cleanup() override;

public:
	FUIWindowConfig InitConfig;
	FName TargetMeshName{};
};