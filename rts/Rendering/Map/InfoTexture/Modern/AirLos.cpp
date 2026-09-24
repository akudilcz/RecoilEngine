/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "AirLos.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/GL/SubState.h"
#include "Sim/Misc/LosHandler.h"
#include "System/Exceptions.h"
#include "System/Log/ILog.h"

#include "System/Misc/TracyDefs.h"


CAirLosTexture::CAirLosTexture()
: CModernInfoTexture("airlos")
{
	texSize = losHandler->airLos.size;

	GL::TextureCreationParams tcp{
		.reqNumLevels = -1,
		.linearMipMapFilter = false,
		.linearTextureFilter = true,
		.wrapMirror = false
	};

	texture = GL::Texture2D(texSize, GL_R8, tcp, false);

	CreateFBO("CAirLosTexture");

	const std::string fragmentCode = R"(
		#version 130

		uniform sampler2D tex0;

		in vec2 uv;
		out float fragData;

		void main() {
			float f = texture(tex0, uv).r;

			fragData = float(f > 0.0);
		}
	)";

	shader = shaderHandler->CreateProgramObject("[CAirLosTexture]", "CAirLosTexture");
	shader->AttachShaderObject(shaderHandler->CreateShaderObject(vertexCode,   "", GL_VERTEX_SHADER));
	shader->AttachShaderObject(shaderHandler->CreateShaderObject(fragmentCode, "", GL_FRAGMENT_SHADER));
	shader->Link();
	if (!shader->IsValid()) {
		const char* fmt = "%s-shader compilation error: %s";
		LOG_L(L_ERROR, fmt, shader->GetName().c_str(), shader->GetLog().c_str());
	} else {
		shader->Enable();
		shader->SetUniform("tex0", 0);
		shader->Disable();
		shader->Validate();
		if (!shader->IsValid()) {
			const char* fmt = "%s-shader validation error: %s";
			LOG_L(L_ERROR, fmt, shader->GetName().c_str(), shader->GetLog().c_str());
		}
	}
	{
		GL::TextureCreationParams tcp{
			.reqNumLevels = 1,
			.wrapMirror = false,
			.minFilter = GL_NEAREST,
			.magFilter = GL_NEAREST
		};

		uploadTex = GL::Texture2D(texSize, GL_R16, tcp, false);
	}

	if (!fbo.IsValid() || !shader->IsValid() || !uploadTex.IsValid()) {
		throw opengl_error("");
	}
}


CAirLosTexture::~CAirLosTexture()
{
	RECOIL_DETAILED_TRACY_ZONE;
	shaderHandler->ReleaseProgramObject("[CAirLosTexture]", "CAirLosTexture");
}

bool CAirLosTexture::IsUpdateNeeded()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!everUpdated)
		return true;

	// don't bother refreshing textures nobody has sampled recently (e.g. an
	// info-texture mode that isn't currently active/composited); GetTexture()
	// force-updates on demand, so this can never hand out stale data.
	if ((spring_gettime() - lastUsage).toSecsi() > 2)
		return false;

	const bool globalLos = losHandler->GetGlobalLOS(gu->myAllyTeam);

	if (globalLos != lastGlobalLos)
		return true;
	if (globalLos)
		return false;

	return (losHandler->airLos.losMaps[gu->myAllyTeam].GetChangeCounter() != lastAirLosCounter);
}


GLuint CAirLosTexture::GetTexture()
{
	RECOIL_DETAILED_TRACY_ZONE;
	lastUsage = spring_gettime();

	// only record usage here: Update() binds its own FBO and viewport and could be
	// reached mid-draw (e.g. Lua sampling $info:*), so staleness after an idle period
	// is resolved by the next regular update instead of an on-demand one

	return CModernInfoTexture::GetTexture();
}


void CAirLosTexture::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	everUpdated = true;
	lastGlobalLos = losHandler->GetGlobalLOS(gu->myAllyTeam);

	if (lastGlobalLos) {
		fbo.Bind();
		glViewport(0, 0, texSize.x, texSize.y);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		globalRendering->LoadViewport();
		FBO::Unbind();

		auto binding = texture.ScopedBind();
		texture.ProduceMipmaps();
		return;
	}

	lastAirLosCounter = losHandler->airLos.losMaps[gu->myAllyTeam].GetChangeCounter();

	const auto& myAirLos = losHandler->airLos.losMaps[gu->myAllyTeam].GetLosMap();
	{
		auto binding = uploadTex.ScopedBind();
		uploadTex.UploadImage(myAirLos.data());
	}

	// do post-processing on the gpu (los-checking & scaling)
	using namespace GL::State;
	auto state = GL::SubState(
		Blending(GL_FALSE)
	);
	RunFullScreenPass();

	// generate mipmaps
	auto binding = texture.ScopedBind();
	texture.ProduceMipmaps();
}
