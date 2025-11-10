#pragma once

class UBone;

struct FFlesh : public FGroupInfo
{
    void Clear();
    void SetData(const TArray<UBone*>& InBones, const TArray<float>& InWeights);
    void SetBones(const TArray<UBone*>& InBones);
    void SetWeights(const TArray<float>& InWeights);
    
    TArray<UBone*> Bones;
    TArray<float> Weights;
    float WeightsTotal = 1.f;
};