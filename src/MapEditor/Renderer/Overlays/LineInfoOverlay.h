#pragma once

#include "OpenGL/Drawing.h"

class MapLine;

class LineInfoOverlay
{
public:
	LineInfoOverlay();
	~LineInfoOverlay() = default;

	void update(MapLine* line);
	void draw(int bottom, int right, float alpha = 1.0f);

private:
	double  scale_ = 1.;
	TextBox text_box_;
	int     last_size_ = 100;

	struct Side
	{
		bool     exists;
		wxString info;
		wxString offsets;
		wxString tex_upper;
		wxString tex_middle;
		wxString tex_lower;
		bool     needs_upper;
		bool     needs_middle;
		bool     needs_lower;
	};
	Side side_front_{};
	Side side_back_{};

	void drawSide(int bottom, int right, float alpha, Side& side, int xstart = 0);
	void drawTexture(float alpha, int x, int y, wxString texture, bool needed, const wxString& pos = "U") const;
};
