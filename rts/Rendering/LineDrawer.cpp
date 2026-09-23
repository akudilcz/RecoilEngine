/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// TODO: move this out of Sim, this is rendering code!

#include "LineDrawer.h"

#include <cmath>

#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Game/UI/CommandColors.h"

CLineDrawer lineDrawer;


CLineDrawer::CLineDrawer()
	: lineStipple(false)
	, useColorRestarts(false)
	, useRestartColor(false)
	, restartAlpha(0.0f)
	, restartColor(NULL)
	, lastPos(ZeroVector)
	, lastColor(NULL)
	, stippleTimer(0.0f)
{
	lineVerts.reserve(256);
	stippledVerts.reserve(256);
}


void CLineDrawer::UpdateLineStipple()
{
	stippleTimer += (globalRendering->lastFrameTime * 0.001f * cmdColors.StippleSpeed());
	stippleTimer = std::fmod(stippleTimer, (16.0f / 20.0f));
}


void CLineDrawer::SetupLineStipple()
{
	const unsigned int stipPat = (0xffff & cmdColors.StipplePattern());
	if ((stipPat != 0x0000) && (stipPat != 0xffff)) {
		lineStipple = true;
	} else {
		lineStipple = false;
		return;
	}
	const unsigned int fullPat = (stipPat << 16) | (stipPat & 0x0000ffff);
	const int shiftBits = 15 - (int(stippleTimer * 20.0f) % 16);
	glLineStipple(cmdColors.StippleFactor(), (fullPat >> shiftBits));
}


void CLineDrawer::DrawAll()
{
	if (lineVerts.empty() && stippledVerts.empty())
		return;

	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LINE_STIPPLE);

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
	auto& shader = rb.GetShader();

	if (!lineVerts.empty()) {
		rb.AddVertices(lineVerts);

		shader.Enable();
		rb.DrawArrays(GL_LINES);
		shader.Disable();
	}

	if (!stippledVerts.empty()) {
		glEnable(GL_LINE_STIPPLE);

		rb.AddVertices(stippledVerts);

		shader.Enable();
		rb.DrawArrays(GL_LINES);
		shader.Disable();

		glDisable(GL_LINE_STIPPLE);
	}

	glPopAttrib();

	// keep capacity, only drop the contents
	lineVerts.clear();
	stippledVerts.clear();
}
