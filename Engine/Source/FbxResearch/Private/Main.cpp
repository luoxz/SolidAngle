#include "Core.h"
#include "Modules\ModuleManager.h"
#include <fbxsdk.h>
#include <iostream>
#include "FbxCommon.h"
#include "fbxsdk\scene\geometry\fbxlayer.h"
class ResearchFbx : public FDefaultModuleImpl
{
public:
	virtual bool SupportsDynamicReloading() override
	{
		// Core cannot be unloaded or reloaded
		return false;
	}
};
IMPLEMENT_MODULE(ResearchFbx, TestModel)

FbxManager*   gSdkManager = nullptr;
FbxScene*     gScene = nullptr;

#ifdef IOS_REF
#undef  IOS_REF
#define IOS_REF (*(pSdkManager->GetIOSettings()))
#endif
DECLARE_LOG_CATEGORY_EXTERN(Main, Log, All);
DEFINE_LOG_CATEGORY(Main);


FbxFileTexture*  gTexture = NULL;
FbxSurfacePhong* gMaterial = NULL;
void CreateTexture(FbxScene* pScene)
{
	gTexture = FbxFileTexture::Create(pScene, "Diffuse Texture");

	// Resource file must be in the application's directory.
	FbxString lTexPath = "\\Crate.jpg";

	// Set texture properties.
	gTexture->SetFileName(lTexPath.Buffer());
	gTexture->SetTextureUse(FbxTexture::eStandard);
	gTexture->SetMappingType(FbxTexture::eUV);
	gTexture->SetMaterialUse(FbxFileTexture::eModelMaterial);
	gTexture->SetSwapUV(false);
	gTexture->SetTranslation(0.0, 0.0);
	gTexture->SetScale(1.0, 1.0);
	gTexture->SetRotation(0.0, 0.0);
}

void CreateMaterial(FbxScene* pScene)
{
	FbxString lMaterialName = "material";
	FbxString lShadingName = "Phong";
	FbxDouble3 lBlack(0.0, 0.0, 0.0);
	FbxDouble3 lRed(1.0, 0.0, 0.0);
	FbxDouble3 lDiffuseColor(0.75, 0.75, 0.0);
	gMaterial = FbxSurfacePhong::Create(pScene, lMaterialName.Buffer());

	// Generate primary and secondary colors.
	gMaterial->Emissive.Set(lBlack);
	gMaterial->Ambient.Set(lRed);
	gMaterial->Diffuse.Set(lDiffuseColor);
	gMaterial->TransparencyFactor.Set(40.5);
	gMaterial->ShadingModel.Set(lShadingName);
	gMaterial->Shininess.Set(0.5);

	// the texture need to be connected to the material on the corresponding property
	if (gTexture)
		gMaterial->Diffuse.ConnectSrcObject(gTexture);
}


void AddMaterials(FbxMesh* pMesh)
{
	// Set material mapping.
	FbxGeometryElementMaterial* lMaterialElement = pMesh->CreateElementMaterial();
	lMaterialElement->SetMappingMode(FbxGeometryElement::eByPolygon);
	lMaterialElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);

	//get the node of mesh, add material for it.
	FbxNode* lNode = pMesh->GetNode();
	if (lNode == NULL)
		return;
	lNode->AddMaterial(gMaterial);

	// We are in eByPolygon, so there's only need for 6 index (a cube has 6 polygons).
	lMaterialElement->GetIndexArray().SetCount(6);

	// Set the Index 0 to 6 to the material in position 0 of the direct array.
	for (int i = 0; i < 6; ++i)
		lMaterialElement->GetIndexArray().SetAt(i, 0);
}


FbxNode* CreateCubeMesh(FbxScene* pScene, char* pName)
{
	int i, j;
	FbxMesh* lMesh = FbxMesh::Create(pScene, pName);

	FbxVector4 lControlPoint0(-50, 0, 50);
	FbxVector4 lControlPoint1(50, 0, 50);
	FbxVector4 lControlPoint2(50, 100, 50);
	FbxVector4 lControlPoint3(-50, 100, 50);
	FbxVector4 lControlPoint4(-50, 0, -50);
	FbxVector4 lControlPoint5(50, 0, -50);
	FbxVector4 lControlPoint6(50, 100, -50);
	FbxVector4 lControlPoint7(-50, 100, -50);

	FbxVector4 lNormalXPos(1, 0, 0);
	FbxVector4 lNormalXNeg(-1, 0, 0);
	FbxVector4 lNormalYPos(0, 1, 0);
	FbxVector4 lNormalYNeg(0, -1, 0);
	FbxVector4 lNormalZPos(0, 0, 1);
	FbxVector4 lNormalZNeg(0, 0, -1);

	// Create control points.
	lMesh->InitControlPoints(24);
	FbxVector4* lControlPoints = lMesh->GetControlPoints();

	lControlPoints[0] = lControlPoint0;
	lControlPoints[1] = lControlPoint1;
	lControlPoints[2] = lControlPoint2;
	lControlPoints[3] = lControlPoint3;
	lControlPoints[4] = lControlPoint1;
	lControlPoints[5] = lControlPoint5;
	lControlPoints[6] = lControlPoint6;
	lControlPoints[7] = lControlPoint2;
	lControlPoints[8] = lControlPoint5;
	lControlPoints[9] = lControlPoint4;
	lControlPoints[10] = lControlPoint7;
	lControlPoints[11] = lControlPoint6;
	lControlPoints[12] = lControlPoint4;
	lControlPoints[13] = lControlPoint0;
	lControlPoints[14] = lControlPoint3;
	lControlPoints[15] = lControlPoint7;
	lControlPoints[16] = lControlPoint3;
	lControlPoints[17] = lControlPoint2;
	lControlPoints[18] = lControlPoint6;
	lControlPoints[19] = lControlPoint7;
	lControlPoints[20] = lControlPoint1;
	lControlPoints[21] = lControlPoint0;
	lControlPoints[22] = lControlPoint4;
	lControlPoints[23] = lControlPoint5;

	// We want to have one normal for each vertex (or control point),
	// so we set the mapping mode to eByControlPoint.
	FbxGeometryElementNormal* lGeometryElementNormal = lMesh->CreateElementNormal();

	lGeometryElementNormal->SetMappingMode(FbxGeometryElement::eByControlPoint);

	// Set the normal values for every control point.
	lGeometryElementNormal->SetReferenceMode(FbxGeometryElement::eDirect);

	lGeometryElementNormal->GetDirectArray().Add(lNormalZPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalZPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalZPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalZPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalZNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalZNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalZNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalZNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalXNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYPos);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYNeg);
	lGeometryElementNormal->GetDirectArray().Add(lNormalYNeg);


	// Array of polygon vertices.
	int lPolygonVertices[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,12, 13,
		14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };


	// Create UV for Diffuse channel.
	FbxGeometryElementUV* lUVDiffuseElement = lMesh->CreateElementUV("DiffuseUV");
	FBX_ASSERT(lUVDiffuseElement != NULL);
	lUVDiffuseElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
	lUVDiffuseElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);

	FbxVector2 lVectors0(0, 0);
	FbxVector2 lVectors1(1, 0);
	FbxVector2 lVectors2(1, 1);
	FbxVector2 lVectors3(0, 1);

	lUVDiffuseElement->GetDirectArray().Add(lVectors0);
	lUVDiffuseElement->GetDirectArray().Add(lVectors1);
	lUVDiffuseElement->GetDirectArray().Add(lVectors2);
	lUVDiffuseElement->GetDirectArray().Add(lVectors3);

	//Now we have set the UVs as eIndexToDirect reference and in eByPolygonVertex  mapping mode
	//we must update the size of the index array.
	lUVDiffuseElement->GetIndexArray().SetCount(24);

	// Create polygons. Assign texture and texture UV indices.
	for (i = 0; i < 6; i++)
	{
		// all faces of the cube have the same texture
		lMesh->BeginPolygon(-1, -1, -1, false);

		for (j = 0; j < 4; j++)
		{
			// Control point index
			lMesh->AddPolygon(lPolygonVertices[i * 4 + j]);

			// update the index array of the UVs that map the texture to the face
			lUVDiffuseElement->GetIndexArray().SetAt(i * 4 + j, j);
		}

		lMesh->EndPolygon();
	}

	// create a FbxNode
	FbxNode* lNode = FbxNode::Create(pScene, pName);

	// set the node attribute
	lNode->SetNodeAttribute(lMesh);

	// set the shading mode to view texture
	lNode->SetShadingMode(FbxNode::eTextureShading);

	// rescale the cube
	lNode->LclScaling.Set(FbxVector4(0.3, 0.3, 0.3));

	// return the FbxNode
	return lNode;
}

int main(int argc,TCHAR* argv[])
{
	FCommandLine::Set(TEXT(""));
	GLogConsole = FPlatformOutputDevices::GetLogConsole();
	GLogConsole->Show(true);
	FPlatformOutputDevices::SetupOutputDevices();
	//GLog->AddOutputDevice(GLogConsole);
	FbxManager* pManger = nullptr;
	FbxScene* pScene = nullptr;
	if (!InitializeSdkObjects(pManger, pScene))
	{
		UE_LOG(Main, Error, TEXT("Create FbxScene Failed!"));
		return -1;
	}
	check(pManger);
	check(pScene);
	GLog->TearDown();
}