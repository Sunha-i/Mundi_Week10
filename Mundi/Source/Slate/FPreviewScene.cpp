#include "pch.h"
#include "FPreviewScene.h"

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

void FPreviewScene::SetCamera(
        const FVector& CameraLocation,
        const FVector& CameraRotation
    )
{
    if (!WorldForPreview)
    {
        UE_LOG("[FPreviewScene::SetCamera] Warning : WorldForPreview is null.");
        return;
    }

    if (!Camera)
        Camera = NewObject<ACameraActor>();

    Camera->SetActorLocation(CameraLocation);
    Camera->SetRotationFromEulerAngles(CameraRotation);

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

void FPreviewScene::SetDirectionalLight(const FVector& LightRotation)
{
    if (!WorldForPreview)
        UE_LOG("[FPreviewScene::SetDirectionalLight] Warning : WorldForPreview is null.");

    ADirectionalLightActor* LightActor =
        NewObject<ADirectionalLightActor>();
    LightActor->SetActorRotation(LightRotation);
    SetActor(LightActor);
}

UWorld* FPreviewScene::GetWorldForPreview() const
{
    return WorldForPreview;
}

void FPreviewScene::SetWorldForPreview(UWorld* InWorldForPreview)
{
    WorldForPreview = InWorldForPreview;
}