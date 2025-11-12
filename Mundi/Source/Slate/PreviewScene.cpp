#include "pch.h"
#include "PreviewScene.h"

FPreviewScene::~FPreviewScene()
{
    OutputDebugStringA("[DESTRUCTOR] FPreviewScene START\n");
    DestroyWorldForPreviewScene();
    OutputDebugStringA("[DESTRUCTOR] FPreviewScene END\n");
}

void FPreviewScene::CreateWorldForPreviewScene()
{
    DestroyWorldForPreviewScene();
    WorldForPreview = NewObject<UWorld>();

    if (WorldForPreview)
    {
        // Preview World 플래그 설정 (에디터 전용 빌보드 생성 방지)
        WorldForPreview->bIsPreviewWorld = true;

        // Preview Scene에서는 빌보드 렌더링도 비활성화
        //WorldForPreview->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Billboard);
    }
}

void FPreviewScene::DestroyWorldForPreviewScene()
{
    if (WorldForPreview)
    {
        // 참조 대신 복사본을 만들어서 순회 중 배열 수정 문제 방지
        TArray<AActor*> EditorActorsCopy = WorldForPreview->GetEditorActors();

        // Actor를 명시적으로 삭제 (World 소멸자가 손상된 Actor를 삭제하지 않도록)
        for (int32 i = 0; i < EditorActorsCopy.size(); i++)
        {
            if (EditorActorsCopy[i])
            {
                WorldForPreview->RemoveEditorActor(EditorActorsCopy[i]);
            }
        }

        ObjectFactory::DeleteObject(WorldForPreview);
        WorldForPreview = nullptr;
    }
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
        WorldForPreview->AddEditorActor(Gizmo);
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