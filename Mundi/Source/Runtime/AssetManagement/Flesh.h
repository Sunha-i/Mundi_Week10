#pragma once

// FFlesh는 이제 섹션 정보만 저장 (FGroupInfo 그대로)
// Skinning 데이터는 정점별로 FSkinnedVertex에 저장됨
struct FFlesh : public FGroupInfo
{
    // 더 이상 섹션별 본 정보를 저장하지 않음
    // CPU Skinning은 정점별로 처리됨
};