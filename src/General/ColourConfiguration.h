#pragma once

#include "OpenGL/OpenGL.h"
#include "Utility/Colour.h"

namespace ColourConfiguration
{
struct Colour
{
	bool     exists = false;
	bool     custom = false;
	wxString name;
	wxString group;
	ColRGBA  colour;
	bool     blend_additive;

	OpenGL::Blend blendMode() const { return blend_additive ? OpenGL::Blend::Additive : OpenGL::Blend::Normal; }
};

ColRGBA       colour(const wxString& name);
const Colour& colDef(const wxString& name);
void          setColour(
			 const wxString& name,
			 int             red            = -1,
			 int             green          = -1,
			 int             blue           = -1,
			 int             alpha          = -1,
			 bool            blend_additive = false);

void setGLColour(const wxString& name, float alpha_mult = 1.f);

double lineHilightWidth();
double lineSelectionWidth();
double flatAlpha();
void   setLineHilightWidth(double mult);
void   setLineSelectionWidth(double mult);
void   setFlatAlpha(double alpha);

bool readConfiguration(MemChunk& mc);
bool writeConfiguration(MemChunk& mc);
bool init();
void loadDefaults();

bool readConfiguration(const wxString& name);
void putConfigurationNames(vector<wxString>& names);

void putColourNames(vector<wxString>& list);
} // namespace ColourConfiguration
