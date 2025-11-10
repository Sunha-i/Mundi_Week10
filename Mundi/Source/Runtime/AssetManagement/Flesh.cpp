#include "pch.h"
#include "Flesh.h"

void FFlesh::Clear()
{
    Bones.clear();
    Weights.clear();
    WeightsTotal = 1.f;
}

void FFlesh::SetData(const TArray<UBone*>& InBones, const TArray<float>& InWeights)
{
    SetBones(InBones);
    SetWeights(InWeights);
}

void FFlesh::SetBones(const TArray<UBone*>& InBones)
{
    Bones = InBones;
}

void FFlesh::SetWeights(const TArray<float>& InWeights)
{
    Weights = InWeights;
}