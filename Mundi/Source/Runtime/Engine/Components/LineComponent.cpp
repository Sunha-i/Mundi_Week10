#include "pch.h"
#include "LineComponent.h"
#include "Renderer.h"

IMPLEMENT_CLASS(ULineComponent)

void ULineComponent::GetWorldLineData(TArray<FVector>& OutStartPoints, TArray<FVector>& OutEndPoints, TArray<FVector4>& OutColors) const
{
    if (!bLinesVisible || Lines.empty())
    {
        OutStartPoints.clear();
        OutEndPoints.clear();
        OutColors.clear();
        return;
    }

    // Use cached data if available and valid
    if (!bWorldDataDirty && !CachedStartPoints.empty())
    {
        OutStartPoints = CachedStartPoints;
        OutEndPoints = CachedEndPoints;
        OutColors = CachedColors;
        return;
    }

    // Recalculate world coordinates
    CachedStartPoints.clear();
    CachedEndPoints.clear();
    CachedColors.clear();

    FMatrix worldMatrix = GetWorldMatrix();
    size_t lineCount = Lines.size();

    CachedStartPoints.reserve(lineCount);
    CachedEndPoints.reserve(lineCount);
    CachedColors.reserve(lineCount);

    for (const ULine* Line : Lines)
    {
        if (Line)
        {
            FVector worldStart, worldEnd;
            Line->GetWorldPoints(worldMatrix, worldStart, worldEnd);

            CachedStartPoints.push_back(worldStart);
            CachedEndPoints.push_back(worldEnd);
            CachedColors.push_back(Line->GetColor());
        }
    }

    bWorldDataDirty = false;

    OutStartPoints = CachedStartPoints;
    OutEndPoints = CachedEndPoints;
    OutColors = CachedColors;
}

void ULineComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    bLinesVisible = true;

    const uint64 NumLines = Lines.size();
    for (uint64 idx = 0; idx < NumLines; ++idx)
    {
        if (Lines[idx])
        {
            Lines[idx] = Lines[idx]->Duplicate();
        }
    }
}

ULineComponent::ULineComponent()
{
    bLinesVisible = true;
}

ULineComponent::~ULineComponent()
{
    ClearLines();
}

ULine* ULineComponent::AddLine(const FVector& StartPoint, const FVector& EndPoint, const FVector4& Color)
{
    ULine* NewLine = NewObject<ULine>();
    NewLine->SetLine(StartPoint, EndPoint);
    NewLine->SetColor(Color);

    Lines.push_back(NewLine);
    bWorldDataDirty = true;  // Cache invalidation

    return NewLine;
}

void ULineComponent::RemoveLine(ULine* Line)
{
    if (!Line) return;

    auto it = std::find(Lines.begin(), Lines.end(), Line);
    if (it != Lines.end())
    {
        DeleteObject(*it);
        Lines.erase(it);
        bWorldDataDirty = true;  // Cache invalidation
    }
}

void ULineComponent::ClearLines()
{
    for (ULine* Line : Lines)
    {
        if (Line)
        {
            DeleteObject(Line);
        }
    }
    Lines.Empty();
    bWorldDataDirty = true;  // Cache invalidation
}

void ULineComponent::OnTransformUpdated()
{
    Super::OnTransformUpdated();
    bWorldDataDirty = true;  // Invalidate cache when transform changes
}

void ULineComponent::CollectLineBatches(URenderer* Renderer)
{
    if (!HasVisibleLines() || !Renderer)
        return;

    TArray<FVector> startPoints, endPoints;
    TArray<FVector4> colors;

    // Extract world coordinate line data efficiently (uses cache)
    GetWorldLineData(startPoints, endPoints, colors);

    // Add all lines to renderer batch at once
    if (!startPoints.empty())
    {
        Renderer->AddLines(startPoints, endPoints, colors);
    }
    // No need to clear/shrink - local arrays will be destroyed automatically
}

