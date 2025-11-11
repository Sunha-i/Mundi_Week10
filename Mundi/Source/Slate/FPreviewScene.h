#pragma once
#include "CameraActor.h"
#include "DirectionalLightActor.h"

class FPreviewScene
{
public:
    void CreateWorldForPreviewScene();
    void DestroyWorldForPreviewScene();

    void SetCamera(
        const FVector& CameraLocation,
        const FVector& CameraRotation
    );
    void SetActor(AActor* InActor);
    void SetDirectionalLight(const FVector& LightRotation);

    UWorld* GetWorldForPreview() const;
    void SetWorldForPreview(UWorld* InWorldForPreview);
private:
    UWorld* WorldForPreview{};
    ACameraActor* Camera{};
};
