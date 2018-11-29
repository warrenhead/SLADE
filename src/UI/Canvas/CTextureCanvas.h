#pragma once

#include "General/ListenerAnnouncer.h"
#include "OGLCanvas.h"
#include "OpenGL/GLTexture.h"

wxDECLARE_EVENT(EVT_DRAG_END, wxCommandEvent);

class CTexture;
class Archive;

class CTextureCanvas : public OGLCanvas, Listener
{
public:
	enum class View
	{
		Normal,
		Sprite,
		HUD
	};

	CTextureCanvas(wxWindow* parent, int id);
	~CTextureCanvas();

	CTexture* getTexture() { return texture_; }
	View      getViewType() { return view_type_; }
	void      setScale(double scale) { this->scale_ = scale; }
	void      setViewType(View type) { this->view_type_ = type; }
	void      drawOutside(bool draw = true) { draw_outside_ = draw; }
	point2_t  getMousePrevPos() { return mouse_prev_; }
	bool      isDragging() { return dragging_; }
	bool      showGrid() { return show_grid_; }
	void      showGrid(bool show = true) { show_grid_ = show; }
	void      blendRGBA(bool rgba) { blend_rgba_ = rgba; }
	bool      getBlendRGBA() { return blend_rgba_; }
	bool      applyTexScale() { return tex_scale_; }
	void      applyTexScale(bool apply) { tex_scale_ = apply; }

	void selectPatch(int index);
	void deSelectPatch(int index);
	bool patchSelected(int index);

	void clearTexture();
	void clearPatchTextures();
	void updatePatchTextures();
	void updateTexturePreview();
	bool openTexture(CTexture* tex, Archive* parent);
	void draw();
	void drawTexture();
	void drawPatch(int num, bool outside = false);
	void drawTextureBorder();
	void drawOffsetLines();
	void resetOffsets() { offset_.x = offset_.y = 0; }
	void redraw(bool update_tex = false);

	point2_t screenToTexPosition(int x, int y);
	point2_t texToScreenPosition(int x, int y);
	int      patchAt(int x, int y);

	bool swapPatches(size_t p1, size_t p2);

	void onAnnouncement(Announcer* announcer, string event_name, MemChunk& event_data);

private:
	CTexture*          texture_;
	Archive*           parent_;
	vector<GLTexture*> patch_textures_;
	GLTexture          tex_preview_;
	vector<bool>       selected_patches_;
	int                hilight_patch_;
	fpoint2_t          offset_;
	point2_t           mouse_prev_;
	double             scale_;
	bool               draw_outside_;
	bool               dragging_;
	bool               show_grid_;
	bool               blend_rgba_;
	bool               tex_scale_;
	View               view_type_;

	// Events
	void onMouseEvent(wxMouseEvent& e);
};
