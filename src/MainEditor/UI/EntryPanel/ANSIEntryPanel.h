#pragma once

#include "EntryPanel.h"

class ANSICanvas;

class ANSIEntryPanel : public EntryPanel
{
public:
	ANSIEntryPanel(wxWindow* parent);
	~ANSIEntryPanel() = default;

	bool saveEntry() override;

	static const int DATASIZE = 4000;

protected:
	bool loadEntry(ArchiveEntry* entry) override;

private:
	ANSICanvas*     ansi_canvas_ = nullptr;
	vector<uint8_t> ansi_chardata_;
};
