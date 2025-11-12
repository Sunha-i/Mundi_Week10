#pragma once

class UBone : public UObject
{
    DECLARE_CLASS(UBone, UObject)
    GENERATED_REFLECTION_BODY()
public:
    UBone() = default;
    UBone(const FName& InName, const FTransform& InitialTransform);
    // Duplicate로만 복제
    UBone(const UBone* Other) = delete;
protected:
    ~UBone() override;
public:
    const FName& GetName();
    void SetName(const FName& InName);
    
    // 자신의 로컬 좌표 Getter Setter
    const FVector& GetRelativeLocation() const;
    const FQuat& GetRelativeRotation() const;
    const FVector& GetRelativeScale() const;
    const FTransform& GetRelativeTransform() const;

    void SetRelativeLocation(const FVector& InLocation);
    void SetRelativeRotation(const FQuat& InRotatation);
    void SetRelativeScale(const FVector& InScale);
    void SetRelativeTransform(const FTransform& InRelativeTransform);
    
    // 자신의 월드 좌표 Getter Setter
    FVector GetWorldLocation() const;
    FQuat GetWorldRotation() const;
    FVector GetWorldScale() const;
    FTransform GetWorldTransform() const;

    // Local BindPos Getter Setter
    const FVector& GetRelativeBindPoseLocation() const;
    const FQuat& GetRelativeBindPoseRotation() const;
    const FVector& GetRelativeBindPoseScale() const;
    const FTransform& GetRelativeBindPose() const;

    void SetRelativeBindPoseLocation(const FVector& InLocation);
    void SetRelativeBindPoseRotation(const FQuat& InRotatation);
    void SetRelativeBindPoseScale(const FVector& InScale);
    void SetRelativeBindPoseTransform(const FTransform& InBindPoseTransform);

    // World BindPos Getter
    FVector GetWorldBindPoseLocation() const;
    FQuat GetWorldBindPoseRotation() const;
    FVector GetWorldBindPoseScale() const;
    FTransform GetWorldBindPose() const;
    
    // BindPos와 현 Transform의 차이를 반환
    FTransform GetBoneOffset();
    FMatrix GetSkinningMatrix();

    void SetParent(UBone* InParent);
    UBone* GetParent() { return Parent; }

    void AddChild(UBone* InChild);
    void RemoveChild(UBone* InChild);
    const TArray<UBone*>& GetChildren() const;

    DECLARE_DUPLICATE(UBone)
    void DuplicateSubObjects() override;
    void PostDuplicate() override;
private:
    FName Name{};

    // 현재 뼈의 트랜스폼 정보를 저장
    FTransform RelativeTransform{};
    // 최초 뼈의 트랜스폼 정보를 저장
    FTransform BindPose{};
    
    UBone* Parent{};
    TArray<UBone*> Children{};
};