//-
// Copyright 2015 Autodesk, Inc. All rights reserved.
// 
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
//+

///////////////////////////////////////////////////////////////////
// DESCRIPTION: This is a simple shader, that only
//				supports decal texturing using standard OpenGL commands.
//
// How to use this shader:
//
//	1. Compile it and put it in your plug-in path.
//	2. Window->Setting/Preferences->Plug-in Manager.
//	   Make sure that the hwUnlitShader.mll plug-in is there
//	   and click the "loaded" checkbox next to it to load it.
//	3. Create a new scene. 
//  4. Create a poly object (say, a polygon cube).
//	5. Create a new hwUnlitShader. You can use the hypershade, or
//     right-click on the cube and select Material->Create new Material->hwUnlitShader.
//	6. Open the attribute editor and select the hwUnlitShader you just created.
//  7. The color attribute can be mapped on a file texture, just like any
//     standard maya shader. Try it, press '6' to go in textured mode and see the texture.
//	8. The transparency attribute can be set to a value other than 0.0 to
//     display transparent color. If the color attribute is textured and
//	   transparency is set, the average of the transparency is modulated
//     by the texture.
//
// COMPATIBILITY NOTE: Before Maya 4.5, a problem prevented Maya from
// properly using MPxHwShaderNode::hasTransparency(). The work-around is
// to create transparency attributes that mimics standard Lambert shaders,
// and to set the transparency value to a color different than black.
// This forces Maya to properly order geometry from farthest to closest.
// This shader illustrates both the workaround (the transparency attributes
// illustrated in initialize() below) and the recommended way (overloading 
// hasTransparency() to return true). If you do not need to support
// earlier versions of Maya, or do not need transparency, then there is
// no need to create the transparency attributes.
//
//
///////////////////////////////////////////////////////////////////

#ifdef WIN32
#pragma warning( disable : 4786 )		// Disable stupid STL warnings.
#endif


#include <maya/MIOStream.h>
#include <math.h>

#include <maya/MString.h>
#include <maya/MPlug.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnLightDataAttribute.h>
#include <maya/MFloatVector.h>
#include <maya/MFnPlugin.h>
#include <maya/MFnStringData.h>
#include <maya/MGlobal.h>

#include <maya/MPoint.h>
#include <maya/MMatrix.h>
#include <maya/MVector.h>
#include <maya/MEulerRotation.h>
#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MSceneMessage.h>

#include <maya/MGL.h>

#include "hwUnlitShader.h"
#include "ShadingConnection.h"


// Plug-in ID and Attributes. 
// This ID needs to be unique to prevent clashes.
//
MTypeId  hwUnlitShader::id( 0x00105440 );

MObject  hwUnlitShader::color;
MObject  hwUnlitShader::colorR;
MObject  hwUnlitShader::colorG;
MObject  hwUnlitShader::colorB;

MObject  hwUnlitShader::transparency;
MObject  hwUnlitShader::transparencyR;
MObject  hwUnlitShader::transparencyG;
MObject  hwUnlitShader::transparencyB;


void hwUnlitShader::postConstructor( )
{
	setMPSafe(false);
}



void hwUnlitShader::printGlError( const char *call )
{
    GLenum error;

	while( (error = glGetError()) != GL_NO_ERROR ) {
		assert(0);
	    cerr << call << ":" << error << " is " << (const char *)gluErrorString( error ) << "\n";
	}
}

hwUnlitShader::hwUnlitShader()
{
	m_pTextureCache = MTextureCache::instance();

	attachSceneCallbacks();
}

hwUnlitShader::~hwUnlitShader()
{
	detachSceneCallbacks();
}

void hwUnlitShader::releaseEverything()
{
	// Clean the texture cache, through refcounting.
	m_pTextureCache->release();

	if(!MTextureCache::getReferenceCount())
	{
		m_pTextureCache = 0;
	}

}

void hwUnlitShader::attachSceneCallbacks()
{
	fBeforeNewCB  = MSceneMessage::addCallback(MSceneMessage::kBeforeNew,  releaseCallback, this);
	fBeforeOpenCB = MSceneMessage::addCallback(MSceneMessage::kBeforeOpen, releaseCallback, this);
	fBeforeRemoveReferenceCB = MSceneMessage::addCallback(MSceneMessage::kBeforeRemoveReference, 
														  releaseCallback, this);
	fMayaExitingCB = MSceneMessage::addCallback(MSceneMessage::kMayaExiting, releaseCallback, this);
}

/*static*/
void hwUnlitShader::releaseCallback(void* clientData)
{
	hwUnlitShader *pThis = (hwUnlitShader*) clientData;
	pThis->releaseEverything();
}

void hwUnlitShader::detachSceneCallbacks()
{
	if (fBeforeNewCB)
		MMessage::removeCallback(fBeforeNewCB);
	if (fBeforeOpenCB)
		MMessage::removeCallback(fBeforeOpenCB);
	if (fBeforeRemoveReferenceCB)
		MMessage::removeCallback(fBeforeRemoveReferenceCB);
	if (fMayaExitingCB)
		MMessage::removeCallback(fMayaExitingCB);

	fBeforeNewCB = 0;
	fBeforeOpenCB = 0;
	fBeforeRemoveReferenceCB = 0;
	fMayaExitingCB = 0;
}


void * hwUnlitShader::creator()
{
    return new hwUnlitShader();
}

MStatus initializePlugin( MObject obj )
{ 
	MStatus   status;
	
	const MString UserClassify( "shader/surface/utility" );

	MFnPlugin plugin( obj, PLUGIN_COMPANY, "4.0.1", "Any");
	status = plugin.registerNode( "hwUnlitShader", hwUnlitShader::id, 
			                      hwUnlitShader::creator, hwUnlitShader::initialize,
								  MPxNode::kHwShaderNode, &UserClassify );
	if (!status) {
		status.perror("registerNode");
		return status;
	}

	return MS::kSuccess;
}

MStatus uninitializePlugin( MObject obj )
{
	MStatus   status;
	
	MFnPlugin plugin( obj );

	plugin.deregisterNode( hwUnlitShader::id );
	if (!status) {
		status.perror("deregisterNode");
		return status;
	}

	return MS::kSuccess;
}

MStatus hwUnlitShader::initialize()
{
    MFnNumericAttribute nAttr; 
	MStatus status;
	MFnTypedAttribute sAttr; // For string attributes

    // Create COLOR input attributes
    colorR = nAttr.create( "colorR", "cr",MFnNumericData::kFloat);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(1.0f);

    colorG = nAttr.create( "colorG", "cg",MFnNumericData::kFloat);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(0.5f);

    colorB = nAttr.create( "colorB", "cb",MFnNumericData::kFloat);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(0.5f);

    color = nAttr.create( "color", "c", colorR, colorG, colorB);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(1.0f, 0.5f, 0.5f);	// ugly pink-salmon color. You can't miss it.
    nAttr.setUsedAsColor(true);

    // Create TRANSPARENCY input attributes
	transparencyR = nAttr.create( "transparencyR", "itr",MFnNumericData::kFloat);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(1.0f);

    transparencyG = nAttr.create( "transparencyG", "itg",MFnNumericData::kFloat);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(0.5f);

    transparencyB = nAttr.create( "transparencyB", "itb",MFnNumericData::kFloat);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(0.5f);

    transparency = nAttr.create( "transparency", "it", transparencyR, transparencyG, transparencyB);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setDefault(0.0001f, 0.0001f, 0.0001f); // very light gray.
    nAttr.setUsedAsColor(true);

	// Add the attributes here
    addAttribute(color);
    addAttribute(transparency);


 // create output attributes here
	// outColor is the only output attribute and it is inherited
	// so we do not need to create or add it.
	//

    return MS::kSuccess;
}


///////////////////////////////////////////////////////
// DESCRIPTION:
// This function gets called by Maya to evaluate the shader.
//
// Get color from the input block.
// Compute the color/alpha of our bump for a given UV coordinate.
// Put the result into the output plug.
///////////////////////////////////////////////////////

MStatus hwUnlitShader::compute(
const MPlug&      plug,
      MDataBlock& block ) 
{ 
    bool k = false;
    k |= (plug==outColor);
    k |= (plug==outColorR);
    k |= (plug==outColorG);
    k |= (plug==outColorB);
    if( !k ) return MS::kUnknownParameter;

	// Always return black for now.
    MFloatVector resultColor(0.0,0.0,0.0);

    // set ouput color attribute
    MDataHandle outColorHandle = block.outputValue( outColor );
    MFloatVector& outColor = outColorHandle.asFloatVector();
    outColor = resultColor;
    outColorHandle.setClean();

    return MS::kSuccess;
}

MStatus hwUnlitShader::getFloat3(MObject attr, float value[3])
{
	// Get the attr to use
	//
	MPlug	plug(thisMObject(), attr);

	MObject object;

	MStatus status = plug.getValue(object);
	if (!status)
	{
		status.perror("hwUnlitShader::bind plug.getValue.");
		return status;
	}


	MFnNumericData data(object, &status);
	if (!status)
	{
		status.perror("hwUnlitShader::bind construct data.");
		return status;
	}

	status = data.getData(value[0], value[1], value[2]);
	if (!status)
	{
		status.perror("hwUnlitShader::bind get values.");
		return status;
	}

	return MS::kSuccess;
}

MStatus hwUnlitShader::getString(MObject attr, MString &str)
{
	MPlug	plug(thisMObject(), attr);
	MStatus status = plug.getValue( str );
	return MS::kSuccess;
}


void hwUnlitShader::updateTransparencyFlags(MString objectPath)
{
	// Update the transparency flags and values.
	// Check if the transparency channel is mapped on a texture, or if
	// it is constant. Textured transparency is not supported in this example,
	// because it would involve multiplying alpha values of two texture maps...
	MString transparencyName = "";
	ShadingConnection transparencyConnection(thisMObject(), objectPath, "transparency");
	if (transparencyConnection.type() == ShadingConnection::CONSTANT_COLOR)
	{
		// transparency = average of r,g,b transparency channels.
		MColor tc = transparencyConnection.constantColor();
		fConstantTransparency = (tc.r + tc.g + tc.b) / 3.0f;
	}
	else
		fConstantTransparency = 0.0f;	// will result in alpha=1.
}


/* virtual */
MStatus	hwUnlitShader::bind(const MDrawRequest& request,
							M3dView& view)
{
	MStatus status;

	// white, opaque.
	float bgColor[4] = {1,1,1,1};

	// Get path of current object in draw request
	currentObjectPath = request.multiPath();
	MString currentPathName( currentObjectPath.partialPathName() );

	updateTransparencyFlags(currentPathName);

	// Get decal texture name
	MString decalName = "";
	ShadingConnection colorConnection(thisMObject(), currentPathName, "color");

	// If the color attribute is ultimately connected to a file texture, find its filename.
	// otherwise use the default color texture.
	if (colorConnection.type() == ShadingConnection::TEXTURE &&
		colorConnection.texture().hasFn(MFn::kFileTexture))
	{
		// Get the filename of the texture.
		MFnDependencyNode textureNode(colorConnection.texture());
		MPlug filenamePlug( colorConnection.texture(), textureNode.attribute(MString("fileTextureName")) );
		filenamePlug.getValue(decalName);
		if (decalName == "")
			getFloat3(color, bgColor);
	}
	else
	{
		decalName = "";
		getFloat3(color, bgColor);
	}
	
	assert(glGetError() == GL_NO_ERROR);

	view.beginGL();

	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

	// Set the standard OpenGL blending mode.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	// Change the constant alpha value. 
	float alpha = 1.0f - fConstantTransparency;

	// Set a color (with alpha). This color will be used directly if
	// the shader is not textured. Otherwise, the texture will get modulated
	// by the alpha.
	glColor4f(bgColor[0], bgColor[1], bgColor[2], alpha);


	// If the shader is textured...
	if (decalName.length() != 0)
	{
		// Enable 2D texturing.
		glEnable(GL_TEXTURE_2D);

		assert(glGetError() == GL_NO_ERROR);

		// Bind the 2D texture through the texture cache. The cache will keep
		// the texture around, so that it will only be loaded in video
		// memory once. In this example, the third parameter (mipmapping) is
		// false, so no mipmaps are generated. Note that mipmaps only work if
		// the texture has even dimensions.

		if(m_pTextureCache)
			m_pTextureCache->bind(colorConnection.texture(), MTexture::RGBA, false);	
		
		// Set minification and magnification filtering to linear interpolation.
		// For better quality, you could enable mipmapping while binding and
		// use GL_MIPMAP_LINEAR_MIPMAP in for minification filtering.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	}

	// Disable lighting.
	glDisable(GL_LIGHTING);

	view.endGL();

	return MS::kSuccess;
}


/* virtual */
MStatus	hwUnlitShader::unbind(const MDrawRequest& request,
			   M3dView& view)
{
	view.beginGL();	
	glPopClientAttrib();
	glPopAttrib();

	view.endGL();

	return MS::kSuccess;
}

/* virtual */
MStatus	hwUnlitShader::geometry( const MDrawRequest& request,
								M3dView& view,
							    int prim,
								unsigned int writable,
								int indexCount,
								const unsigned int * indexArray,
								int vertexCount,
								const int * vertexIDs,
								const float * vertexArray,
								int normalCount,
								const float ** normalArrays,
								int colorCount,
								const float ** colorArrays,
								int texCoordCount,
								const float ** texCoordArrays)
{
	view.beginGL();

	glVertexPointer(3, GL_FLOAT, 0, vertexArray);
	glEnableClientState(GL_VERTEX_ARRAY);

	if (normalCount > 0)
	{
		// Technically, we don't need the normals for this example. But
		// most of the 3rd party plug-ins will probably want the normal,
		// which is why the following lines were kept.
		glNormalPointer(GL_FLOAT, 0, normalArrays[0]);
		glEnableClientState(GL_NORMAL_ARRAY);
	}

	if (texCoordCount > 0)
	{
		glTexCoordPointer(2, GL_FLOAT, 0, texCoordArrays[0]);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	glDrawElements(prim, indexCount, GL_UNSIGNED_INT, indexArray);

	view.endGL();

	return MS::kSuccess;
}

/* virtual */
int		hwUnlitShader::normalsPerVertex()
{
	// Want only normals
	return 1;
}

/* virtual */
int		hwUnlitShader::texCoordsPerVertex()
{
	return 1;
}

/* virtual */
bool	hwUnlitShader::hasTransparency()
{
	// Performance note: if we knew that the texture  
	// is always opaque, we could return false here
	// to avoid the computation cost associated with
	// ordering objects from farthest to closest.
	return true;
}
