//========= HL2RPM ============================================================//
//
// A model turning on the card of an item (the Tab menu, loot and trade windows).
// The window asks for it while it paints; the model goes into a render target
// after the frame's scene (hl2rpm_itempreview.cpp), the window draws that target.
//
//=============================================================================//
#ifndef HL2RPM_ITEMPREVIEW_H
#define HL2RPM_ITEMPREVIEW_H
#pragma once

class Color;

namespace RPMItemPreview
{
	// where a card starts: the long side of a thing across the view, a little turned
	const float YAW_START = 60.0f;
	const float PITCH_START = 12.0f;

	// from a Paint(): shows pszModel in the rectangle (panel coordinates). The model is as big as
	// the rectangle allows at any angle of its turn; flZoom scales that. False = nothing to show
	// (no model, no render target): the window keeps its layout without it.
	bool Paint( const char *pszModel, int x, int y, int w, int h, float flYaw, float flPitch, float flZoom, int alpha, const Color &rim );

	// is there a model of this name in the game's files?
	bool HasModel( const char *pszModel );
}

void InitItemPreviewRT();			// from InitDeferredRTs
void RPMItemPreview_Render();		// from the view render, after the scene

#endif // HL2RPM_ITEMPREVIEW_H
