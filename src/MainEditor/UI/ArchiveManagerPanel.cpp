
/*******************************************************************
 * SLADE - It's a Doom Editor
 * Copyright (C) 2008-2014 Simon Judd
 *
 * Email:       sirjuddington@gmail.com
 * Web:         http://slade.mancubus.net
 * Filename:    ArchiveManagerPanel.cpp
 * Description: ArchiveManagerPanel class. Basically the UI for the
 *              ArchiveManager class, has a tree of all open
 *              archives and basic file management controls.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *******************************************************************/


/*******************************************************************
 * INCLUDES
 *******************************************************************/
#include "Main.h"
#include "ArchiveManagerPanel.h"
#include "Archive/ArchiveManager.h"
#include "Archive/Formats/DirArchive.h"
#include "ArchivePanel.h"
#include "Dialogs/DirArchiveUpdateDialog.h"
#include "EntryPanel/EntryPanel.h"
#include "General/UI.h"
#include "Graphics/Icons.h"
#include "MainEditor/MainEditor.h"
#include "MainEditor/UI/MainWindow.h"
#include "MapEditor/MapEditor.h"
#include "MapEditor/UI/MapEditorWindow.h"
#include "TextureXEditor/TextureXEditor.h"
#include "UI/STabCtrl.h"


/*******************************************************************
 * VARIABLES
 *******************************************************************/
CVAR(Bool, close_archive_with_tab, true, CVAR_SAVE)
CVAR(Int, am_current_tab, 0, CVAR_SAVE)
CVAR(Bool, am_file_browser_tab, true, CVAR_SAVE)
CVAR(Bool, check_dir_archives, true, CVAR_SAVE)


/*******************************************************************
 * EXTERNAL VARIABLES
 *******************************************************************/
EXTERN_CVAR(String, dir_last);


/*******************************************************************
 * DIRARCHIVECHECK CLASS FUNCTIONS
 *******************************************************************/
wxDEFINE_EVENT(wxEVT_COMMAND_DIRARCHIVECHECK_COMPLETED, wxThreadEvent);

/* DirArchiveCheck::DirArchiveCheck
 * DirArchiveCheck class constructor
 *******************************************************************/
DirArchiveCheck::DirArchiveCheck(wxEvtHandler* handler, DirArchive* archive)
{
	this->handler = handler;
	dir_path = archive->filename();
	removed_files = archive->removedFiles();
	change_list.archive = archive;

	// Get flat entry list
	vector<ArchiveEntry*> entries;
	archive->getEntryTreeAsList(entries);

	// Build entry info list
	for (unsigned a = 0; a < entries.size(); a++)
	{
		entry_info_t inf;
		inf.file_path = entries[a]->exProp("filePath").getStringValue();
		inf.entry_path = entries[a]->getPath(true);
		inf.is_dir = (entries[a]->getType() == EntryType::folderType());
		inf.file_modified = archive->fileModificationTime(entries[a]);
		entry_info.push_back(inf);
	}
}

/* DirArchiveCheck::~DirArchiveCheck
 * DirArchiveCheck class destructor
 *******************************************************************/
DirArchiveCheck::~DirArchiveCheck()
{
}

/* DirArchiveCheck::addChange
 * Register a change to a file, as long as it hasn't been ignored
 *******************************************************************/
void DirArchiveCheck::addChange(DirEntryChange change)
{
	if (! ((DirArchive*)change_list.archive)->shouldIgnoreEntryChange(change))
		change_list.changes.push_back(change);
}

/* DirArchiveCheck::Entry
 * DirArchiveCheck thread entry function
 *******************************************************************/
wxThread::ExitCode DirArchiveCheck::Entry()
{
	// Get current directory structure
	vector<string> files, dirs;
	DirArchiveTraverser traverser(files, dirs);
	wxDir dir(dir_path);
	dir.Traverse(traverser, "", wxDIR_FILES | wxDIR_DIRS);

	// Check for deleted files
	for (unsigned a = 0; a < entry_info.size(); a++)
	{
		string path = entry_info[a].file_path;

		// Ignore if not on disk
		if (path.IsEmpty())
			continue;

		if (entry_info[a].is_dir)
		{
			if (!wxDirExists(path))
				addChange(DirEntryChange(DirEntryChange::DELETED_DIR, path, entry_info[a].entry_path));
		}
		else
		{
			if (!wxFileExists(path))
				addChange(DirEntryChange(DirEntryChange::DELETED_FILE, path, entry_info[a].entry_path));
		}
	}

	// Check for new/updated files
	for (unsigned a = 0; a < files.size(); a++)
	{
		// Ignore files removed from archive since last save
		if (VECTOR_EXISTS(removed_files, files[a]))
			continue;

		// Find file in archive
		entry_info_t inf;
		bool found = false;
		for (unsigned b = 0; b < entry_info.size(); b++)
		{
			if (entry_info[b].file_path == files[a])
			{
				inf = entry_info[b];
				found = true;
				break;
			}
		}

		time_t mod = wxFileModificationTime(files[a]);
		// No match, added to archive
		if (!found)
			addChange(DirEntryChange(DirEntryChange::ADDED_FILE, files[a], "", mod));
		// Matched, check modification time
		else if (mod > inf.file_modified)
			addChange(DirEntryChange(DirEntryChange::UPDATED, files[a], inf.entry_path, mod));
	}

	// Check for new dirs
	for (unsigned a = 0; a < dirs.size(); a++)
	{
		// Ignore dirs removed from archive since last save
		if (VECTOR_EXISTS(removed_files, dirs[a]))
			continue;

		// Find dir in archive
		entry_info_t inf;
		bool found = false;
		for (unsigned b = 0; b < entry_info.size(); b++)
		{
			if (entry_info[b].file_path == dirs[a])
			{
				inf = entry_info[b];
				found = true;
				break;
			}
		}

		time_t mod = wxFileModificationTime(dirs[a]);
		// No match, added to archive
		if (!found)
			addChange(DirEntryChange(DirEntryChange::ADDED_DIR, dirs[a], "", mod));
	}

	// Send changes via event
	wxThreadEvent* event = new wxThreadEvent(wxEVT_COMMAND_DIRARCHIVECHECK_COMPLETED);
	event->SetPayload<dir_archive_changelist_t>(change_list);
	wxQueueEvent(handler, event);

	return NULL;
}


/*******************************************************************
 * WMFILEBROWSER CLASS FUNCTIONS
 *******************************************************************/

/* WMFileBrowser::WMFileBrowser
 * WMFileBrowser class constructor
 *
 * Note: wxWidgets 2.9.4 deprecated the wxDIRCTRL_SHOW_FILTERS flag;
 * instead, the filters are always shown if any are defined.
 *******************************************************************/
WMFileBrowser::WMFileBrowser(wxWindow* parent, ArchiveManagerPanel* wm, int id)
	: wxGenericDirCtrl(parent, id, wxDirDialogDefaultFolderStr, wxDefaultPosition, wxDefaultSize, 0,
	"Any Supported Archive File (*.wad; *.zip; *.pk3; *.pke; *.lib; *.dat)|*.wad;*.zip;*.pk3;*.pke;*.lib;*.dat|Doom Wad files (*.wad)|*.wad|Zip files (*.zip)|*.zip|Pk3 (zip) files (*.pk3)|*.pk3|All Files (*.*)|*.*")
{
	// Set the parent
	this->parent = wm;

	// Connect a custom event for when an item in the file tree is activated
	GetTreeCtrl()->Connect(GetTreeCtrl()->GetId(), wxEVT_TREE_ITEM_ACTIVATED, wxTreeEventHandler(WMFileBrowser::onItemActivated));
}

/* WMFileBrowser::~WMFileBrowser
 * WMFileBrowser class destructor
 *******************************************************************/
WMFileBrowser::~WMFileBrowser()
{
}

/* WMFileBrowser::onItemActivated
 * Event called when an item in the tree is activated. Opens a file
 * if one is selected.
 *******************************************************************/
void WMFileBrowser::onItemActivated(wxTreeEvent& e)
{
	// Get related objects
	wxTreeCtrl* tree = (wxTreeCtrl*) e.GetEventObject();
	WMFileBrowser* browser = (WMFileBrowser*) tree->GetParent();

	// If the selected item has no children (ie it's a file),
	// open it in the archive manager
	if (!tree->ItemHasChildren(e.GetItem()))
		browser->parent->openFile(browser->GetPath());

	e.Skip();
}


/*******************************************************************
 * ARCHIVEMANAGERPANEL CLASS FUNCTIONS
 *******************************************************************/

/* ArchiveManagerPanel::ArchiveManagerPanel
 * ArchiveManagerPanel class constructor
 *******************************************************************/
ArchiveManagerPanel::ArchiveManagerPanel(wxWindow* parent, STabCtrl* nb_archives)
	: DockPanel(parent)
{
	stc_archives = nb_archives;
	pending_closed_archive = NULL;
	checked_dir_archive_changes = false;
	asked_save_unchanged = false;

	// Create main sizer
	wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
	SetSizer(vbox);

	// Create/setup tabs
	stc_tabs = new STabCtrl(this, false);
	stc_tabs->SetInitialSize(wxSize(224, -1));
	vbox->Add(stc_tabs, 1, wxEXPAND | wxALL, 4);

	// Open archives tab
	panel_am = new wxPanel(stc_tabs);
	stc_tabs->AddPage(panel_am, "Archives", true);

	// Create/setup archive list
	createArchivesPanel();
	refreshArchiveList();

	// Create/setup recent files list and menu
	menu_recent = new wxMenu();
	createRecentPanel();
	refreshRecentFileList();

	// Create/setup bookmarks tab
	wxPanel* panel_bm = new wxPanel(stc_tabs);
	wxBoxSizer* box_bm = new wxBoxSizer(wxVERTICAL);
	panel_bm->SetSizer(box_bm);
	box_bm->Add(new wxStaticText(panel_bm, -1, "Bookmarks:"), 0, wxEXPAND | wxALL, 4);
	list_bookmarks = new ListView(panel_bm, -1);
	box_bm->Add(list_bookmarks, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	refreshBookmarkList();
	stc_tabs->AddPage(panel_bm, "Bookmarks", true);

	// Create/setup file browser tab
	// Used to cause problems, however seems to work much better now than what I can remember,
	// adding a cvar to disable in case it still has issues
	if (am_file_browser_tab)
	{
		file_browser = new WMFileBrowser(stc_tabs, this, -1);
		stc_tabs->AddPage(file_browser, "File Browser");
	}

	// Set current tab
	stc_tabs->SetSelection(am_current_tab);

	// Bind events
	list_archives->Bind(wxEVT_LIST_ITEM_SELECTED, &ArchiveManagerPanel::onListArchivesChanged, this);
	list_archives->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ArchiveManagerPanel::onListArchivesActivated, this);
	list_archives->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &ArchiveManagerPanel::onListArchivesRightClick, this);
	list_recent->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ArchiveManagerPanel::onListRecentActivated, this);
	list_recent->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &ArchiveManagerPanel::onListRecentRightClick, this);
	list_bookmarks->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ArchiveManagerPanel::onListBookmarksActivated, this);
	list_bookmarks->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &ArchiveManagerPanel::onListBookmarksRightClick, this);
	stc_archives->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGING, &ArchiveManagerPanel::onArchiveTabChanging, this);
	stc_archives->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ArchiveManagerPanel::onArchiveTabChanged, this);
	stc_archives->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &ArchiveManagerPanel::onArchiveTabClose, this);
	stc_archives->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSED, &ArchiveManagerPanel::onArchiveTabClosed, this);
	stc_tabs->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ArchiveManagerPanel::onAMTabChanged, this);
	Bind(wxEVT_COMMAND_DIRARCHIVECHECK_COMPLETED, &ArchiveManagerPanel::onDirArchiveCheckCompleted, this);

	// Listen to the ArchiveManager
	listenTo(&App::archiveManager());

	// Init layout
	Layout();
	SetInitialSize(wxSize(256, -1));
}

/* ArchiveManagerPanel::~ArchiveManagerPanel
 * ArchiveManagerPanel class destructor
 *******************************************************************/
ArchiveManagerPanel::~ArchiveManagerPanel()
{
}

/* ArchiveManagerPanel::createArchivesPanel
 * Creates the 'Open Archives' panel
 *******************************************************************/
void ArchiveManagerPanel::createArchivesPanel()
{
	panel_archives = new wxPanel(panel_am, -1);
	wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
	panel_archives->SetSizer(vbox);
	vbox->Add(new wxStaticText(panel_archives, -1, "Open Archives:"), 0, wxEXPAND);
	list_archives = new ListView(panel_archives, -1);
	vbox->Add(list_archives, 1, wxEXPAND|wxTOP, 4);
}

/* ArchiveManagerPanel::createRecentPanel
 * Creates the 'Recent Files' panel
 *******************************************************************/
void ArchiveManagerPanel::createRecentPanel()
{
	panel_rf = new wxPanel(panel_am, -1);
	wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
	panel_rf->SetSizer(vbox);
	vbox->Add(new wxStaticText(panel_rf, -1, "Recent Files:"), 0, wxEXPAND);
	list_recent = new ListView(panel_rf, -1);
	vbox->Add(list_recent, 1, wxEXPAND|wxTOP, 4);

	// Setup image list
	wxImageList* list = new wxImageList(16, 16, false, 0);
	list->Add(Icons::getIcon(Icons::ENTRY, "archive"));
	list->Add(Icons::getIcon(Icons::ENTRY, "wad"));
	list->Add(Icons::getIcon(Icons::ENTRY, "zip"));
	list->Add(Icons::getIcon(Icons::ENTRY, "folder"));
	list_recent->SetImageList(list, wxIMAGE_LIST_SMALL);
}

/* ArchiveManagerPanel::layoutNormal
 * Layout the panel normally
 *******************************************************************/
void ArchiveManagerPanel::layoutNormal()
{
	// Layout archives tab vertically
	wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
	panel_am->SetSizer(vbox);

	vbox->Add(panel_archives, 1, wxEXPAND|wxALL, 4);
	vbox->Add(panel_rf, 1, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 4);
}

/* ArchiveManagerPanel::layoutHorizontal
 * Layout the panel horizontally
 *******************************************************************/
void ArchiveManagerPanel::layoutHorizontal()
{
	// Layout archives tab horizontally
	wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);
	panel_am->SetSizer(hbox);

	hbox->Add(panel_archives, 1, wxEXPAND|wxALL, 4);
	hbox->Add(panel_rf, 1, wxEXPAND|wxTOP|wxRIGHT|wxBOTTOM, 4);
}

/* ArchiveManagerPanel::refreshRecentFileList
 * Clears and rebuilds the recent file list in the menu and the tab
 *******************************************************************/
void ArchiveManagerPanel::refreshRecentFileList()
{
	// Clear the list
	list_recent->ClearAll();

	// Get first recent file menu id
	SAction* a_recent = SAction::fromId("aman_recent");
	int id_recent_start = a_recent->getWxId();

	// Clear menu; needs to do with a count down rather than up
	// otherwise the following elements are not properly removed
	for (unsigned a = menu_recent->GetMenuItemCount(); a > 0; a--)
		menu_recent->Destroy(id_recent_start + a - 1);

	// Add columns
	list_recent->InsertColumn(0, "Filename");
	list_recent->InsertColumn(1, "Path");

	// Add each recent archive (same logic as the recent files submenu)
	list_recent->enableSizeUpdate(false);
	for (unsigned a = 0; a < App::archiveManager().numRecentFiles(); a++)
	{
		list_recent->addItem(a, wxEmptyString);
		updateRecentListItem(a);

		if (a < 8)
		{
			// Get path and determine icon
			string fn = App::archiveManager().recentFile(a);
			string icon = "archive";
			if (fn.EndsWith(".wad"))
				icon = "wad";
			else if (fn.EndsWith(".zip") || fn.EndsWith(".pk3") || fn.EndsWith(".pke"))
				icon = "zip";
			else if (wxDirExists(fn))
				icon = "folder";

			// Create and add menu item
			a_recent->addToMenu(menu_recent, fn, icon, a);
			//wxMenuItem* mi = new wxMenuItem(menu_recent, id_recent_start + a, fn);
			//mi->SetBitmap(Icons::getIcon(Icons::ENTRY, icon));
			//menu_recent->Append(mi);
		}
	}

	// Update size
	list_recent->enableSizeUpdate(true);
	list_recent->updateSize();
}

/* ArchiveManagerPanel::disableArchiveListUpdate
* disables updating the archive list 4 speed reasons
*******************************************************************/
void ArchiveManagerPanel::disableArchiveListUpdate()
{
	list_archives->enableSizeUpdate(false);
}

/* ArchiveManagerPanel::refreshArchiveList
 * Clears and rebuilds the open archives list
 *******************************************************************/
void ArchiveManagerPanel::refreshArchiveList()
{
	// Clear the list
	list_archives->ClearAll();

	// Add columns
	list_archives->InsertColumn(0, "Filename");
	list_archives->InsertColumn(1, "Path");

	// Add each archive that is opened in the ArchiveManager
	list_archives->enableSizeUpdate(false);
	for (int a = 0; a < App::archiveManager().numArchives(); a++)
	{
		list_archives->addItem(a, wxEmptyString);
		updateOpenListItem(a);
	}

	// Update size
	list_archives->enableSizeUpdate(true);
	list_archives->updateSize();
}

/* ArchiveManagerPanel::refreshAllTabs
 * Refreshes all open archive tabs
 *******************************************************************/
void ArchiveManagerPanel::refreshAllTabs()
{
	// Go through tabs
	for (unsigned a = 0; a < stc_archives->GetPageCount(); a++)
	{
		wxWindow* tab = stc_archives->GetPage(a);

		// Refresh if it's an archive panel
		if (isArchivePanel(a))
			((ArchivePanel*)tab)->refreshPanel();
	}
}

/* ArchiveManagerPanel::updateOpenListItem
 * Updates the archive list item at <index>
 *******************************************************************/
void ArchiveManagerPanel::updateOpenListItem(int index)
{
	Archive* archive = App::archiveManager().getArchive(index);

	if (!archive)
		return;

	// Get path as wxFileName for processing
	wxFileName fn(archive->filename());

	// Set item name
	
	list_archives->setItemText(index, 0, fn.GetFullName());
	list_archives->setItemText(index, 1, fn.GetPath());

	// Set item status colour
	if (archive->canSave())
	{
		if (archive->isModified())
			list_archives->setItemStatus(index, LV_STATUS_MODIFIED);
		else
			list_archives->setItemStatus(index, LV_STATUS_NORMAL);
	}
	else
		list_archives->setItemStatus(index, LV_STATUS_NEW);
}

/* ArchiveManagerPanel::updateRecentListItem
 * Updates the recent file list item at <index>
 *******************************************************************/
void ArchiveManagerPanel::updateRecentListItem(int index)
{
	// Get path as wxFileName for processing
	string path = App::archiveManager().recentFile(index);
	wxFileName fn(path);

	// Set item name
	list_recent->setItemText(index, 0, fn.GetFullName());
	list_recent->setItemText(index, 1, fn.GetPath());

	// Set item icon
	int icon = 0;
	if (path.EndsWith(".wad"))
		icon = 1;
	else if (path.EndsWith(".zip") || path.EndsWith(".pk3") || path.EndsWith(".pke"))
		icon = 2;
	else if (wxDirExists(path))
		icon = 3;
	list_recent->SetItemImage(index, icon);
}

/* ArchiveManagerPanel::updateArchiveTabTitle
 * Updates the title of the tab for the archive at <index>
 *******************************************************************/
void ArchiveManagerPanel::updateArchiveTabTitle(int index)
{
	Archive* archive = App::archiveManager().getArchive(index);

	if (!archive)
		return;

	// Go through all tabs
	for (size_t a = 0; a < stc_archives->GetPageCount(); a++)
	{
		// Check page type is "archive"
		if (stc_archives->GetPage(a)->GetName().CmpNoCase("archive"))
			continue;

		// Check for archive match
		ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(a);
		if (ap->getArchive() == archive)
		{
			string title;
			if (archive->isModified())
				title = S_FMT("%s *", archive->filename(false));
			else
				title = archive->filename(false);
			stc_archives->SetPageText(a, title);
			return;
		}
	}
}

/* ArchiveManagerPanel::isArchivePanel
 * Checks if the tab at [tab_index] is an ArchivePanel. Returns true
 * if it is, false if not
 *******************************************************************/
bool ArchiveManagerPanel::isArchivePanel(int tab_index)
{
	// Check that tab index is in range
	if ((unsigned)tab_index >= stc_archives->GetPageCount())
		return false;

	// Check the page's name
	if (!stc_archives->GetPage(tab_index)->GetName().CmpNoCase("archive"))
		return true;
	else
		return false;
}

/* ArchiveManagerPanel::isEntryPanel
 * Checks if the tab at [tab_index] is an EntryPanel. Returns true
 * if it is, false if not
 *******************************************************************/
bool ArchiveManagerPanel::isEntryPanel(int tab_index)
{
	// Check that tab index is in range
	if ((unsigned)tab_index >= stc_archives->GetPageCount())
		return false;

	// Check the page's name
	if (!stc_archives->GetPage(tab_index)->GetName().CmpNoCase("entry"))
		return true;
	else
		return false;
}

/* ArchiveManagerPanel::getArchive
 * Returns the archive associated with the archive tab at [tab_index]
 * or NULL if the index is invalid or the tab isn't an archive panel
 *******************************************************************/
Archive* ArchiveManagerPanel::getArchive(int tab_index)
{
	// Check the index is valid
	if (tab_index < 0 || (unsigned)tab_index >= stc_archives->GetPageCount())
		return NULL;

	// Check the specified tab is actually an archive tab
	if (!isArchivePanel(tab_index))
		return NULL;

	// Get the archive associated with the tab
	ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(tab_index);
	return ap->getArchive();
}

/* ArchiveManagerPanel::currentTabIndex
 * Returns the index of the currently open tab
 *******************************************************************/
int ArchiveManagerPanel::currentTabIndex()
{
	return stc_archives->GetSelection();
}

/* ArchiveManagerPanel::currentArchive
 * Returns the currently 'open' archive - the archive associated
 * with the current ArchivePanel tab. Returns NULL if the current tab
 * isn't an ArchivePanel
 *******************************************************************/
Archive* ArchiveManagerPanel::currentArchive()
{
	// Get current tab
	wxWindow* page = stc_archives->GetPage(stc_archives->GetSelection());

	// Return if no tabs exist
	if (!page)
		return NULL;

	// ArchivePanel
	if (page->GetName() == "archive")
	{
		ArchivePanel* ap = (ArchivePanel*)page;
		return ap->getArchive();
	}

	// EntryPanel
	else if (page->GetName() == "entry")
	{
		EntryPanel* ep = (EntryPanel*)page;
		return ep->getEntry()->getParent();
	}

	// TextureXEditor
	else if (page->GetName() == "texture")
	{
		TextureXEditor* tx = (TextureXEditor*)page;
		return tx->getArchive();
	}

	// Not an archive-related tab
	else
		return NULL;
}

/* ArchiveManagerPanel::currentPanel
 * Returns the current panel tab
 *******************************************************************/
wxWindow* ArchiveManagerPanel::currentPanel()
{
	return stc_archives->GetPage(stc_archives->GetSelection());
}

/* ArchiveManagerPanel::currentArea
 * Returns the current panel tab
 *******************************************************************/
EntryPanel* ArchiveManagerPanel::currentArea()
{
	// Get current tab index
	int selected = stc_archives->GetSelection();

	// Entry tab
	if (isEntryPanel(selected))
		return (EntryPanel*)stc_archives->GetPage(selected);

	// Archive tab
	if (isArchivePanel(selected))
	{
		ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(selected);
		return ap->currentArea();
	}

	// No currently active entry panel
	return NULL;
}

/* ArchiveManagerPanel::currentEntry
 * Returns the currently open entry in the current archive panel
 * (if any)
 *******************************************************************/
ArchiveEntry* ArchiveManagerPanel::currentEntry()
{
	// Get current tab index
	int selected = stc_archives->GetSelection();

	// Check it's an archive tab
	if (!isArchivePanel(selected))
		return NULL;

	// Get the archive panel
	ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(selected);
	return ap->currentEntry();
}

/* ArchiveManagerPanel::currentEntrySelection
 * Returns a list of all selected entries in the current archive
 * panel
 *******************************************************************/
vector<ArchiveEntry*> ArchiveManagerPanel::currentEntrySelection()
{
	// Get current tab index
	int selected = stc_archives->GetSelection();

	// Check it's an archive tab
	if (!isArchivePanel(selected))
		return vector<ArchiveEntry*>();

	// Get the archive panel
	ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(selected);
	return ap->currentEntries();
}

/* ArchiveManagerPanel::openTab
 * Opens a new tab for the archive at <archive_index> in the archive
 * manager
 *******************************************************************/
void ArchiveManagerPanel::openTab(int archive_index)
{
	Archive* archive = App::archiveManager().getArchive(archive_index);
	if (archive)
	{
		openTab(archive);
	}
}

/* ArchiveManagerPanel::getArchiveTab
 * Returns the ArchivePanel for [archive], or NULL if none is open
 *******************************************************************/
ArchivePanel* ArchiveManagerPanel::getArchiveTab(Archive* archive)
{
	// Check archive was given
	if (!archive)
		return NULL;

	// Go through all tabs
	for (size_t a = 0; a < stc_archives->GetPageCount(); a++)
	{
		// Check page type is "archive"
		if (stc_archives->GetPage(a)->GetName().CmpNoCase("archive"))
			continue;

		// Check for archive match
		ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(a);
		if (ap->getArchive() == archive)
			return ap;
	}

	// No tab currently open for archive
	return NULL;
}

/* ArchiveManagerPanel::openTab
 * Opens a new tab for the archive
 *******************************************************************/
void ArchiveManagerPanel::openTab(Archive* archive)
{
	if (archive)
	{
		// Check if the archive is already open in a tab
		ArchivePanel* wp = getArchiveTab(archive);
		if (wp)
		{
			// Switch to tab
			stc_archives->SetSelection(stc_archives->GetPageIndex(wp));
			wp->focusEntryList();

			return;
		}

		// If tab isn't already open, open a new one
		wp = new ArchivePanel(stc_archives, archive);

		// Determine icon
		string icon = "archive";
		if (archive->formatId() == "wad")
			icon = "wad";
		else if (archive->formatId() == "zip")
			icon = "zip";
		else if (archive->formatId() == "folder")
			icon = "folder";

		wp->SetName("archive");
		stc_archives->AddPage(wp, archive->filename(false), false);
		stc_archives->SetSelection(stc_archives->GetPageCount() - 1);
		stc_archives->SetPageBitmap(stc_archives->GetPageCount() - 1, Icons::getIcon(Icons::ENTRY, icon));
		wp->addMenus();
		wp->Show(true);
		wp->SetFocus();
		wp->focusEntryList();
	}
}

/* ArchiveManagerPanel::closeTab
 * Closes the archive editor tab for the archive at <archive_index>
 * in the archive manager
 *******************************************************************/
void ArchiveManagerPanel::closeTab(int archive_index)
{
	Archive* archive = App::archiveManager().getArchive(archive_index);
	ArchivePanel* ap = getArchiveTab(archive);

	if (ap)
		stc_archives->DeletePage(stc_archives->GetPageIndex(ap));
}

/* ArchiveManagerPanel::openTextureTab
 * Opens a new texture editor tab for the archive at <archive_index>
 * in the archive manager
 *******************************************************************/
void ArchiveManagerPanel::openTextureTab(int archive_index, ArchiveEntry* entry)
{
	Archive* archive = App::archiveManager().getArchive(archive_index);

	if (archive)
	{
		// Go through all tabs
		for (size_t a = 0; a < stc_archives->GetPageCount(); a++)
		{
			// Check page type is "texture"
			if (stc_archives->GetPage(a)->GetName().CmpNoCase("texture"))
				continue;

			// Check for archive match
			TextureXEditor* txed = (TextureXEditor*)stc_archives->GetPage(a);
			if (txed->getArchive() == archive)
			{
				// Selected archive already has its texture editor open, so show that tab
				stc_archives->SetSelection(a);
				txed->setSelection(entry);
				return;
			}
		}

		// If tab isn't already open, open a new one
		TextureXEditor* txed = new TextureXEditor(stc_archives);
		txed->Show(false);
		if (!txed->openArchive(archive))
		{
			delete txed;
			return;
		}

		stc_archives->AddPage(txed, S_FMT("Texture Editor (%s)", archive->filename(false)), true);
		stc_archives->SetPageBitmap(stc_archives->GetPageCount() - 1, Icons::getIcon(Icons::ENTRY, "texturex"));
		txed->SetName("texture");
		txed->setSelection(entry);
		txed->Show(true);
		// Select the new tab
		for (size_t a = 0; a < stc_archives->GetPageCount(); a++)
		{
			if (stc_archives->GetPage(a) == txed)
			{
				stc_archives->SetSelection(a);
				return;
			}
		}
	}
}

/* ArchiveManagerPanel::getTextureTab
 * Returns the TextureXEditor for the archive at [archive_index],
 * or NULL if none is open for that archive
 *******************************************************************/
TextureXEditor* ArchiveManagerPanel::getTextureTab(int archive_index)
{
	Archive* archive = App::archiveManager().getArchive(archive_index);

	if (archive)
	{
		// Go through all tabs
		for (size_t a = 0; a < stc_archives->GetPageCount(); a++)
		{
			// Check page type is "texture"
			if (stc_archives->GetPage(a)->GetName().CmpNoCase("texture"))
				continue;

			// Check for archive match
			TextureXEditor* txed = (TextureXEditor*)stc_archives->GetPage(a);
			if (txed->getArchive() == archive)
				return txed;
		}
	}

	// No texture editor open for that archive
	return NULL;
}

/* ArchiveManagerPanel::closeTextureTab
 * Closes the texture editor tab for the archive at <archive_index>
 * in the archive manager
 *******************************************************************/
void ArchiveManagerPanel::closeTextureTab(int archive_index)
{
	TextureXEditor* txed = getTextureTab(archive_index);
	if (txed) stc_archives->DeletePage(stc_archives->GetPageIndex(txed));
}

/* ArchiveManagerPanel::redirectToTab
 * Redirects to the separated tab with given entry if exists
 *******************************************************************/
bool ArchiveManagerPanel::redirectToTab(ArchiveEntry *entry)
{
	for (unsigned a = 0; a < stc_archives->GetPageCount(); a++)
	{
		// Check page type is "entry"
		if (stc_archives->GetPage(a)->GetName() != "entry")
			continue;

		// Check for entry match
		EntryPanel* ep = (EntryPanel*)stc_archives->GetPage(a);
		if (ep->getEntry() == entry)
		{
			// Already open, switch to tab
			stc_archives->SetSelection(a);
			return true;
		}
	}
	return false;
}

/* ArchiveManagerPanel::openEntryTab
 * Opens the appropriate EntryPanel for [entry] in a new tab
 *******************************************************************/
void ArchiveManagerPanel::openEntryTab(ArchiveEntry* entry)
{
	// Close the same entry in archive tab
	ArchivePanel *panel = getArchiveTab(entry->getParent());
	panel->closeCurrentEntry();

	// First check if the entry is already open in a tab
	redirectToTab(entry);

	// Create an EntryPanel for the entry
	EntryPanel* ep = ArchivePanel::createPanelForEntry(entry, stc_archives);
	ep->openEntry(entry);

	// Don't bother with the default entry panel
	// (it's absolutely useless to open in a tab)
	if (ep->getName() == "default")
	{
		delete ep;
		return;
	}

	// Create new tab for the EntryPanel
	stc_archives->AddPage(ep, S_FMT("%s/%s", entry->getParent()->filename(false), entry->getName()), true);
	stc_archives->SetPageBitmap(stc_archives->GetPageCount() - 1, Icons::getIcon(Icons::ENTRY, entry->getType()->getIcon()));
	ep->SetName("entry");
	ep->Show(true);
	ep->addCustomMenu();
	ep->updateToolbar();

	// Select the new tab
	for (size_t a = 0; a < stc_archives->GetPageCount(); a++)
	{
		if (stc_archives->GetPage(a) == ep)
		{
			stc_archives->SetSelection(a);
			return;
		}
	}
}

/* ArchiveManagerPanel::closeEntryTab
 * Closes the EntryPanel tab for [entry]
 *******************************************************************/
void ArchiveManagerPanel::closeEntryTabs(Archive* parent)
{
	// Check archive was given
	if (!parent)
		return;

	// Go through tabs
	for (unsigned a = 0; a < stc_archives->GetPageCount(); a++)
	{
		// Check page type is "entry"
		if (stc_archives->GetPage(a)->GetName() != "entry")
			continue;

		// Check for entry parent archive match
		EntryPanel* ep = (EntryPanel*)stc_archives->GetPage(a);
		if (ep->getEntry()->getParent() == parent)
		{
			// Close tab
			ep->removeCustomMenu();
			stc_archives->DeletePage(a);
			a--;
		}
	}
}

/* ArchiveManagerPanel::openFile
 * Opens an archive and initialises the UI for it
 *******************************************************************/
void ArchiveManagerPanel::openFile(string filename)
{
	// Show splash screen
	UI::showSplash("Opening Archive...", true);

	// test
	wxStopWatch sw;
	sw.Start();

	// Open the file in the archive manager
	Archive* new_archive = App::archiveManager().openArchive(filename);

	sw.Pause();
	LOG_MESSAGE(1, "Opening took %d ms", (int)sw.Time());

	// Hide splash screen
	UI::hideSplash();

	// Check that the archive opened ok
	if (!new_archive)
	{
		// If archive didn't open ok, show error message
		wxMessageBox(S_FMT("Error opening %s:\n%s", filename, Global::error), "Error", wxICON_ERROR);
	}
}

/* ArchiveManagerPanel::openFiles
 * Opens each file in a supplied array of filenames
 *******************************************************************/
void ArchiveManagerPanel::openFiles(wxArrayString& files)
{
	// Go through each filename in the array
	for (int a = 0; a < (int) files.size(); a++)
	{
		// Open the archive
		openFile(files[a]);
	}
}

/* ArchiveManagerPanel::openDirAsArchive
* Opens a directory as an archive and initialises the UI for it
*******************************************************************/
void ArchiveManagerPanel::openDirAsArchive(string dir)
{
	// Show splash screen
	UI::showSplash("Opening Directory...", true);

	// test
	wxStopWatch sw;
	sw.Start();

	// Open the file in the archive manager
	Archive* new_archive = App::archiveManager().openDirArchive(dir);

	sw.Pause();
	LOG_MESSAGE(1, "Opening took %d ms", (int)sw.Time());

	// Hide splash screen
	UI::hideSplash();

	// Check that the archive opened ok
	if (!new_archive)
	{
		// If archive didn't open ok, show error message
		wxMessageBox(S_FMT("Error opening directory %s:\n%s", dir, Global::error), "Error", wxICON_ERROR);
	}
}

/* ArchiveManagerPanel::undo
 * Performs an undo operation on the currently selected tab, returns
 * true if the tab supports undo, false otherwise
 *******************************************************************/
bool ArchiveManagerPanel::undo()
{
	// Get current tab page
	wxWindow* page_current = currentPanel();

	// Archive panel
	if (S_CMPNOCASE(page_current->GetName(), "archive"))
	{
		((ArchivePanel*)page_current)->undo();
		return true;
	}

	// TEXTUREx panel
	else if (S_CMPNOCASE(page_current->GetName(), "texture"))
	{
		((TextureXEditor*)page_current)->undo();
		return true;
	}

	return false;
}

/* ArchiveManagerPanel::redo
 * Performs an redo operation on the currently selected tab, returns
 * true if the tab supports redo, false otherwise
 *******************************************************************/
bool ArchiveManagerPanel::redo()
{
	// Get current tab page
	wxWindow* page_current = currentPanel();

	// Archive panel
	if (S_CMPNOCASE(page_current->GetName(), "archive"))
	{
		((ArchivePanel*)page_current)->redo();
		return true;
	}

	// TEXTUREx panel
	else if (S_CMPNOCASE(page_current->GetName(), "texture"))
	{
		((TextureXEditor*)page_current)->redo();
		return true;
	}

	return false;
}

/* ArchiveManagerPanel::closeAll
 * Closes all currently open archives
 *******************************************************************/
bool ArchiveManagerPanel::closeAll()
{
	asked_save_unchanged = false;

	while (App::archiveManager().numArchives() > 0)
	{
		if (!closeArchive(App::archiveManager().getArchive(0)))
			return false;
	}

	return true;
}

/* ArchiveManagerPanel::saveAll
 * Saves all currently open archives
 *******************************************************************/
void ArchiveManagerPanel::saveAll()
{
	// Go through all open archives
	for (int a = 0; a < App::archiveManager().numArchives(); a++)
	{
		// Get the archive to be saved
		Archive* archive = App::archiveManager().getArchive(a);

		if (archive->canSave())
		{
			// Save the archive if possible
			if (!archive->save())
			{
				// If there was an error pop up a message box
				wxMessageBox(S_FMT("Error: %s", Global::error), "Error", wxICON_ERROR);
			}
		}
		else
		{
			// If the archive is newly created, do Save As instead

			// Popup file save dialog
			string formats = archive->fileExtensionString();
			string filename = wxFileSelector("Save Archive " + archive->filename(false) + " As", dir_last, "", wxEmptyString, formats, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

			// Check a filename was selected
			if (!filename.empty())
			{
				// Save the archive
				if (!archive->save(filename))
				{
					// If there was an error pop up a message box
					wxMessageBox(S_FMT("Error: %s", Global::error), "Error", wxICON_ERROR);
				}

				// Save 'dir_last'
				wxFileName fn(filename);
				dir_last = fn.GetPath(true);
			}
		}
	}
}

/* ArchiveManagerPanel::checkDirArchives
 * Checks all open directory archives for changes on the file system
 * in background threads
 *******************************************************************/
void ArchiveManagerPanel::checkDirArchives()
{
	if (checked_dir_archive_changes || !check_dir_archives)
		return;

	for (int a = 0; a < App::archiveManager().numArchives(); a++)
	{
		Archive* archive = App::archiveManager().getArchive(a);
		if (archive->formatId() != "folder")
			continue;

		if (VECTOR_EXISTS(checking_archives, archive))
			continue;

		LOG_MESSAGE(2, "Checking %s for external changes...", CHR(archive->filename()));
		checking_archives.push_back(archive);
		DirArchiveCheck* check = new DirArchiveCheck(this, (DirArchive*)archive);
		check->Create();
		check->Run();
	}
}

/* ArchiveManagerPanel::createNewArchive
 * Creates a new archive of the given type and opens it in a tab
 *******************************************************************/
void ArchiveManagerPanel::createNewArchive(string format)
{
	Archive* new_archive = App::archiveManager().newArchive(format);

	if (new_archive)
		openTab(App::archiveManager().archiveIndex(new_archive));
}

/* ArchiveManagerPanel::saveEntryChanges
 * If there are any unsaved entry changes in [archive]'s ArchivePanel
 * tab, saves the changes (or not, depending on user settings)
 *******************************************************************/
bool ArchiveManagerPanel::saveEntryChanges(Archive* archive)
{
	// Go through tabs
	for (size_t a = 0; a < stc_archives->GetPageCount(); a++)
	{
		// Check page type is "archive"
		if (stc_archives->GetPage(a)->GetName().CmpNoCase("archive"))
			continue;

		// Check for archive match
		ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(a);
		if (ap->getArchive() == archive)
		{
			// Save entry changes
			return ap->saveEntryChanges();
		}
	}

	return false;
}

/* ArchiveManagerPanel::saveArchive
 * Saves [archive] to disk, opens a file dialog if necessary
 *******************************************************************/
bool ArchiveManagerPanel::saveArchive(Archive* archive)
{
	// Check for null pointer
	if (!archive)
		return false;

	// Check for unsaved entry changes
	saveEntryChanges(archive);

	if (archive->canSave())
	{
		// Save the archive if possible
		if (!archive->save())
		{
			// If there was an error pop up a message box
			wxMessageBox(S_FMT("Error: %s", Global::error), "Error", wxICON_ERROR);
			return false;
		}

		return true;
	}
	else
		return saveArchiveAs(archive);	// If the archive is newly created, do Save As instead
}

/* ArchiveManagerPanel::saveArchive
 * Saves [archive] to disk under a different filename, opens
 * a file dialog to select the new name/path
 *******************************************************************/
bool ArchiveManagerPanel::saveArchiveAs(Archive* archive)
{
	// Check for null pointer
	if (!archive)
		return false;

	// Check archive type
	if (archive->formatId() == "folder")
		return true;	// Can't do save as for folder

	// Check for unsaved entry changes
	saveEntryChanges(archive);

	// Popup file save dialog
	string formats = archive->fileExtensionString();
	string filename = wxFileSelector("Save Archive " + archive->filename(false) + " As", dir_last, "", wxEmptyString, formats, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	// Check a filename was selected
	if (!filename.empty())
	{
		// Save the archive
		if (!archive->save(filename))
		{
			// If there was an error pop up a message box
			wxMessageBox(S_FMT("Error: %s", Global::error), "Error", wxICON_ERROR);
			return false;
		}

		// Save 'dir_last'
		wxFileName fn(filename);
		dir_last = fn.GetPath(true);

		// Add recent file
		App::archiveManager().addRecentFile(filename);
	}
	else
		return false;

	return true;
}

/* ArchiveManagerPanel::beforeCloseArchive
 * Checks for any unsaved changes and prompts the user to save if
 * needed, but doesn't actually close the archive
 *******************************************************************/
bool ArchiveManagerPanel::beforeCloseArchive(Archive* archive)
{
	// Check for NULL pointers -- this can happen, for example,
	// with onArchiveTabClose() when closing a texture editor tab.
	if (!archive)
		return false;

	// Check if a map from the archive is currently open
	if (MapEditor::window()->IsShown() && MapEditor::window()->hasMapOpen(archive))
	{
		if (!MapEditor::window()->Close())
			return false;
	}

	// Check for unsaved entry changes
	saveEntryChanges(archive);

	// Check for unsaved texture editor changes
	int archive_index = App::archiveManager().archiveIndex(archive);
	TextureXEditor* txed = getTextureTab(archive_index);
	if (txed)
	{
		openTextureTab(archive_index);
		if (!txed->close())
			return false;	// User cancelled saving texturex changes, don't close
	}

	// If the archive has unsaved changes, prompt to save
	if (archive->isModified() && archive->isWritable())
	{
		asked_save_unchanged = true;
		wxMessageDialog md(this, S_FMT("Save changes to archive %s?", archive->filename(false)), "Unsaved Changes", wxYES_NO|wxCANCEL);
		int result = md.ShowModal();
		if (result == wxID_YES)
		{
			// User selected to save
			if (!saveArchive(archive))
				return false;	// Unsuccessful save, don't close
		}
		else if (result == wxID_CANCEL)
			return false;	// User selected cancel, don't close the archive
	}

	return true;
}

/* ArchiveManagerPanel::closeArchive
 * Checks for any unsaved changes and prompts the user to save if
 * needed before closing [archive]
 *******************************************************************/
bool ArchiveManagerPanel::closeArchive(Archive* archive)
{
	if (!archive)
		return false;

	checked_dir_archive_changes = true;
	bool ok = beforeCloseArchive(archive) && App::archiveManager().closeArchive(archive);
	checked_dir_archive_changes = false;

	return ok;
}

/* ArchiveManagerPanel::getSelectedArchives
 * Gets a list of indices of all selected archive list items
 *******************************************************************/
vector<int> ArchiveManagerPanel::getSelectedArchives()
{
	vector<int> ret;

	// Go through all wad list items
	long item = -1;
	while (true)
	{
		// Get the next item in the list that is selected
		item = list_archives->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		// If -1 then none were selected
		if (item == -1)
			break;

		// Otherwise add the selected item to the vector
		ret.push_back(item);
	}

	return ret;
}

/* ArchiveManagerPanel::getSelectedFiles
 * Gets a list of indices of all selected recent file list items
 *******************************************************************/
vector<int> ArchiveManagerPanel::getSelectedFiles()
{
	vector<int> ret;

	// Go through all wad list items
	long item = -1;
	while (true)
	{
		// Get the next item in the list that is selected
		item = list_recent->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		// If -1 then none were selected
		if (item == -1)
			break;

		// Otherwise add the selected item to the vector
		ret.push_back(item);
	}

	return ret;
}

/* ArchiveManagerPanel::getSelectedBookmarks
 * Gets a list of indices of all selected bookmark list items
 *******************************************************************/
vector<int> ArchiveManagerPanel::getSelectedBookmarks()
{
	vector<int> ret;

	// Go through all wad list items
	long item = -1;
	while (true)
	{
		// Get the next item in the list that is selected
		item = list_bookmarks->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		// If -1 then none were selected
		if (item == -1)
			break;

		// Otherwise add the selected item to the vector
		ret.push_back(item);
	}

	return ret;
}

/* ArchiveManagerPanel::onAnnouncement
 * Called when an announcement is recieved from the Archive Manager
 *******************************************************************/
void ArchiveManagerPanel::onAnnouncement(Announcer* announcer, string event_name, MemChunk& event_data)
{
	// Reset event data for reading
	event_data.seek(0, SEEK_SET);

	// If an archive is about to be closed
	if (event_name == "archive_closing")
	{
		int32_t index = -1;
		event_data.read(&index, 4);

		// Close any related tabs
		closeTextureTab(index);
		closeEntryTabs(App::archiveManager().getArchive(index));
		closeTab(index);
	}

	// If an archive was closed
	if (event_name == "archive_closed")
	{
		int32_t index = -1;
		event_data.read(&index, 4);
		refreshArchiveList();
	}

	// If an archive was added
	if (event_name == "archive_added")
	{
		int index = App::archiveManager().numArchives() - 1;
		list_archives->addItem(index, wxEmptyString);
		updateOpenListItem(index);
	}

	// If an archive was opened
	if (event_name == "archive_opened")
	{
		uint32_t index = -1;
		event_data.read(&index, 4);
		openTab(index);
	}

	// If an archive was saved
	if (event_name == "archive_saved")
	{
		int32_t index = -1;
		event_data.read(&index, 4);
		updateOpenListItem(index);
		updateArchiveTabTitle(index);
	}

	// If an archive was modified
	if (event_name == "archive_modified")
	{
		int32_t index = -1;
		event_data.read(&index, 4);
		updateOpenListItem(index);
		updateArchiveTabTitle(index);
	}

	// If a texture editor is to be opened
	if (event_name == "open_tex_editor")
	{
		uint32_t index = 0;
		event_data.read(&index, 4);
		openTextureTab(index);
	}

	// If the recent files list has changed
	if (event_name == "recent_files_changed")
	{
		refreshRecentFileList();
	}

	// If the bookmarks list has changed
	if (event_name == "bookmarks_changed")
	{
		refreshBookmarkList();
	}
}

/* ArchiveManagerPanel::saveSelection
 * Saves the currently selected archive(s) in the list
 *******************************************************************/
void ArchiveManagerPanel::saveSelection()
{
	// Get the list of selected archives
	vector<int> selection = getSelectedArchives();

	// Don't continue if there are no selected items
	if (selection.size() == 0)
		return;

	// Go through the selection
	for (size_t a = 0; a < selection.size(); a++)
	{
		// Get the archive to be saved
		Archive* archive = App::archiveManager().getArchive(selection[a]);

		saveArchive(archive);
	}
}

/* ArchiveManagerPanel::saveSelectionAs
 * Saves the currently selected archive(s) in the list as new file(s)
 *******************************************************************/
void ArchiveManagerPanel::saveSelectionAs()
{
	// Get the list of selected archives
	vector<int> selection = getSelectedArchives();

	// Don't continue if there are no selected items
	if (selection.size() == 0)
		return;

	// Go through the selection
	for (size_t a = 0; a < selection.size(); a++)
	{
		// Get the archive
		Archive* archive = App::archiveManager().getArchive(selection[a]);
		saveArchiveAs(archive);
	}

	refreshArchiveList();
}

/* ArchiveManagerPanel::closeSelection
 * Closes the currently selected archive(s) in the list
 *******************************************************************/
bool ArchiveManagerPanel::closeSelection()
{
	// Get the list of selected list items
	vector<int> selection = getSelectedArchives();

	// Don't continue if there are no selected items
	if (selection.size() == 0)
		return true;

	// Get the list of selected archives
	vector<Archive*> selected_archives;
	for (size_t a = 0; a < selection.size(); a++)
		selected_archives.push_back(App::archiveManager().getArchive(selection[a]));

	// Close all selected archives, starting from the last
	bool all_closed = true;
	for (size_t a = selected_archives.size() - 1; (signed)a >= 0; --a)
	{
		if (!closeArchive(selected_archives[a]))
			all_closed = false;
	}

	return all_closed;
}

/* ArchiveManagerPanel::openSelection
 * Open the currently selected archive(s) in the recent file list
 *******************************************************************/
void ArchiveManagerPanel::openSelection()
{
	// Get the list of selected list items
	vector<int> selection = getSelectedFiles();

	// Don't continue if there are no selected items
	if (selection.size() == 0)
		return;

	// Get the list of selected archives
	vector<string> selected_archives;
	for (size_t a = 0; a < selection.size(); a++)
		selected_archives.push_back(App::archiveManager().recentFile(selection[a]));

	// Open all selected archives
	for (size_t a = 0; a < selected_archives.size(); a++)
		App::archiveManager().openArchive(selected_archives[a]);
}

/* ArchiveManagerPanel::removeSelection
 * Remove the currently selected archive(s) from the recent file list
 *******************************************************************/
void ArchiveManagerPanel::removeSelection()
{
	// Get the list of selected list items
	vector<int> selection = getSelectedFiles();

	// Don't continue if there are no selected items
	if (selection.size() == 0)
		return;

	// Remove selected recent files (starting from the last and going backward,
	// because the list reorders itself whenever an item is removed)
	for (unsigned a = selection.size(); a > 0; --a)
		App::archiveManager().removeRecentFile(App::archiveManager().recentFile(selection[a - 1]));
}

/* ArchiveManagerPanel::handleAction
 * Handles the action [id]. Returns true if the action was handled,
 * false otherwise
 *******************************************************************/
bool ArchiveManagerPanel::handleAction(string id)
{
	// We're only interested in "aman_" actions
	if (!id.StartsWith("aman_"))
		return false;

	// File->New Wad
	if (id == "aman_newwad")
		createNewArchive("wad");

	// File->New Zip
	else if (id == "aman_newzip")
		createNewArchive("zip");

	// File->New Map
	else if (id == "aman_newmap")
	{
		if (!MapEditor::chooseMap())
			MapEditor::window()->Show(false);
	}

	// File->Open
	else if (id == "aman_open")
	{
		// Create extensions string
		string extensions = App::archiveManager().getArchiveExtensionsString();

		// Open a file browser dialog that allows multiple selection
		// and filters by wad, zip and pk3 file extensions
		wxFileDialog dialog_open(this, "Choose file(s) to open", dir_last, wxEmptyString, extensions, wxFD_OPEN|wxFD_MULTIPLE|wxFD_FILE_MUST_EXIST, wxDefaultPosition);

		// Run the dialog & check that the user didn't cancel
		if (dialog_open.ShowModal() == wxID_OK)
		{
			wxBeginBusyCursor();

			// Get an array of selected filenames
			wxArrayString files;
			dialog_open.GetPaths(files);

			// Open them
			openFiles(files);

			wxEndBusyCursor();

			// Save 'dir_last'
			dir_last = dialog_open.GetDirectory();
		}
	}

	// File->Open Directory
	else if (id == "aman_opendir")
	{
		// Open a directory browser dialog
		wxDirDialog dialog_open(this, "Select a Directory to open", dir_last, wxDD_DIR_MUST_EXIST | wxDD_NEW_DIR_BUTTON);

		// Run the dialog & check the user didn't cancel
		if (dialog_open.ShowModal() == wxID_OK)
		{
			wxBeginBusyCursor();

			// Open directory
			openDirAsArchive(dialog_open.GetPath());

			wxEndBusyCursor();

			// Save 'dir_last'
			dir_last = dialog_open.GetPath();
		}
	}

	// File->Recent
	else if (id == "aman_recent")
	{
		// Get recent file index
		unsigned index = wx_id_offset;//SAction::fromId(id)->getWxId() - SAction::fromId("aman_recent1")->getWxId();

		// Open it
		openFile(App::archiveManager().recentFile(index));
	}

	// File->Save
	else if (id == "aman_save")
		saveArchive(currentArchive());

	// File->Save As
	else if (id == "aman_saveas")
		saveArchiveAs(currentArchive());

	// File->Save All
	else if (id == "aman_saveall")
		saveAll();

	// File->Close All
	else if (id == "aman_closeall")
		closeAll();

	// File->Close
	else if (id == "aman_close")
		closeArchive(currentArchive());

	// Archives context menu cannot needs its own functions!
	else if (id == "aman_save_a")
		saveSelection();
	else if (id == "aman_saveas_a")
		saveSelectionAs();
	else if (id == "aman_close_a")
		closeSelection();

	// Recent files context menu
	else if (id == "aman_recent_open")
		openSelection();
	else if (id == "aman_recent_remove")
		removeSelection();

	// Bookmarks context menu
	else if (id == "aman_bookmark_go")
		goToBookmark();
	else if (id == "aman_bookmark_remove")
		deleteSelectedBookmarks();


	// Unknown action
	else
		return false;

	// Action performed, return true
	return true;
}

/* ArchiveManagerPanel::updateBookmarkListItem
 * Updates the bookmark list item at <index>
 *******************************************************************/
void ArchiveManagerPanel::updateBookmarkListItem(int index)
{
	// Only valid indices
	if (index < 0 || (unsigned)index >= App::archiveManager().numBookmarks())
		return;

	ArchiveEntry* entry = App::archiveManager().getBookmark(index);
	if (!entry)
		return;

	// Set item name
	list_bookmarks->setItemText(index, 0, entry->getName());
	list_bookmarks->setItemText(index, 1, entry->getParent()->filename());

	// Set item status colour
	if (entry->isLocked())
		list_bookmarks->setItemStatus(index, LV_STATUS_LOCKED);
	else switch (entry->getState())
		{
		case 0:	list_bookmarks->setItemStatus(index, LV_STATUS_NORMAL); break;
		case 1:	list_bookmarks->setItemStatus(index, LV_STATUS_MODIFIED); break;
		case 2:	list_bookmarks->setItemStatus(index, LV_STATUS_NEW); break;
		default:list_bookmarks->setItemStatus(index, LV_STATUS_ERROR); break;
		}
}

/* ArchiveManagerPanel::refreshBookmarkList
 * Clears and rebuilds the bookmark list
 *******************************************************************/
void ArchiveManagerPanel::refreshBookmarkList()
{
	// Clear the list
	list_bookmarks->ClearAll();

	// Add columns
	list_bookmarks->InsertColumn(0, "Entry");
	list_bookmarks->InsertColumn(1, "Archive");

	// Add each bookmark
	list_bookmarks->enableSizeUpdate(false);
	for (unsigned a = 0; a < App::archiveManager().numBookmarks(); a++)
	{
		list_bookmarks->addItem(a, wxEmptyString);
		updateBookmarkListItem(a);
	}

	// Update size
	list_bookmarks->enableSizeUpdate(true);
	list_bookmarks->updateSize();
}

/* ArchiveManagerPanel::deleteSelectedBookmarks
 * Deletes selected bookmarks from the list
 *******************************************************************/
void ArchiveManagerPanel::deleteSelectedBookmarks()
{
	vector<int> selection = getSelectedBookmarks();

	// Don't continue if there are no selected items
	if (selection.size() == 0)
		return;

	// Clear selection
	list_bookmarks->clearSelection();

	// Remove bookmarks
	for (int a = selection.size()-1; a >= 0; a--)
	{
		App::archiveManager().deleteBookmark(a);
	}
}

/* ArchiveManagerPanel::goToBookmark
 * Open the bookmark in the entry panel
 *******************************************************************/
void ArchiveManagerPanel::goToBookmark(long index)
{
	// Get the selected bookmark entry
	ArchiveEntry* bookmark = App::archiveManager().getBookmark(list_bookmarks->selectedItems()[0]);

	// Check it's valid
	if (!bookmark)
		return;

	// Open its parent archive in a tab
	openTab(bookmark->getParent());

	// Get the opened tab (should be an ArchivePanel unless something went wrong)
	wxWindow* tab = stc_archives->GetPage(stc_archives->GetSelection());

	// Check it's an archive panel
	if (!(S_CMP(tab->GetName(), "archive")))
		return;

	// Finally, open the entry
	((ArchivePanel*)tab)->openEntry(bookmark, true);
	if (bookmark->getType() != EntryType::folderType())
		((ArchivePanel*)tab)->focusOnEntry(bookmark);
}


/*******************************************************************
 * ARCHIVEMANAGERPANEL EVENTS
 *******************************************************************/

/* ArchiveManagerPanel::onListArchivesChanged
 * Called when the user selects an archive in the open files list.
 * Updates the maps list with any maps found within the selected
 * archive
 *******************************************************************/
void ArchiveManagerPanel::onListArchivesChanged(wxListEvent& e)
{
	// Get the selected archive
	Archive* selected_archive = App::archiveManager().getArchive(e.GetIndex());

	// Return if selection doesn't exist
	if (!selected_archive)
		return;

	current_maps = selected_archive;
}

/* ArchiveManagerPanel::onListArchivesActivated
 * Called when the user activates an archive in the list.
 * Opens the archive in a new tab, if it isn't already open.
 *******************************************************************/
void ArchiveManagerPanel::onListArchivesActivated(wxListEvent& e)
{
	// Open the archive tab, or create a new tab if it isn't already
	openTab(e.GetIndex());
}

/* ArchiveManagerPanel::onListArchivesRightClick
 * Called when the user right clicks an item on the archive list,
 * pops up a context menu
 *******************************************************************/
void ArchiveManagerPanel::onListArchivesRightClick(wxListEvent& e)
{
	// Generate context menu
	wxMenu context;
	SAction::fromId("aman_save_a")->addToMenu(&context, true);
	SAction::fromId("aman_saveas_a")->addToMenu(&context, true);
	SAction::fromId("aman_close_a")->addToMenu(&context, true);

	// Pop it up
	PopupMenu(&context);
}

/* ArchiveManagerPanel::onListRecentActivated
 * Called when the user activates an archive in the list.
 * Opens the archive in a new tab, if it isn't already open.
 *******************************************************************/
void ArchiveManagerPanel::onListRecentActivated(wxListEvent& e)
{
	// Open the archive
	openFile(App::archiveManager().recentFile(e.GetIndex()));
	// Refresh the list
	refreshRecentFileList();
}

/* ArchiveManagerPanel::onListRecentRightClick
 * Called when the user right clicks an item on the archive list,
 * pops up a context menu
 *******************************************************************/
void ArchiveManagerPanel::onListRecentRightClick(wxListEvent& e)
{
	// Generate context menu
	wxMenu context;
	SAction::fromId("aman_recent_open")->addToMenu(&context, true);
	SAction::fromId("aman_recent_remove")->addToMenu(&context, true);

	// Pop it up
	PopupMenu(&context);
}

/* ArchiveManagerPanel::onListBookmarksActivated
 * Called when the user activates an entry in the list.
 * Opens the entry and if needed its archive in a new tab, if it
 * isn't already open.
 *******************************************************************/
void ArchiveManagerPanel::onListBookmarksActivated(wxListEvent& e)
{
	// Open the archive
	goToBookmark(e.GetIndex());
}

/* ArchiveManagerPanel::onListBookmarksRightClick
 * Called when the user right clicks an item on the bookmark list,
 * pops up a context menu
 *******************************************************************/
void ArchiveManagerPanel::onListBookmarksRightClick(wxListEvent& e)
{
	// Generate context menu
	wxMenu context;
	SAction::fromId("aman_bookmark_go")->addToMenu(&context, true);
	SAction::fromId("aman_bookmark_remove")->addToMenu(&context, true);

	// Pop it up
	PopupMenu(&context);
}

/* ArchiveManagerPanel::onArchiveTabChanging
 * Called when the current archive tab is about to change
 *******************************************************************/
void ArchiveManagerPanel::onArchiveTabChanging(wxAuiNotebookEvent& e)
{
	e.Skip();
}

/* ArchiveManagerPanel::onArchiveTabChanged
 * Called when the current archive tab has changed
 *******************************************************************/
void ArchiveManagerPanel::onArchiveTabChanged(wxAuiNotebookEvent& e)
{
	// Page has changed, update custom menus and toolbars
	int selection = stc_archives->GetSelection();

	// Remove any current custom menus/toolbars
	theMainWindow->removeAllCustomMenus();
	theMainWindow->removeAllCustomToolBars();
	theMainWindow->enableToolBar("_archive", false);
	theMainWindow->enableToolBar("_entry", false);

	// ArchivePanel
	if (isArchivePanel(selection))
	{
		ArchivePanel* ap = (ArchivePanel*)stc_archives->GetPage(selection);
		ap->currentArea()->updateStatus();
		ap->addMenus();
	}

	// EntryPanel
	if (stc_archives->GetPage(selection)->GetName() == "entry")
	{
		EntryPanel* ep = (EntryPanel*)stc_archives->GetPage(selection);
		ep->addCustomMenu();
		ep->addCustomToolBar();
	}

	e.Skip();
}

/* ArchiveManagerPanel::onArchiveTabClose
 * Called when the user clicks the close button on an archive tab
 *******************************************************************/
void ArchiveManagerPanel::onArchiveTabClose(wxAuiNotebookEvent& e)
{
	// Get tab that is closing
	int tabindex = e.GetSelection();
	wxWindow* page = stc_archives->GetPage(tabindex);

	if (tabindex < 0)
		return;

	// Close the tab's archive if needed
	if (close_archive_with_tab && isArchivePanel(tabindex))
	{
		ArchivePanel* ap = (ArchivePanel*)page;
		Archive* archive = ap->getArchive();

		vector<Archive*> deps = App::archiveManager().getDependentArchives(archive);
		deps.insert(deps.begin(), archive);
		// Iterate in reverse order so the deepest-nested is closed first
		for (unsigned a = deps.size(); a > 0; a--)
		{
			if (!beforeCloseArchive(deps[a - 1]))
			{
				e.Veto();
				return;
			}
		}

		pending_closed_archive = archive;

		e.Skip();
		return;
	}

	// Check for texture editor
	if (page->GetName() == "texture")
	{
		TextureXEditor* txed = (TextureXEditor*)page;
		if (!txed->close())
			e.Veto();
	}

	e.Skip();
}

/* ArchiveManagerPanel::onArchiveTabClosed
 * Called after a tab has been closed by clicking on a close button
 *******************************************************************/
void ArchiveManagerPanel::onArchiveTabClosed(wxAuiNotebookEvent& e)
{
	// Actually close the archive the CLOSE event decided to close
	if (pending_closed_archive)
	{
		App::archiveManager().closeArchive(pending_closed_archive);
		pending_closed_archive = NULL;
	}

	e.Skip();
}

/* ArchiveManagerPanel::onAMTabChanged
 * Called when a different archive manager tab is selected
 *******************************************************************/
void ArchiveManagerPanel::onAMTabChanged(wxAuiNotebookEvent& e)
{
	am_current_tab = stc_tabs->GetSelection();
}

/* ArchiveManagerPanel::onDirArchiveCheckCompleted
 * Called when a directory archive check thread finishes work. Pops
 * up a dialog to apply any changes found (if any)
 *******************************************************************/
void ArchiveManagerPanel::onDirArchiveCheckCompleted(wxThreadEvent& e)
{
	dir_archive_changelist_t change_list = e.GetPayload<dir_archive_changelist_t>();

	// Check the archive is still open
	if (App::archiveManager().archiveIndex(change_list.archive) >= 0)
	{
		LOG_MESSAGE(2, "Finished checking %s for external changes", CHR(change_list.archive->filename()));

		if (!change_list.changes.empty())
		{
			checked_dir_archive_changes = true;

			// Show change/update dialog
			DirArchiveUpdateDialog dlg((wxWindow*)theMainWindow, (DirArchive*)change_list.archive, change_list.changes);
			dlg.ShowModal();
			
			checked_dir_archive_changes = false;
		}
		else
		{
			LOG_MESSAGE(2, "No changes");
		}
	}

	VECTOR_REMOVE(checking_archives, change_list.archive);
}
