/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GRASSDRAWER_H
#define GRASSDRAWER_H

#include <vector>

#include "Rendering/GL/VBO.h"
#include "Rendering/GL/VAO.h"
#include "Rendering/GL/VertexArrayTypes.h"
#include "System/float3.h"
#include "System/EventClient.h"

namespace Shader {
	struct IProgramObject;
}


class CGrassDrawer : public CEventClient
{
public:
	CGrassDrawer();
	~CGrassDrawer();

	void Draw();
	void DrawShadow();
	void AddGrass(const float3& pos,  const uint8_t grassValue);
	void ResetPos(const float3& pos);
	void RemoveGrass(const float3& pos);
	unsigned char GetGrass(const float3& pos);

	void ChangeDetail(int detail);

	/// @see ConfigHandler::ConfigNotifyCallback
	void ConfigNotify(const std::string& key, const std::string& value);

public:
	// EventClient
	void UnsyncedHeightMapUpdate(const SRectangle& rect);
	void Update();

public:
	struct InviewNearGrass {
		int x;
		int y;
		float dist;
	};
	struct GrassStruct {
		GrassStruct()
			: posX(0)
			, posZ(0)
			, lastSeen(0)
			, lastFar(0)
			, lastDist(0.0f)
			, vboVertOffset(0)
			, vboVertCount(0)
		{}

		int posX;
		int posZ;

		int lastSeen;
		int lastFar;
		float lastDist;

		// fixed slot (in vertices) of this block's billboard quads within grassFarVBO,
		// and the number of vertices currently valid there (0 == nothing to draw)
		unsigned int vboVertOffset;
		unsigned int vboVertCount;
	};

	enum GrassShaderProgram {
		GRASS_PROGRAM_NEAR        = 0,
		GRASS_PROGRAM_DIST        = 1,
		GRASS_PROGRAM_SHADOW_GEN  = 2,
		GRASS_PROGRAM_LAST        = 3
	};

protected:
	void LoadGrassShaders();
	void CreateGrassBladeTex(unsigned char* buf);
	void CreateFarTex();
	void CreateGrassDispList(int listNum);
	void CreateGrassMeshBuffers();

	void EnableShader(const GrassShaderProgram type);
	void SetupGlStateNear();
	void ResetGlStateNear();
	void SetupGlStateFar();
	void ResetGlStateFar();
	void RebuildNearInstances(const std::vector<InviewNearGrass>& inviewGrass);
	void DrawNear();
	void DrawFarBillboards(const std::vector<GrassStruct*>& inviewGrass);
	void DrawNearBillboards(const std::vector<InviewNearGrass>& inviewNearGrass);
	void DrawBillboard(const int x, const int y, const float dist, VA_TYPE_TN* va_tn);

	void ResetPos(const int grassBlockX, const int grassBlockZ);

protected:
	friend class CGrassBlockDrawer;

	int blocksX;
	int blocksY;

	unsigned int grassDL;
	unsigned int grassBladeTex;
	unsigned int farTex;

	// near mesh grass: static turf mesh + per-instance (pos, rotation) transforms,
	// instanced-drawn in a single call; instance buffer only rebuilt on visibility change
	VBO grassMeshVBO;
	VAO grassMeshVAO;
	VBO grassInstanceVBO;
	unsigned int grassMeshVertexCount;
	unsigned int grassNumInstances;

	// far (per-block) billboard quads: one persistent, GPU-resident VBO holding a fixed-size
	// slot per grass block; only dirty blocks get re-uploaded (glBufferSubData)
	VBO grassFarVBO;
	unsigned int grassFarVertsPerBlock;

	// near billboards ("near but not close" turfs): single combined VBO, rebuilt only
	// when the visible set changes instead of every frame
	VBO grassNearBillboardVBO;
	unsigned int grassNearBillboardVertCount;

	std::vector<GrassStruct> grass;
	std::vector<unsigned char> grassMap;

	std::vector<Shader::IProgramObject*> grassShaders;
	Shader::IProgramObject* grassShader;

	float maxGrassDist;
	float maxDetailedDist;
	int detailedBlocks;
	int numTurfs;
	int strawPerTurf;

	float3 oldCamPos;
	float3 oldCamDir;
	int lastVisibilityUpdate;

	bool grassOff;
	bool updateBillboards;
	bool updateNearBillboards;
	bool updateVisibility;
};

extern CGrassDrawer* grassDrawer;


#endif /* GRASSDRAWER_H */
