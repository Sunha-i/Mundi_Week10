#pragma once
#include "CameraActor.h"
#include "DirectionalLightActor.h"

class FPreviewScene
{
public:
    void CreateWorldForPreviewScene();
    void DestroyWorldForPreviewScene();

    void SetCamera(ACameraActor* InCamera);
    void SetActor(AActor* InActor);
    void SetDirectionalLight(const FVector& LightRotation, const FVector& LightLocation = FVector(0.f, 0.f, 0.f));
    
    UWorld* GetWorldForPreview() const;
    void SetWorldForPreview(UWorld* InWorldForPreview);

    
private:
    UWorld* WorldForPreview{};
    ACameraActor* Camera{};
};
