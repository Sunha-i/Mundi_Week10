#pragma once

#include <fbxsdk.h>

/*
 * Fbx Import Debug Log Helper
 */

namespace FbxDebugLog
{
    /* 탭 문자("\t") 카운터 */
    inline int NumTabs = 0;
    /* 필요한 수의 탭을 반환한다 */
    FString GetTabs();
    /*
     * 속성 타입에 기반한 문자열 표현을 반환한다.
     */
    FbxString GetAttributeTypeName(FbxNodeAttribute::EType type);
    /*
     * 단일 노드를 출력한다.
     */
    void PrintNode(FbxNode* Node);

    /*
     * 노드, 그 속성 및 모든 자식을 재귀적으로 출력한다.
     */
    void PrintAllChildNodes(FbxNode* Node);
    /*
     * 속성을 출력한다
     */
    void PrintAttribute(FbxNodeAttribute* Attribute);
}