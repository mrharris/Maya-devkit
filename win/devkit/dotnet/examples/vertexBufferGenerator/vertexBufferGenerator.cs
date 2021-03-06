﻿// ==================================================================
// Copyright 2012 Autodesk, Inc.  All rights reserved.
// 
// This computer source code  and related  instructions and comments are
// the unpublished confidential and proprietary information of Autodesk,
// Inc. and are  protected  under applicable  copyright and trade secret
// law. They may not  be disclosed to, copied or used by any third party
// without the prior written consent of Autodesk, Inc.
// ==================================================================


/////////////////////////////////////////////////////////////////////////////
//
// vertexBufferGenerator.cs
//
// Description:
//		This plug-in is an example of a custom MPxVertexBufferGenerator.
//		It provides custom vertex streams based on shader requirements coming from
//		an MPxShaderOverride.  The semanticName() in the MVertexBufferDescriptor is used
//		to signify a unique identifier for a custom stream.
//
//		This plugin is meant to be used in conjunction with the d3d11Shader or cgfxShader plugins.
//		The .fx and .cgfx files accompanying this sample  (vertexBufferProfilerGL.cgfx and
//		vertexBufferGeneratorDX11.fx) can be loaded using the appropriate shader plugin.
//		The Names of the streams and the stream data generated by this plugin match what is
//		expected from the included effects files.
//
// Note:
//		This C# plugin is ported from: $(MAYADIR)\devkit\plug-ins\vertexBufferGenerator.
//
// Steps (as shown in the accompanying MEL script):
//		1. Create or load a triangulated mesh.
//		2. Switch to Viewport 2.0 and turn on Hardware Texturing.
//		2. Load plugins: cgfxShader.mll and vertexBufferGenerator.nll.dll.
//		4. Assign New Material -> Cgfx Shader -> vertexBuffergeneratorGL.cgfx.
//
/////////////////////////////////////////////////////////////////////////////

using System;

using Autodesk.Maya.OpenMaya;
using Autodesk.Maya.OpenMayaRender;
using Autodesk.Maya.OpenMayaRender.MHWRender;

//// register a generator based on a custom semantic for DX11.  You can use any name in DX11.
//[assembly: MPxVertexBufferGeneratorClass(typeof(MayaNetPlugin.MyVertexBufferGenerator), "myCustomStream")]

// register a generator based on a custom semantic for cg.
// Pretty limited in cg so we hook onto the "ATTR" semantics.

[assembly: MPxVertexBufferGeneratorClass(typeof(MayaNetPlugin.MyVertexBufferGenerator), "ATTR8")]

namespace MayaNetPlugin
{
	class MyVertexBufferGenerator : MPxVertexBufferGenerator
	{
        public override bool getSourceIndexing(MObject obj, MComponentDataIndexing sourceIndexing)
		{
            // get the mesh from the current path, if it is not a mesh we do nothing.
			MFnMesh mesh = null;
			try {
				mesh = new MFnMesh(obj);
			} catch(System.Exception) {
				return false;
			}

			// if it is an empty mesh we do nothing.
			int numPolys = mesh.numPolygons;
			if (numPolys <= 0) return false;

			// for each face
            MUintArray vertToFaceVertIDs = sourceIndexing.indicesProperty;
			uint faceNum = 0;
			for (int i = 0; i < numPolys; i++)
			{
				// assign a color ID to all vertices in this face.
				uint faceColorID = faceNum % 3;

				int vertexCount = mesh.polygonVertexCount(i);
				for (int j = 0; j < vertexCount; j++)
				{
					// set each face vertex to the face color
					vertToFaceVertIDs.append(faceColorID);
				}

				faceNum++;
			}

			// assign the source indexing
			sourceIndexing.setComponentType(MComponentDataIndexing.MComponentType.kFaceVertex);

			return true;
		}

		public override bool getSourceStreams(MObject objPath, MStringArray sourceStreams)
		{
			// No source stream needed
			return false;
		}

        public override void createVertexStream(MObject objPath,
												MVertexBuffer vertexBuffer,
												MComponentDataIndexing targetIndexing,
												MComponentDataIndexing sharedIndexing,
												MVertexBufferArray sourceStreams)
		{
			// get the descriptor from the vertex buffer.
			// It describes the format and layout of the stream.
			MVertexBufferDescriptor descriptor = vertexBuffer.descriptor;

			// we are expecting a float stream.
			if (descriptor.dataType != Autodesk.Maya.OpenMayaRender.MHWRender.MGeometry.DataType.kFloat) return;

			// we are expecting a float2
			if (descriptor.dimension != 2) return;

			// we are expecting a texture channel
			if (descriptor.semantic != Autodesk.Maya.OpenMayaRender.MHWRender.MGeometry.Semantic.kTexture) return;

			// get the mesh from the current path, if it is not a mesh we do nothing.
			MFnMesh mesh = null;
			try {
				mesh = new MFnMesh(objPath);
			} catch(System.Exception) {
				return; // failed
			}

			MUintArray indices = targetIndexing.indicesProperty;
			uint vertexCount = indices.length;
			if (vertexCount <= 0) return;

            unsafe {
			    // acquire the buffer to fill with data.
			    float * buffer = (float *)vertexBuffer.acquire(vertexCount);

			    for (int i = 0; i < vertexCount; i++)
			    {
				    // Here we are embedding some custom data into the stream.
				    // The included effects (vertexBufferGeneratorGL.cgfx and
				    // vertexBufferGeneratorDX11.fx) will alternate 
				    // red, green, and blue vertex colored triangles based on this input.
				    *(buffer++) = 1.0f;
				    *(buffer++) = (float)indices[i]; // color index
			    }

			    // commit the buffer to signal completion.
			    vertexBuffer.commit( (byte *)buffer);
            }
		}
	}
}
