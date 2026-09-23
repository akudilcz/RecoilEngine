/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef CUBEMAP_HANDLER_HDR
#define CUBEMAP_HANDLER_HDR

#include "Rendering/GL/FBO.h"
#include "System/float4.h"

class CubeMapHandler {
public:
	CubeMapHandler(): reflectionCubeFBO(true) {}

	bool Init();
	void Free();

	void UpdateReflectionTexture();
	void UpdateSpecularTexture();

	unsigned int GetEnvReflectionTextureID() const { return envReflectionTexID; }
	unsigned int GetSkyReflectionTextureID() const { return skyReflectionTexID; }
	unsigned int GetSpecularTextureID() const { return specularTexID; }
	unsigned int GetReflectionTextureSize() const { return reflTexSize; }
	unsigned int GetSpecularTextureSize() const { return specTexSize; }

private:
	void CreateReflectionFace(unsigned int, bool);
	void CreateSpecularFacePart(unsigned int, unsigned int, const float3&, const float3&, const float3&, unsigned int, unsigned char*);
	void CreateSpecularFace(unsigned int, unsigned int, const float3&, const float3&, const float3&);
	void UpdateSpecularFace(unsigned int, unsigned int, const float3&, const float3&, const float3&, unsigned int, unsigned char*);

	// returns true if anything the reflection cubemap depends on (camera
	// position, heightmap, sun direction, sky or sun-lighting appearance)
	// has changed since the last completed render cycle; conservative by
	// design, i.e. defaults to "changed" whenever unsure
	bool ReflectionInputsChanged() const;
	void SnapshotReflectionInputs();

	unsigned int envReflectionTexID; // sky and map
	unsigned int skyReflectionTexID; // sky only
	unsigned int specularTexID;

	unsigned int reflTexSize;
	unsigned int specTexSize;

	unsigned int currReflectionFace;
	unsigned int specularTexIter;

	bool mapSkyReflections;
	bool generateMipMaps;

	// set once a full face cycle has completed at least one time; until then
	// (or after any relevant input changes) we keep refreshing every frame
	bool reflectionCycleValid = false;

	float3 lastReflectionCamPos;

	float4 lastSunLightDir;

	float3 lastSkyColor;
	float3 lastSunColor;
	float3 lastCloudColor;
	float4 lastFogColor;
	float  lastCloudDensity = 0.0f;
	float4 lastSkyAxisAngle;

	float4 lastGroundAmbientColor;
	float4 lastGroundDiffuseColor;
	float4 lastGroundSpecularColor;
	float4 lastModelAmbientColor;
	float4 lastModelDiffuseColor;
	float4 lastModelSpecularColor;
	float  lastSpecularExponent = 0.0f;
	float  lastGroundShadowDensity = 0.0f;
	float  lastModelShadowDensity = 0.0f;

	std::vector<unsigned char> specTexPartBuf;
	std::vector<unsigned char> specTexFaceBuf;

	FBO reflectionCubeFBO;

	/*
	GL_TEXTURE_CUBE_MAP_POSITIVE_X
	GL_TEXTURE_CUBE_MAP_NEGATIVE_X
	GL_TEXTURE_CUBE_MAP_POSITIVE_Y
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
	GL_TEXTURE_CUBE_MAP_POSITIVE_Z
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
	*/

	const float3 faceDirs[6][3] = {
		{ RgtVector,  FwdVector,   UpVector}, // fwd = +x, right = +z, up = +y
		{-RgtVector, -FwdVector,   UpVector}, // fwd = -x
		{  UpVector, -RgtVector, -FwdVector}, // fwd = +y
		{ -UpVector, -RgtVector,  FwdVector}, // fwd = -y
		{ FwdVector, -RgtVector,   UpVector}, // fwd = +z
		{-FwdVector,  RgtVector,   UpVector}, // fwd = -z
	};
};

extern CubeMapHandler cubeMapHandler;

#endif
