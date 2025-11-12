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
    
    void SetGizmo();
    AGizmoActor* GetGizmo() { return Gizmo; }

    void SetWorldForPreview(UWorld* InWorldForPreview);
    UWorld* GetWorldForPreview() const;

private:
    UWorld* WorldForPreview{};
    ACameraActor* Camera{};
    ADirectionalLightActor* DirectionalLight{};
    AGizmoActor* Gizmo{};
};
