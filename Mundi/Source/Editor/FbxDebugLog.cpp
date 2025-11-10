#include "pch.h"
#include "FbxDebugLog.h"

/* 필요한 수의 탭을 출력한다 */
FString FbxDebugLog::GetTabs()
{
    FString Tabs;
    for (int i = 0; i < NumTabs; i++)
        Tabs += FString("\t");

    return Tabs;
}

/*
 * 속성 타입에 기반한 문자열 표현을 반환한다.
 */
FbxString FbxDebugLog::GetAttributeTypeName(FbxNodeAttribute::EType type)
{
    switch (type)
    {
        case FbxNodeAttribute::eUnknown: return "unidentified";
        case FbxNodeAttribute::eNull: return "null";
        case FbxNodeAttribute::eMarker: return "marker";
        case FbxNodeAttribute::eSkeleton: return "skeleton";
        case FbxNodeAttribute::eMesh: return "mesh";
        case FbxNodeAttribute::eNurbs: return "nurbs";
        case FbxNodeAttribute::ePatch: return "patch";
        case FbxNodeAttribute::eCamera: return "camera";
        case FbxNodeAttribute::eCameraStereo: return "stereo";
        case FbxNodeAttribute::eCameraSwitcher: return "camera switcher";
        case FbxNodeAttribute::eLight: return "light";
        case FbxNodeAttribute::eOpticalReference: return "optical reference";
        case FbxNodeAttribute::eOpticalMarker: return "marker";
        case FbxNodeAttribute::eNurbsCurve: return "nurbs curve";
        case FbxNodeAttribute::eTrimNurbsSurface: return "trim nurbs surface";
        case FbxNodeAttribute::eBoundary: return "boundary";
        case FbxNodeAttribute::eNurbsSurface: return "nurbs surface";
        case FbxNodeAttribute::eShape: return "shape";
        case FbxNodeAttribute::eLODGroup: return "lodgroup";
        case FbxNodeAttribute::eSubDiv: return "subdiv";
        default: return "unknown";
    }
}

void FbxDebugLog::PrintNode(FbxNode* Node)
{
    // Root 여부 판별
    if (Node->GetParent() == nullptr)
    {
        UE_LOG("<root />");
        return;
    }
    
    const char* NodeName = Node->GetName();
    FbxDouble3 Translation = Node->LclTranslation.Get();
    FbxDouble3 Rotation = Node->LclRotation.Get();
    FbxDouble3 Scaling = Node->LclScaling.Get();

    // 노드의 내용을 출력한다.
    FString NodeTransformInfo = GetTabs() + "<node name='%s' translation='(%f, %f, %f)' rotation='(%f, %f, %f)' scaling='(%f, %f, %f)'>\n";
    UE_LOG(
        NodeTransformInfo.c_str(),
        NodeName,
        Translation[0],
        Translation[1],
        Translation[2],
        Rotation[0],
        Rotation[1],
        Rotation[2],
        Scaling[0],
        Scaling[1],
        Scaling[2]
    );
    NumTabs++;

    // 노드의 속성을 출력한다.
    for (int i = 0; i < Node->GetNodeAttributeCount(); i++)
        PrintAttribute(Node->GetNodeAttributeByIndex(i));
}

void FbxDebugLog::PrintAllChildNodes(FbxNode* Node)
{
    PrintNode(Node);

    // 자식을 재귀적으로 출력한다.
    for (int j = 0; j < Node->GetChildCount(); j++)
        PrintAllChildNodes(Node->GetChild(j));

    NumTabs--;
    UE_LOG((GetTabs() + "</Node>\n").c_str());
}

/*
 * 속성을 출력한다
 */
void FbxDebugLog::PrintAttribute(FbxNodeAttribute* Attribute)
{
    if (Attribute) return;

    FbxString TypeName = GetAttributeTypeName(Attribute->GetAttributeType());
    FbxString AttrName = Attribute->GetName();

    // 참고 : FbxString의 문자 배열을 검색하려면 Buffer() 메서드를 사용.
    UE_LOG(
        (GetTabs() + "<attribute type='%s' name='%s'/>\n").c_str(),
        TypeName.Buffer(),
        AttrName.Buffer()
    );
}