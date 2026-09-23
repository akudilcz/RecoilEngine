/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _AIRLOS_TEXTURE_H
#define _AIRLOS_TEXTURE_H

#include "ModernInfoTexture.h"
#include "Rendering/GL/FBO.h"
#include "System/Misc/SpringTime.h"


namespace Shader {
	struct IProgramObject;
}


class CAirLosTexture : public CModernInfoTexture
{
public:
	CAirLosTexture();
	~CAirLosTexture();

public:
	void Update() override;
	bool IsUpdateNeeded() override;

	GLuint GetTexture() override;
private:
	GL::Texture2D uploadTex;

	bool everUpdated = false;
	bool lastGlobalLos = false;
	unsigned int lastAirLosCounter = 0;
	spring_time lastUsage;
};

#endif // _AIRLOS_TEXTURE_H
