/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _LINE_DRAWER_H
#define _LINE_DRAWER_H

#include <vector>
#include <array>

#include "Game/UI/CursorIcons.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/VertexArrayTypes.h"

class CLineDrawer {
	public:
		CLineDrawer();

		void Configure(bool useColorRestarts, bool useRestartColor,
		               const float* restartColor, float restartAlpha);

		void SetupLineStipple();
		void UpdateLineStipple();

		void StartPath(const float3& pos, const float* color);
		void FinishPath() const;
		void DrawLine(const float3& endPos, const float* color);
		void DrawLineAndIcon(int cmdID, const float3& endPos, const float* color);
		void DrawIconAtLastPos(int cmdID);
		void Break(const float3& endPos, const float* color);
		void Restart();
		/// now same as restart
		void RestartSameColor();
		void RestartWithColor(const float* color);
		const float3& GetLastPos() const { return lastPos; }

		void DrawAll();

	private:
		bool lineStipple;
		bool useColorRestarts;
		bool useRestartColor;
		float restartAlpha;
		const float* restartColor;

		float3 lastPos;
		const float* lastColor;

		float stippleTimer;

		// queue all line segments (as independent GL_LINES pairs) and draw
		// each list in a single batched call later; storage persists across
		// frames (only cleared, never freed) to avoid reallocating every frame
		std::vector<VA_TYPE_C> lineVerts;
		std::vector<VA_TYPE_C> stippledVerts;
};


extern CLineDrawer lineDrawer;


/******************************************************************************/
//
//  Inlines
//

inline void CLineDrawer::Configure(bool ucr, bool urc,
                                   const float* rc, float ra)
{
	restartAlpha = ra;
	restartColor = rc;
	useRestartColor = urc;
	useColorRestarts = ucr;
}


inline void CLineDrawer::FinishPath() const
{
	// noop, left for compatibility
}


inline void CLineDrawer::Break(const float3& endPos, const float* color)
{
	lastPos = endPos;
	lastColor = color;
}


inline void CLineDrawer::Restart()
{
	// nothing to do: segments are stored flat and independent of each other
	// (GL_LINES pairs), so a new path simply starts from lastPos/lastColor
	// without needing to open a new strip/list entry
}


inline void CLineDrawer::RestartWithColor(const float *color)
{
	lastColor = color;
	Restart();
}


inline void CLineDrawer::RestartSameColor()
{
	// the only way for this to work would be using glGet AFAIK
	// so it's left broken
	Restart();
}


inline void CLineDrawer::StartPath(const float3& pos, const float* color)
{
	lastPos = pos;
	lastColor = color;
	Restart();
}


inline void CLineDrawer::DrawLine(const float3& endPos, const float* color)
{
	std::vector<VA_TYPE_C>& verts = lineStipple ? stippledVerts : lineVerts;

	if (!useColorRestarts) {
		verts.emplace_back(VA_TYPE_C{ lastPos, SColor(lastColor) });
		verts.emplace_back(VA_TYPE_C{ endPos , SColor(color) });
	} else {
		if (useRestartColor) {
			verts.emplace_back(VA_TYPE_C{ lastPos, SColor(restartColor) });
		} else {
			const float startColor[4] = { color[0], color[1], color[2], color[3] * restartAlpha };
			verts.emplace_back(VA_TYPE_C{ lastPos, SColor(startColor) });
		}
		verts.emplace_back(VA_TYPE_C{ endPos, SColor(color) });
	}

	lastPos = endPos;
	lastColor = color;
}


inline void CLineDrawer::DrawLineAndIcon(
                         int cmdID, const float3& endPos, const float* color)
{
	cursorIcons.AddIcon(cmdID, endPos);
	DrawLine(endPos, color);
}


inline void CLineDrawer::DrawIconAtLastPos(int cmdID)
{
	cursorIcons.AddIcon(cmdID, lastPos);
}


#endif // _LINE_DRAWER_H
