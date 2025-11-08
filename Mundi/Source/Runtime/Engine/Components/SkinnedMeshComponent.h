#pragma once
#include "MeshComponent.h"

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent();

protected:
	~USkinnedMeshComponent() override;
};
