/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _RADAR_TEXTURE_H
#define _RADAR_TEXTURE_H


#include "ModernInfoTexture.h"
#include "Rendering/GL/FBO.h"
#include "System/Misc/SpringTime.h"


namespace Shader {
	struct IProgramObject;
}


class CRadarTexture : public CModernInfoTexture
{
public:
	CRadarTexture();
	~CRadarTexture() override;

public:
	void Update() override;
	bool IsUpdateNeeded() override;

	GLuint GetTexture() override;
private:
	GL::Texture2D uploadTexRadar;
	GL::Texture2D uploadTexJammer;

	bool everUpdated = false;
	bool lastGlobalLos = false;
	int lastAllyTeam = -1; // the counters are per ally team: a switch must force an update
	unsigned int lastRadarCounter = 0;
	unsigned int lastJammerCounter = 0;
	spring_time lastUsage;
};

#endif // _RADAR_TEXTURE_H
