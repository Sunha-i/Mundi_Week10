#include "pch.h"
#include "PreviewScene.h"

void FPreviewScene::CreateWorldForPreviewScene()
{
    DestroyWorldForPreviewScene();
    WorldForPreview = NewObject<UWorld>();
}

void FPreviewScene::DestroyWorldForPreviewScene()
{
    if (WorldForPreview)
        ObjectFactory::DeleteObject(WorldForPreview);
}

void FPreviewScene::SetCamera(ACameraActor* InCamera)
{
    if (!WorldForPreview)
    {
        UE_LOG("[FPreviewScene::SetCamera] Warning : WorldForPreview is null.");
        return;
    }

    if (!InCamera)
    {
        UE_LOG("[FPreviewScene::SetCamera] Warning : InCamera is null.");
        return;
    }

    Camera = InCamera;
    WorldForPreview->SetEditorCameraActor(Camera);
}

void FPreviewScene::SetActor(AActor* InActor)
{
    if (!InActor)
        UE_LOG("[FPreviewScene::SetActor] Warning : InActor is null.");
    if (!WorldForPreview)
        UE_LOG("[FPreviewScene::SetActor] Warning : WorldForPreview is null.");

    WorldForPreview->AddActorToLevel(InActor);
}

void FPreviewScene::SetDirectionalLight(const FVector& LightRotation, const FVector& LightLocation)
{
    if (!WorldForPreview)
        UE_LOG("[FPreviewScene::SetDirectionalLight] Warning : WorldForPreview is null.");

    if (!DirectionalLight)
    {
        DirectionalLight = NewObject<ADirectionalLightActor>();
        SetActor(DirectionalLight);
    }
    DirectionalLight->SetActorRotation(LightRotation);
    DirectionalLight->SetActorLocation(LightLocation);
}

void FPreviewScene::SetGizmo()
{
    if (!WorldForPreview) return;
    if (!Gizmo)
    {
        Gizmo = NewObject<AGizmoActor>();
        SetActor(Gizmo);
    }
}

UWorld* FPreviewScene::GetWorldForPreview() const
{
    return WorldForPreview;
}

void FPreviewScene::SetWorldForPreview(UWorld* InWorldForPreview)
{
    WorldForPreview = InWorldForPreview;
}