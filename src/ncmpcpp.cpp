/***************************************************************************
 *   Copyright (C) 2008 by Andrzej Rybczak   *
 *   electricityispower@gmail.com   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "ncmpcpp.h"
#include "mpdpp.h"
#include "status_checker.h"
#include "helpers.h"
#include "settings.h"
#include "song.h"
#include "lyrics.h"

#define LOCK_STATUSBAR \
			if (Config.statusbar_visibility) \
				block_statusbar_update = 1; \
			else \
				block_progressbar_update = 1; \
			allow_statusbar_unblock = 0

#define UNLOCK_STATUSBAR \
			allow_statusbar_unblock = 1; \
			if (block_statusbar_update_delay < 0) \
			{ \
				if (Config.statusbar_visibility) \
					block_statusbar_update = 0; \
				else \
					block_progressbar_update = 0; \
			}

#define REFRESH_MEDIA_LIBRARY_SCREEN \
			mLibArtists->Display(redraw_me); \
			mvvline(main_start_y, lib_albums_start_x-1, 0, main_height); \
			mLibAlbums->Display(redraw_me); \
			mvvline(main_start_y, lib_songs_start_x-1, 0, main_height); \
			mLibSongs->Display(redraw_me)

#define REFRESH_PLAYLIST_EDITOR_SCREEN \
			mPlaylistList->Display(redraw_me); \
			mvvline(main_start_y, lib_albums_start_x-1, 0, main_height); \
			mPlaylistEditor->Display(redraw_me)

#define REFRESH_ALBUM_EDITOR_SCREEN \
			mEditorAlbums->Display(redraw_me); \
			mvvline(main_start_y, lib_albums_start_x-1, 0, main_height); \
			mEditorTagTypes->Display(redraw_me); \
			mvvline(main_start_y, lib_songs_start_x-1, 0, main_height); \
			mEditorTags->Display(redraw_me)

#ifdef HAVE_TAGLIB_H
 const string tag_screen = "Tag editor";
 const string tag_screen_keydesc = "Edit song's tags/playlist's name\n";
#else
 const string tag_screen = "Tag info";
 const string tag_screen_keydesc = "Show song's tags/edit playlist's name\n";
#endif

ncmpcpp_config Config;
ncmpcpp_keys Key;

SongList vSearched;
std::map<string, string, CaseInsensitiveComparison> vLibAlbums;
std::map<string, string, CaseInsensitiveComparison> vEditorAlbums;

vector<int> vFoundPositions;
int found_pos = 0;

Window *wCurrent = 0;
Window *wPrev = 0;

Menu<Song> *mPlaylist;
Menu<Item> *mBrowser;
Menu<string> *mTagEditor;
Menu<string> *mSearcher;

Menu<string> *mLibArtists;
Menu<string> *mLibAlbums;
Menu<Song> *mLibSongs;

Menu<string> *mEditorAlbums;
Menu<string> *mEditorTagTypes;
Menu<Song> *mEditorTags;

Menu<string> *mPlaylistList;
Menu<Song> *mPlaylistEditor;

Scrollpad *sHelp;
Scrollpad *sLyrics;

Window *wHeader;
Window *wFooter;

MPDConnection *Mpd;

time_t block_delay;
time_t timer;
time_t now;

int now_playing = -1;
int playing_song_scroll_begin = 0;
int browsed_dir_scroll_begin = 0;
int stats_scroll_begin = 0;

int block_statusbar_update_delay = -1;

string browsed_dir = "/";
string song_lyrics;
string player_state;
string volume_state;
string switch_state;

string mpd_repeat;
string mpd_random;
string mpd_crossfade;
string mpd_db_updating;

NcmpcppScreen current_screen;
NcmpcppScreen prev_screen;

Song edited_song;
Song searched_song;

bool main_exit = 0;
bool messages_allowed = 0;
bool title_allowed = 0;

bool header_update_status = 0;

bool dont_change_now_playing = 0;
bool block_progressbar_update = 0;
bool block_statusbar_update = 0;
bool allow_statusbar_unblock = 1;
bool block_playlist_update = 0;

bool search_case_sensitive = 1;
bool search_mode_match = 1;

bool redraw_me = 0;

extern string EMPTY_TAG;
extern string UNKNOWN_ARTIST;
extern string UNKNOWN_TITLE;
extern string UNKNOWN_ALBUM;

extern string playlist_stats;

const string message_part_of_songs_added = "Only part of requested songs' list added to playlist!";

int main(int argc, char *argv[])
{
	DefaultConfiguration(Config);
	DefaultKeys(Key);
	ReadConfiguration(Config);
	ReadKeys(Key);
	DefineEmptyTags();
	
	Mpd = new MPDConnection;
	
	if (getenv("MPD_HOST"))
		Mpd->SetHostname(getenv("MPD_HOST"));
	if (getenv("MPD_PORT"))
		Mpd->SetPort(atoi(getenv("MPD_PORT")));
	if (getenv("MPD_PASSWORD"))
		Mpd->SetPassword(getenv("MPD_PASSWORD"));
	
	Mpd->SetTimeout(Config.mpd_connection_timeout);
	
	if (!Mpd->Connect())
	{
		printf("Cannot connect to mpd: %s\n", Mpd->GetLastErrorMessage().c_str());
		return -1;
	}
	
	setlocale(LC_ALL,"");
	initscr();
	noecho();
	cbreak();
	curs_set(0);
	
	if (Config.colors_enabled)
		Window::EnableColors();
	
	int main_start_y = 2;
	int main_height = LINES-4;
	
	if (!Config.header_visibility)
	{
		main_start_y -= 2;
		main_height += 2;
	}
	if (!Config.statusbar_visibility)
		main_height++;
	
	mPlaylist = new Menu<Song>(0, main_start_y, COLS, main_height, Config.columns_in_playlist ? DisplayColumns(Config.song_columns_list_format) : "", Config.main_color, brNone);
	mPlaylist->SetSelectPrefix(Config.selected_item_prefix);
	mPlaylist->SetSelectSuffix(Config.selected_item_suffix);
	mPlaylist->SetItemDisplayer(Config.columns_in_playlist ? DisplaySongInColumns : DisplaySong);
	mPlaylist->SetItemDisplayerUserData(Config.columns_in_playlist ? &Config.song_columns_list_format : &Config.song_list_format);
	
	mBrowser = new Menu<Item>(0, main_start_y, COLS, main_height, "", Config.main_color, brNone);
	mBrowser->SetSelectPrefix(Config.selected_item_prefix);
	mBrowser->SetSelectSuffix(Config.selected_item_suffix);
	mBrowser->SetItemDisplayer(DisplayItem);
	
	mTagEditor = new Menu<string>(0, main_start_y, COLS, main_height, "", Config.main_color, brNone);
	mSearcher = static_cast<Menu<string> *>(mTagEditor->Clone());
	mSearcher->SetSelectPrefix(Config.selected_item_prefix);
	mSearcher->SetSelectSuffix(Config.selected_item_suffix);
	
	int lib_artist_width = COLS/3-1;
	int lib_albums_width = COLS/3;
	int lib_albums_start_x = lib_artist_width+1;
	int lib_songs_width = COLS-COLS/3*2-1;
	int lib_songs_start_x = lib_artist_width+lib_albums_width+2;
	
	mLibArtists = new Menu<string>(0, main_start_y, lib_artist_width, main_height, "Artists", Config.main_color, brNone);
	mLibAlbums = new Menu<string>(lib_albums_start_x, main_start_y, lib_albums_width, main_height, "Albums", Config.main_color, brNone);
	mLibSongs = new Menu<Song>(lib_songs_start_x, main_start_y, lib_songs_width, main_height, "Songs", Config.main_color, brNone);
	mLibSongs->SetSelectPrefix(Config.selected_item_prefix);
	mLibSongs->SetSelectSuffix(Config.selected_item_suffix);
	mLibSongs->SetItemDisplayer(DisplaySong);
	mLibSongs->SetItemDisplayerUserData(&Config.song_library_format);
	
	mEditorAlbums = new Menu<string>(0, main_start_y, lib_artist_width, main_height, "Albums", Config.main_color, brNone);
	mEditorTagTypes = new Menu<string>(lib_albums_start_x, main_start_y, lib_albums_width, main_height, "Tag types", Config.main_color, brNone);
	mEditorTags = new Menu<Song>(lib_songs_start_x, main_start_y, lib_songs_width, main_height, "Tags", Config.main_color, brNone);
	mEditorTags->SetItemDisplayer(DisplayTag);
	mEditorTags->SetItemDisplayerUserData(mEditorTagTypes);
	
	mPlaylistList = new Menu<string>(0, main_start_y, lib_artist_width, main_height, "Playlists", Config.main_color, brNone);
	mPlaylistEditor = new Menu<Song>(lib_albums_start_x, main_start_y, lib_albums_width+lib_songs_width+1, main_height, "Playlist's content", Config.main_color, brNone);
	mPlaylistEditor->SetSelectPrefix(Config.selected_item_prefix);
	mPlaylistEditor->SetSelectSuffix(Config.selected_item_suffix);
	mPlaylistEditor->SetItemDisplayer(DisplaySong);
	mPlaylistEditor->SetItemDisplayerUserData(&Config.song_list_format);
	
	sHelp = new Scrollpad(0, main_start_y, COLS, main_height, "", Config.main_color, brNone);
	sLyrics = static_cast<Scrollpad *>(sHelp->EmptyClone());
	
	sHelp->Add("   [.b]Keys - Movement\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(Key.Up) + "Move Cursor up\n");
	sHelp->Add(DisplayKeys(Key.Down) + "Move Cursor down\n");
	sHelp->Add(DisplayKeys(Key.PageUp) + "Page up\n");
	sHelp->Add(DisplayKeys(Key.PageDown) + "Page down\n");
	sHelp->Add(DisplayKeys(Key.Home) + "Home\n");
	sHelp->Add(DisplayKeys(Key.End) + "End\n\n");
	
	sHelp->Add(DisplayKeys(Key.ScreenSwitcher) + "Switch between playlist and browser\n");
	sHelp->Add(DisplayKeys(Key.Help) + "Help screen\n");
	sHelp->Add(DisplayKeys(Key.Playlist) + "Playlist screen\n");
	sHelp->Add(DisplayKeys(Key.Browser) + "Browse screen\n");
	sHelp->Add(DisplayKeys(Key.SearchEngine) + "Search engine\n");
	sHelp->Add(DisplayKeys(Key.MediaLibrary) + "Media library\n");
	sHelp->Add(DisplayKeys(Key.PlaylistEditor) + "Playlist editor\n");
	sHelp->Add(DisplayKeys(Key.AlbumEditor) + "Album editor\n\n\n");
	
	sHelp->Add("   [.b]Keys - Global\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(Key.Stop) + "Stop\n");
	sHelp->Add(DisplayKeys(Key.Pause) + "Pause\n");
	sHelp->Add(DisplayKeys(Key.Next) + "Next track\n");
	sHelp->Add(DisplayKeys(Key.Prev) + "Previous track\n");
	sHelp->Add(DisplayKeys(Key.SeekForward) + "Seek forward\n");
	sHelp->Add(DisplayKeys(Key.SeekBackward) + "Seek backward\n");
	sHelp->Add(DisplayKeys(Key.VolumeDown) + "Decrease volume\n");
	sHelp->Add(DisplayKeys(Key.VolumeUp) + "Increase volume\n\n");
	
	sHelp->Add(DisplayKeys(Key.ToggleSpaceMode) + "Toggle space mode (select/add)\n");
	sHelp->Add(DisplayKeys(Key.ReverseSelection) + "Reverse selection\n");
	sHelp->Add(DisplayKeys(Key.DeselectAll) + "Deselect all items\n");
	sHelp->Add(DisplayKeys(Key.AddSelected) + "Add selected items to playlist/m3u file\n\n");
	
	sHelp->Add(DisplayKeys(Key.ToggleRepeat) + "Toggle repeat mode\n");
	sHelp->Add(DisplayKeys(Key.ToggleRepeatOne) + "Toggle \"repeat one\" mode\n");
	sHelp->Add(DisplayKeys(Key.ToggleRandom) + "Toggle random mode\n");
	sHelp->Add(DisplayKeys(Key.Shuffle) + "Shuffle playlist\n");
	sHelp->Add(DisplayKeys(Key.ToggleCrossfade) + "Toggle crossfade mode\n");
	sHelp->Add(DisplayKeys(Key.SetCrossfade) + "Set crossfade\n");
	sHelp->Add(DisplayKeys(Key.UpdateDB) + "Start a music database update\n\n");
	
	sHelp->Add(DisplayKeys(Key.FindForward) + "Forward find\n");
	sHelp->Add(DisplayKeys(Key.FindBackward) + "Backward find\n");
	sHelp->Add(DisplayKeys(Key.PrevFoundPosition) + "Go to previous found position\n");
	sHelp->Add(DisplayKeys(Key.NextFoundPosition) + "Go to next found position\n");
	sHelp->Add(DisplayKeys(Key.ToggleFindMode) + "Toggle find mode (normal/wrapped)\n");
	sHelp->Add(DisplayKeys(Key.GoToContainingDir) + "Go to directory containing current item\n");
	sHelp->Add(DisplayKeys(Key.EditTags) + tag_screen_keydesc);
	sHelp->Add(DisplayKeys(Key.GoToPosition) + "Go to chosen position in current song\n");
	sHelp->Add(DisplayKeys(Key.Lyrics) + "Show/hide song's lyrics\n\n");
	
	sHelp->Add(DisplayKeys(Key.Quit) + "Quit\n\n\n");
	
	sHelp->Add("   [.b]Keys - Playlist screen\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(Key.Enter) + "Play\n");
	sHelp->Add(DisplayKeys(Key.Delete) + "Delete item/selected items from playlist\n");
	sHelp->Add(DisplayKeys(Key.Clear) + "Clear playlist\n");
	sHelp->Add(DisplayKeys(Key.Crop) + "Clear playlist but hold currently playing/selected items\n");
	sHelp->Add(DisplayKeys(Key.MvSongUp) + "Move item/group of items up\n");
	sHelp->Add(DisplayKeys(Key.MvSongDown) + "Move item/group of items down\n");
	sHelp->Add(DisplayKeys(Key.Add) + "Add url/file/directory to playlist\n");
	sHelp->Add(DisplayKeys(Key.SavePlaylist) + "Save playlist\n");
	sHelp->Add(DisplayKeys(Key.GoToNowPlaying) + "Go to currently playing position\n");
	sHelp->Add(DisplayKeys(Key.TogglePlaylistDisplayMode) + "Toggle playlist display mode\n");
	sHelp->Add(DisplayKeys(Key.ToggleAutoCenter) + "Toggle auto center mode\n\n\n");
	
	sHelp->Add("   [.b]Keys - Browse screen\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(Key.Enter) + "Enter directory/Add item to playlist and play\n");
	sHelp->Add(DisplayKeys(Key.Space) + "Add item to playlist\n");
	sHelp->Add(DisplayKeys(Key.GoToParentDir) + "Go to parent directory\n");
	sHelp->Add(DisplayKeys(Key.Delete) + "Delete playlist\n\n\n");
	
	sHelp->Add("   [.b]Keys - Search engine\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(Key.Enter) + "Add item to playlist and play/change option\n");
	sHelp->Add(DisplayKeys(Key.Space) + "Add item to playlist\n");
	sHelp->Add(DisplayKeys(Key.StartSearching) + "Start searching immediately\n\n\n");
	
	sHelp->Add("   [.b]Keys - Media library\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(&Key.VolumeDown[0], 1) + "Previous column\n");
	sHelp->Add(DisplayKeys(&Key.VolumeUp[0], 1) + "Next column\n");
	sHelp->Add(DisplayKeys(Key.Enter) + "Add to playlist and play song/album/artist's songs\n");
	sHelp->Add(DisplayKeys(Key.Space) + "Add to playlist song/album/artist's songs\n\n\n");
	
	sHelp->Add("   [.b]Keys - Playlist Editor\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(&Key.VolumeDown[0], 1) + "Previous column\n");
	sHelp->Add(DisplayKeys(&Key.VolumeUp[0], 1) + "Next column\n");
	sHelp->Add(DisplayKeys(Key.Enter) + "Add item to playlist and play\n");
	sHelp->Add(DisplayKeys(Key.Space) + "Add to playlist/select item\n");
	sHelp->Add(DisplayKeys(Key.MvSongUp) + "Move item/group of items up\n");
	sHelp->Add(DisplayKeys(Key.MvSongDown) + "Move item/group of items down\n\n\n");
	
#	ifdef HAVE_TAGLIB_H
	sHelp->Add("   [.b]Keys - Tag editor\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(Key.Enter) + "Change option\n");
#	else
	sHelp->Add("   [.b]Keys - Tag info\n -----------------------------------------[/b]\n");
	sHelp->Add(DisplayKeys(Key.Enter) + "Return\n");
#	endif
	
	if (Config.header_visibility)
	{
		wHeader = new Window(0, 0, COLS, 2, "", Config.header_color, brNone);
		wHeader->Display();
	}
	
	int footer_start_y = LINES-(Config.statusbar_visibility ? 2 : 1);
	int footer_height = Config.statusbar_visibility ? 2 : 1;
	
	wFooter = new Window(0, footer_start_y, COLS, footer_height, "", Config.statusbar_color, brNone);
	wFooter->SetGetStringHelper(TraceMpdStatus);
	wFooter->Display();
	
	wCurrent = mPlaylist;
	current_screen = csPlaylist;
	
	int input;
	timer = time(NULL);
	
	sHelp->SetTimeout(ncmpcpp_window_timeout);
	mPlaylist->SetTimeout(ncmpcpp_window_timeout);
	mBrowser->SetTimeout(ncmpcpp_window_timeout);
	mTagEditor->SetTimeout(ncmpcpp_window_timeout);
	mSearcher->SetTimeout(ncmpcpp_window_timeout);
	mLibArtists->SetTimeout(ncmpcpp_window_timeout);
	mLibAlbums->SetTimeout(ncmpcpp_window_timeout);
	mLibSongs->SetTimeout(ncmpcpp_window_timeout);
	mEditorAlbums->SetTimeout(ncmpcpp_window_timeout);
	mEditorTagTypes->SetTimeout(ncmpcpp_window_timeout);
	mEditorTags->SetTimeout(ncmpcpp_window_timeout);
	sLyrics->SetTimeout(ncmpcpp_window_timeout);
	wFooter->SetTimeout(ncmpcpp_window_timeout);
	mPlaylistList->SetTimeout(ncmpcpp_window_timeout);
	mPlaylistEditor->SetTimeout(ncmpcpp_window_timeout);
	
	mPlaylist->HighlightColor(Config.main_highlight_color);
	mBrowser->HighlightColor(Config.main_highlight_color);
	mTagEditor->HighlightColor(Config.main_highlight_color);
	mSearcher->HighlightColor(Config.main_highlight_color);
	mLibArtists->HighlightColor(Config.main_highlight_color);
	mLibAlbums->HighlightColor(Config.main_highlight_color);
	mLibSongs->HighlightColor(Config.main_highlight_color);
	mPlaylistList->HighlightColor(Config.main_highlight_color);
	mPlaylistEditor->HighlightColor(Config.main_highlight_color);
	
	Mpd->SetStatusUpdater(NcmpcppStatusChanged, NULL);
	Mpd->SetErrorHandler(NcmpcppErrorCallback, NULL);
	
	while (!main_exit)
	{
		if (!Mpd->Connected())
		{
			ShowMessage("Attempting to reconnect...");
			Mpd->Disconnect();
			if (Mpd->Connect())
				ShowMessage("Connected!");
			messages_allowed = 0;
		}
		
		TraceMpdStatus();
		
		block_playlist_update = 0;
		messages_allowed = 1;
		
		if (Config.header_visibility)
		{
			string title;
			const int max_allowed_title_length = wHeader->GetWidth()-volume_state.length();
			
			switch (current_screen)
			{
				case csHelp:
					title = "Help";
					break;
				case csPlaylist:
					title = "Playlist ";
					break;
				case csBrowser:
					title = "Browse: ";
					break;
				case csTagEditor:
					title = tag_screen;
					break;
				case csSearcher:
					title = "Search engine";
					break;
				case csLibrary:
					title = "Media library";
					break;
				case csLyrics:
					title = song_lyrics;
					break;
				case csPlaylistEditor:
					title = "Playlist editor";
					break;
				case csAlbumEditor:
					title = "Albums' tag editor";
					break;
			}
		
			if (title_allowed)
			{
				wHeader->Bold(1);
				wHeader->WriteXY(0, 0, max_allowed_title_length, title, 1);
				wHeader->Bold(0);
				
				if (current_screen == csPlaylist && !playlist_stats.empty())
					wHeader->WriteXY(title.length(), 0, max_allowed_title_length-title.length(), playlist_stats);
				
				if (current_screen == csBrowser)
				{
					int max_length_without_scroll = wHeader->GetWidth()-volume_state.length()-title.length();
					my_string_t wbrowseddir = TO_WSTRING(browsed_dir);
					wHeader->Bold(1);
					if (browsed_dir.length() > max_length_without_scroll)
					{
#						ifdef UTF8_ENABLED
						wbrowseddir += L" ** ";
#						else
						wbrowseddir += " ** ";
#						endif
						const int scrollsize = max_length_without_scroll;
						my_string_t part = wbrowseddir.substr(browsed_dir_scroll_begin++, scrollsize);
						if (part.length() < scrollsize)
							part += wbrowseddir.substr(0, scrollsize-part.length());
						wHeader->WriteXY(title.length(), 0, part);
						if (browsed_dir_scroll_begin >= wbrowseddir.length())
							browsed_dir_scroll_begin = 0;
					}
					else
						wHeader->WriteXY(title.length(), 0, browsed_dir);
					wHeader->Bold(0);
				}
			}
			else
				wHeader->WriteXY(0, 0, max_allowed_title_length, "[.b]1:[/b]Help  [.b]2:[/b]Playlist  [.b]3:[/b]Browse  [.b]4:[/b]Search  [.b]5:[/b]Library [.b]6:[/b]Playlist editor [.b]7:[/b]Albums' editor", 1);
		
			wHeader->SetColor(Config.volume_color);
			wHeader->WriteXY(max_allowed_title_length, 0, volume_state);
			wHeader->SetColor(Config.header_color);
		}
		
		// media library stuff
		
		if (current_screen == csLibrary)
		{
			if (mLibArtists->Empty())
			{
				found_pos = 0;
				vFoundPositions.clear();
				TagList list;
				mLibAlbums->Clear(0);
				mLibSongs->Clear(0);
				Mpd->GetArtists(list);
				sort(list.begin(), list.end(), CaseInsensitiveComparison());
				for (TagList::const_iterator it = list.begin(); it != list.end(); it++)
					mLibArtists->AddOption(*it);
				mLibArtists->Window::Clear();
				mLibArtists->Refresh();
			}
			
			if (mLibAlbums->Empty() && mLibSongs->Empty())
			{
				mLibAlbums->Reset();
				vLibAlbums.clear();
				TagList list;
				Mpd->GetAlbums(mLibArtists->GetOption(), list);
				for (TagList::const_iterator it = list.begin(); it != list.end(); it++)
				{
					bool written = 0;
					SongList l;
					Mpd->StartSearch(1);
					Mpd->AddSearch(MPD_TAG_ITEM_ARTIST, mLibArtists->GetOption());
					Mpd->AddSearch(MPD_TAG_ITEM_ALBUM, *it);
					Mpd->CommitSearch(l);
					for (SongList::const_iterator j = l.begin(); j != l.end(); j++)
					{
						if ((*j)->GetYear() != EMPTY_TAG)
						{
							vLibAlbums["(" + (*j)->GetYear() + ") " + *it] = *it;
							written = 1;
							break;
						}
					}
					if (!written)
						vLibAlbums[*it] = *it;
					FreeSongList(l);
				}
				for (std::map<string, string>::const_iterator it = vLibAlbums.begin(); it != vLibAlbums.end(); it++)
					mLibAlbums->AddOption(it->first);
				mLibAlbums->Window::Clear();
				mLibAlbums->Refresh();
			}
			
			if (wCurrent == mLibAlbums && mLibAlbums->Empty())
			{
				mLibAlbums->HighlightColor(Config.main_highlight_color);
				mLibArtists->HighlightColor(Config.active_column_color);
				wCurrent = mLibArtists;
			}
			
			if (mLibSongs->Empty())
			{
				mLibSongs->Reset();
				SongList list;
				if (mLibAlbums->Empty())
				{
					mLibAlbums->WriteXY(0, 0, "No albums found.");
					mLibSongs->Clear(0);
					Mpd->StartSearch(1);
					Mpd->AddSearch(MPD_TAG_ITEM_ARTIST, mLibArtists->GetOption());
					Mpd->CommitSearch(list);
				}
				else
				{
					mLibSongs->Clear(0);
					Mpd->StartSearch(1);
					Mpd->AddSearch(MPD_TAG_ITEM_ARTIST, mLibArtists->GetOption());
					Mpd->AddSearch(MPD_TAG_ITEM_ALBUM, vLibAlbums[mLibAlbums->GetOption()]);
					Mpd->CommitSearch(list);
				}
				sort(list.begin(), list.end(), SortSongsByTrack);
				bool bold = 0;
			
				for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
				{
					for (int j = 0; j < mPlaylist->Size(); j++)
					{
						if ((*it)->GetHash() == mPlaylist->at(j).GetHash())
						{
							bold = 1;
							break;
						}
					}
					bold ? mLibSongs->AddBoldOption(**it) : mLibSongs->AddOption(**it);
					bold = 0;
				}
				FreeSongList(list);
				mLibSongs->Window::Clear();
				mLibSongs->Refresh();
			}
		}
		
		// media library end
		
		// playlist editor stuff
		
		if (current_screen == csPlaylistEditor)
		{
			if (mPlaylistList->Empty())
			{
				mPlaylistEditor->Clear(0);
				TagList list;
				Mpd->GetPlaylists(list);
				for (TagList::const_iterator it = list.begin(); it != list.end(); it++)
					mPlaylistList->AddOption(*it);
				mPlaylistList->Window::Clear();
				mPlaylistList->Refresh();
			}
		
			if (mPlaylistEditor->Empty())
			{
				mPlaylistEditor->Reset();
				SongList list;
				Mpd->GetPlaylistContent(mPlaylistList->GetOption(), list);
				if (!list.empty())
					mPlaylistEditor->SetTitle("Playlist's content (" + IntoStr(list.size()) + " item" + (list.size() == 1 ? ")" : "s)"));
				else
					mPlaylistEditor->SetTitle("Playlist's content");
				bool bold = 0;
				for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
				{
					for (int j = 0; j < mPlaylist->Size(); j++)
					{
						if ((*it)->GetHash() == mPlaylist->at(j).GetHash())
						{
							bold = 1;
							break;
						}
					}
					bold ? mPlaylistEditor->AddBoldOption(**it) : mPlaylistEditor->AddOption(**it);
					bold = 0;
				}
				FreeSongList(list);
				mPlaylistEditor->Window::Clear();
				mPlaylistEditor->Display();
			}
			
			if (wCurrent == mPlaylistEditor && mPlaylistEditor->Empty())
			{
				
				mPlaylistEditor->HighlightColor(Config.main_highlight_color);
				mPlaylistList->HighlightColor(Config.active_column_color);
				wCurrent = mPlaylistList;
			}
			
			if (mPlaylistEditor->Empty())
				mPlaylistEditor->WriteXY(0, 0, "Playlist is empty.");
		}
		
		// playlist editor end
		
		// album editor stuff
		
		if (current_screen == csAlbumEditor)
		{
			if (mEditorAlbums->Empty())
			{
				found_pos = 0;
				vFoundPositions.clear();
				vEditorAlbums.clear();
				mEditorAlbums->Window::Clear();
				mEditorTags->Clear();
				TagList list;
				mEditorAlbums->WriteXY(0, 0, "Fetching albums' list...");
				Mpd->GetAlbums("", list);
				for (TagList::const_iterator it = list.begin(); it != list.end(); it++)
				{
					bool written = 0;
					SongList l;
					Mpd->StartSearch(1);
					Mpd->AddSearch(MPD_TAG_ITEM_ALBUM, *it);
					Mpd->CommitSearch(l);
					for (SongList::const_iterator j = l.begin(); j != l.end(); j++)
					{
						if ((*j)->GetYear() != EMPTY_TAG)
						{
							vEditorAlbums["(" + (*j)->GetYear() + ") " + *it] = *it;
							written = 1;
							break;
						}
					}
					if (!written)
						vEditorAlbums[*it] = *it;
					FreeSongList(l);
				}
				for (std::map<string, string>::const_iterator it = vEditorAlbums.begin(); it != vEditorAlbums.end(); it++)
					mEditorAlbums->AddOption(it->first);
				mEditorAlbums->Refresh();
				mEditorTagTypes->Refresh();
			}
			
			if (mEditorTags->Empty())
			{
				mEditorTags->Reset();
				SongList list;
				Mpd->StartSearch(1);
				Mpd->AddSearch(MPD_TAG_ITEM_ALBUM, vEditorAlbums[mEditorAlbums->GetOption()]);
				Mpd->CommitSearch(list);
				for (SongList::iterator it = list.begin(); it != list.end(); it++)
					mEditorTags->AddOption(**it);
				FreeSongList(list);
				mEditorTags->Window::Clear();
				mEditorTags->Refresh();
			}
			
			mEditorTagTypes->GetChoice() < 7 ? mEditorTags->Refresh(1) : mEditorTags->Window::Clear();
		}
		
		// album editor end
		
		if (Config.columns_in_playlist && wCurrent == mPlaylist)
			wCurrent->Display(redraw_me);
		else
			wCurrent->Refresh(redraw_me);
		redraw_me = 0;
		
		wCurrent->ReadKey(input);
		if (input == ERR)
			continue;
		
		title_allowed = 1;
		timer = time(NULL);
		
		switch (current_screen)
		{
			case csPlaylist:
				mPlaylist->Highlighting(1);
				break;
			case csBrowser:
				browsed_dir_scroll_begin--;
				break;
			case csLibrary:
			case csPlaylistEditor:
			case csAlbumEditor:
			{
				if (Keypressed(input, Key.Up) || Keypressed(input, Key.Down) || Keypressed(input, Key.PageUp) || Keypressed(input, Key.PageDown) || Keypressed(input, Key.Home) || Keypressed(input, Key.End) || Keypressed(input, Key.FindForward) || Keypressed(input, Key.FindBackward) || Keypressed(input, Key.NextFoundPosition) || Keypressed(input, Key.PrevFoundPosition))
				{
					if (wCurrent == mLibArtists)
					{
						mLibAlbums->Clear(0);
						mLibSongs->Clear(0);
					}
					else if (wCurrent == mLibAlbums)
					{
						mLibSongs->Clear(0);
					}
					else if (wCurrent == mPlaylistList)
					{
						mPlaylistEditor->Clear(0);
					}
					else if (wCurrent == mEditorAlbums)
					{
						mEditorTags->Clear(0);
						mEditorTagTypes->Reset();
						mEditorTagTypes->Refresh();
					}
				}
			}
			default:
				break;
		}
		
		// key mapping beginning
		
		if (Keypressed(input, Key.Up))
		{
			wCurrent->Go(wUp);
		}
		else if (Keypressed(input, Key.Down))
		{
			wCurrent->Go(wDown);
		}
		else if (Keypressed(input, Key.PageUp))
		{
			wCurrent->Go(wPageUp);
		}
		else if (Keypressed(input, Key.PageDown))
		{
			wCurrent->Go(wPageDown);
		}
		else if (Keypressed(input, Key.Home))
		{
			wCurrent->Go(wHome);
		}
		else if (Keypressed(input, Key.End))
		{
			wCurrent->Go(wEnd);
		}
		else if (input == KEY_RESIZE)
		{
			redraw_me = 1;
			
			if (COLS < 20 || LINES < 5)
			{
				endwin();
				printf("Screen too small!\n");
				return 1;
			}
			
			main_height = LINES-4;
	
			if (!Config.header_visibility)
				main_height += 2;
			if (!Config.statusbar_visibility)
				main_height++;
			
			sHelp->Resize(COLS, main_height);
			mPlaylist->Resize(COLS, main_height);
			mPlaylist->SetTitle(Config.columns_in_playlist ? DisplayColumns(Config.song_columns_list_format) : "");
			mBrowser->Resize(COLS, main_height);
			mTagEditor->Resize(COLS, main_height);
			mSearcher->Resize(COLS, main_height);
			sLyrics->Resize(COLS, main_height);
			
			lib_artist_width = COLS/3-1;
			lib_albums_start_x = lib_artist_width+1;
			lib_albums_width = COLS/3;
			lib_songs_start_x = lib_artist_width+lib_albums_width+2;
			lib_songs_width = COLS-COLS/3*2-1;
			
			mLibArtists->Resize(lib_artist_width, main_height);
			mLibAlbums->Resize(lib_albums_width, main_height);
			mLibSongs->Resize(lib_songs_width, main_height);
			mLibAlbums->MoveTo(lib_albums_start_x, main_start_y);
			mLibSongs->MoveTo(lib_songs_start_x, main_start_y);
			
			mEditorAlbums->Resize(lib_artist_width, main_height);
			mEditorTagTypes->Resize(lib_albums_width, main_height);
			mEditorTags->Resize(lib_songs_width, main_height);
			mEditorTagTypes->MoveTo(lib_albums_start_x, main_start_y);
			mEditorTags->MoveTo(lib_songs_start_x, main_start_y);
			
			mPlaylistList->Resize(lib_artist_width, main_height);
			mPlaylistEditor->Resize(lib_albums_width+lib_songs_width+1, main_height);
			mPlaylistEditor->MoveTo(lib_albums_start_x, main_start_y);
			
			if (Config.header_visibility)
				wHeader->Resize(COLS, wHeader->GetHeight());
			
			footer_start_y = LINES-(Config.statusbar_visibility ? 2 : 1);
			wFooter->MoveTo(0, footer_start_y);
			wFooter->Resize(COLS, wFooter->GetHeight());
			
			if (wCurrent != sHelp)
				wCurrent->Window::Clear();
			wCurrent->Refresh(1);
			if (current_screen == csLibrary)
			{
				REFRESH_MEDIA_LIBRARY_SCREEN;
			}
			else if (current_screen == csPlaylistEditor)
			{
				REFRESH_PLAYLIST_EDITOR_SCREEN;
			}
			else if (current_screen == csAlbumEditor)
			{
				REFRESH_ALBUM_EDITOR_SCREEN;
			}
			header_update_status = 1;
			PlayerState mpd_state = Mpd->GetState();
			MPDStatusChanges changes;
			if (mpd_state == psPlay || mpd_state == psPause)
				changes.ElapsedTime = 1; // restore status
			else
				changes.PlayerState = 1;
			
			NcmpcppStatusChanged(Mpd, changes, NULL);
		}
		else if (Keypressed(input, Key.GoToParentDir))
		{
			if (wCurrent == mBrowser && browsed_dir != "/")
			{
				mBrowser->Reset();
				goto GO_TO_PARENT_DIR;
			}
		}
		else if (Keypressed(input, Key.Enter))
		{
			switch (current_screen)
			{
				case csPlaylist:
				{
					if (!mPlaylist->Empty())
						Mpd->PlayID(mPlaylist->at(mPlaylist->GetChoice()).GetID());
					break;
				}
				case csBrowser:
				{
					GO_TO_PARENT_DIR:
					
					const Item &item = mBrowser->at(mBrowser->GetChoice());
					switch (item.type)
					{
						case itDirectory:
						{
							found_pos = 0;
							vFoundPositions.clear();
							GetDirectory(item.name, browsed_dir);
							break;
						}
						case itSong:
						{
							Song &s = *item.song;
							int id = Mpd->AddSong(s);
							if (id >= 0)
							{
								Mpd->PlayID(id);
								ShowMessage("Added to playlist: " + DisplaySong(s, &Config.song_status_format));
							}
							mBrowser->Refresh();
							break;
						}
						case itPlaylist:
						{
							SongList list;
							Mpd->GetPlaylistContent(item.name, list);
							for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
								Mpd->QueueAddSong(**it);
							if (Mpd->CommitQueue())
							{
								ShowMessage("Loading and playing playlist " + item.name + "...");
								Song *s = &mPlaylist->at(mPlaylist->Size()-list.size());
								if (s->GetHash() == list[0]->GetHash())
									Mpd->PlayID(s->GetID());
								else
									ShowMessage(message_part_of_songs_added);
							}
							FreeSongList(list);
							break;
						}
					}
					break;
				}
				case csTagEditor:
				{
#					ifdef HAVE_TAGLIB_H
					int id = mTagEditor->GetRealChoice()+1;
					int option = mTagEditor->GetChoice();
					LOCK_STATUSBAR;
					Song &s = edited_song;
					
					switch (id)
					{
						case 1:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]New title:[/b] ", 1);
							if (s.GetTitle() == UNKNOWN_TITLE)
								s.SetTitle(wFooter->GetString());
							else
								s.SetTitle(wFooter->GetString(s.GetTitle()));
							mTagEditor->UpdateOption(option, "[.b]Title:[/b] " + s.GetTitle());
							break;
						}
						case 2:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]New artist:[/b] ", 1);
							if (s.GetArtist() == UNKNOWN_ARTIST)
								s.SetArtist(wFooter->GetString());
							else
								s.SetArtist(wFooter->GetString(s.GetArtist()));
							mTagEditor->UpdateOption(option, "[.b]Artist:[/b] " + s.GetArtist());
							break;
						}
						case 3:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]New album:[/b] ", 1);
							if (s.GetAlbum() == UNKNOWN_ALBUM)
								s.SetAlbum(wFooter->GetString());
							else
								s.SetAlbum(wFooter->GetString(s.GetAlbum()));
							mTagEditor->UpdateOption(option, "[.b]Album:[/b] " + s.GetAlbum());
							break;
						}
						case 4:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]New year:[/b] ", 1);
							if (s.GetYear() == EMPTY_TAG)
								s.SetYear(wFooter->GetString(4));
							else
								s.SetYear(wFooter->GetString(s.GetYear(), 4));
							mTagEditor->UpdateOption(option, "[.b]Year:[/b] " + s.GetYear());
							break;
						}
						case 5:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]New track:[/b] ", 1);
							if (s.GetTrack() == EMPTY_TAG)
								s.SetTrack(wFooter->GetString(3));
							else
								s.SetTrack(wFooter->GetString(s.GetTrack(), 3));
							mTagEditor->UpdateOption(option, "[.b]Track:[/b] " + s.GetTrack());
							break;
						}
						case 6:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]New genre:[/b] ", 1);
							if (s.GetGenre() == EMPTY_TAG)
								s.SetGenre(wFooter->GetString());
							else
								s.SetGenre(wFooter->GetString(s.GetGenre()));
							mTagEditor->UpdateOption(option, "[.b]Genre:[/b] " + s.GetGenre());
							break;
						}
						case 7:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]New comment:[/b] ", 1);
							if (s.GetComment() == EMPTY_TAG)
								s.SetComment(wFooter->GetString());
							else
								s.SetComment(wFooter->GetString(s.GetComment()));
							mTagEditor->UpdateOption(option, "[.b]Comment:[/b] " + s.GetComment());
							break;
						}
						case 8:
						{
							ShowMessage("Updating tags...");
							if (WriteTags(s))
							{
								ShowMessage("Tags updated!");
								Mpd->UpdateDirectory(s.GetDirectory());
								if (prev_screen == csSearcher)
								{
									*vSearched[mSearcher->GetRealChoice()-1] = s;
									mSearcher->UpdateOption(mSearcher->GetChoice(), DisplaySong(s));
								}
							}
								ShowMessage("Error writing tags!");
						}
						case 9:
						{
#							endif // HAVE_TAGLIB_H
							wCurrent->Clear();
							wCurrent = wPrev;
							current_screen = prev_screen;
							redraw_me = 1;
							if (current_screen == csLibrary)
							{
								REFRESH_MEDIA_LIBRARY_SCREEN;
							}
							else if (current_screen == csPlaylistEditor)
							{
								REFRESH_PLAYLIST_EDITOR_SCREEN;
							}
#							ifdef HAVE_TAGLIB_H
							break;
						}
					}
					UNLOCK_STATUSBAR;
#					endif // HAVE_TAGLIB_H
					break;
				}
				case csSearcher:
				{
					ENTER_SEARCH_ENGINE_SCREEN:
					
					int option = mSearcher->GetChoice();
					LOCK_STATUSBAR;
					Song &s = searched_song;
					
					switch (option+1)
					{
						case 1:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Filename:[/b] ", 1);
							if (s.GetShortFilename() == EMPTY_TAG)
								s.SetShortFilename(wFooter->GetString());
							else
								s.SetShortFilename(wFooter->GetString(s.GetShortFilename()));
							mSearcher->UpdateOption(option, "[.b]Filename:[/b] " + s.GetShortFilename());
							break;
						}
						case 2:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Title:[/b] ", 1);
							if (s.GetTitle() == UNKNOWN_TITLE)
								s.SetTitle(wFooter->GetString());
							else
								s.SetTitle(wFooter->GetString(s.GetTitle()));
							mSearcher->UpdateOption(option, "[.b]Title:[/b] " + s.GetTitle());
							break;
						}
						case 3:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Artist:[/b] ", 1);
							if (s.GetArtist() == UNKNOWN_ARTIST)
								s.SetArtist(wFooter->GetString());
							else
								s.SetArtist(wFooter->GetString(s.GetArtist()));
							mSearcher->UpdateOption(option, "[.b]Artist:[/b] " + s.GetArtist());
							break;
						}
						case 4:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Album:[/b] ", 1);
							if (s.GetAlbum() == UNKNOWN_ALBUM)
								s.SetAlbum(wFooter->GetString());
							else
								s.SetAlbum(wFooter->GetString(s.GetAlbum()));
							mSearcher->UpdateOption(option, "[.b]Album:[/b] " + s.GetAlbum());
							break;
						}
						case 5:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Year:[/b] ", 1);
							if (s.GetYear() == EMPTY_TAG)
								s.SetYear(wFooter->GetString(4));
							else
								s.SetYear(wFooter->GetString(s.GetYear(), 4));
							mSearcher->UpdateOption(option, "[.b]Year:[/b] " + s.GetYear());
							break;
						}
						case 6:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Track:[/b] ", 1);
							if (s.GetTrack() == EMPTY_TAG)
								s.SetTrack(wFooter->GetString(3));
							else
								s.SetTrack(wFooter->GetString(s.GetTrack(), 3));
							mSearcher->UpdateOption(option, "[.b]Track:[/b] " + s.GetTrack());
							break;
						}
						case 7:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Genre:[/b] ", 1);
							if (s.GetGenre() == EMPTY_TAG)
								s.SetGenre(wFooter->GetString());
							else
								s.SetGenre(wFooter->GetString(s.GetGenre()));
							mSearcher->UpdateOption(option, "[.b]Genre:[/b] " + s.GetGenre());
							break;
						}
						case 8:
						{
							wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Comment:[/b] ", 1);
							if (s.GetComment() == EMPTY_TAG)
								s.SetComment(wFooter->GetString());
							else
								s.SetComment(wFooter->GetString(s.GetComment()));
							mSearcher->UpdateOption(option, "[.b]Comment:[/b] " + s.GetComment());
							break;
						}
						case 10:
						{
							search_mode_match = !search_mode_match;
							mSearcher->UpdateOption(option, "[.b]Search mode:[/b] " + (search_mode_match ? search_mode_one : search_mode_two));
							break;
						}
						case 11:
						{
							search_case_sensitive = !search_case_sensitive;
							mSearcher->UpdateOption(option, "[.b]Case sensitive:[/b] " + (string)(search_case_sensitive ? "Yes" : "No"));
							break;
						}
						case 13:
						{
							ShowMessage("Searching...");
							Search(vSearched, s);
							if (!vSearched.empty())
							{
								bool bold = 0;
								mSearcher->AddSeparator();
								mSearcher->AddStaticBoldOption("[.white]Search results:[/white] [.green]Found " + IntoStr(vSearched.size()) + (vSearched.size() > 1 ? " songs" : " song") + "[/green]");
								mSearcher->AddSeparator();
								for (SongList::const_iterator it = vSearched.begin(); it != vSearched.end(); it++)
								{
									for (int j = 0; j < mPlaylist->Size(); j++)
									{
										if (mPlaylist->at(j).GetHash() == (*it)->GetHash())
										{
											bold = 1;
											break;
										}
									}
									bold ? mSearcher->AddBoldOption(DisplaySong(**it)) : mSearcher->AddOption(DisplaySong(**it));
									bold = 0;
								}
								ShowMessage("Searching finished!");
								for (int i = 0; i < 13; i++)
									mSearcher->MakeStatic(i, 1);
								mSearcher->Go(wDown);
								mSearcher->Go(wDown);
							}
							else
								ShowMessage("No results found");
							break;
						}
						case 14:
						{
							found_pos = 0;
							vFoundPositions.clear();
							FreeSongList(vSearched);
							PrepareSearchEngine(searched_song);
							ShowMessage("Search state reset");
							break;
						}
						default:
						{
							Song &s = *vSearched[mSearcher->GetRealChoice()-1];
							int id = Mpd->AddSong(s);
							if (id >= 0)
							{
								Mpd->PlayID(id);
								ShowMessage("Added to playlist: " + DisplaySong(s, &Config.song_status_format));
							}
							break;
						}
					}
					UNLOCK_STATUSBAR;
					break;
				}
				case csLibrary:
				{
					ENTER_LIBRARY_SCREEN: // same code for Key.Space, but without playing.
					
					SongList list;
					
					if (wCurrent == mLibArtists)
					{
						const string &artist = mLibArtists->GetOption();
						Mpd->StartSearch(1);
						Mpd->AddSearch(MPD_TAG_ITEM_ARTIST, artist);
						Mpd->CommitSearch(list);
						for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
							Mpd->QueueAddSong(**it);
						if (Mpd->CommitQueue())
						{
							ShowMessage("Adding all songs artist's: " + artist);
							Song *s = &mPlaylist->at(mPlaylist->Size()-list.size());
							if (s->GetHash() == list[0]->GetHash())
							{
								if (Keypressed(input, Key.Enter))
									Mpd->PlayID(s->GetID());
							}
							else
								ShowMessage(message_part_of_songs_added);
						}
					}
					else if (wCurrent == mLibAlbums)
					{
						for (int i = 0; i < mLibSongs->Size(); i++)
							Mpd->QueueAddSong(mLibSongs->at(i));
						if (Mpd->CommitQueue())
						{
							ShowMessage("Adding songs from: " + mLibArtists->GetOption() + " \"" + vLibAlbums[mLibAlbums->GetOption()] + "\"");
							Song *s = &mPlaylist->at(mPlaylist->Size()-mLibSongs->Size());
							if (s->GetHash() == mLibSongs->at(0).GetHash())
							{
								if (Keypressed(input, Key.Enter))
									Mpd->PlayID(s->GetID());
							}
							else
								ShowMessage(message_part_of_songs_added);
						}
					}
					else if (wCurrent == mLibSongs)
					{
						if (!mLibSongs->Empty())
						{
							Song &s = mLibSongs->at(mLibSongs->GetChoice());
							int id = Mpd->AddSong(s);
							if (id >= 0)
							{
								ShowMessage("Added to playlist: " + DisplaySong(s, &Config.song_status_format));
								if (Keypressed(input, Key.Enter))
									Mpd->PlayID(id);
							}
						}
					}
					FreeSongList(list);
					if (Keypressed(input, Key.Space))
					{
						wCurrent->Go(wDown);
						if (wCurrent == mLibArtists)
						{
							mLibAlbums->Clear(0);
							mLibSongs->Clear(0);
						}
						else if (wCurrent == mLibAlbums)
							mLibSongs->Clear(0);
					}
					break;
				}
				case csPlaylistEditor:
				{
					ENTER_PLAYLIST_EDITOR_SCREEN: // same code for Key.Space, but without playing.
					
					SongList list;
					
					if (wCurrent == mPlaylistList)
					{
						const string &playlist = mPlaylistList->GetOption();
						Mpd->GetPlaylistContent(playlist, list);
						for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
							Mpd->QueueAddSong(**it);
						if (Mpd->CommitQueue())
						{
							ShowMessage("Loading playlist " + playlist + "...");
							Song &s = mPlaylist->at(mPlaylist->Size()-list.size());
							if (s.GetHash() == list[0]->GetHash())
							{
								if (Keypressed(input, Key.Enter))
									Mpd->PlayID(s.GetID());
							}
							else
								ShowMessage(message_part_of_songs_added);
						}
					}
					else if (wCurrent == mPlaylistEditor)
					{
						if (!mPlaylistEditor->Empty())
						{
							Song &s = mPlaylistEditor->at(mPlaylistEditor->GetChoice());
							int id = Mpd->AddSong(s);
							if (id >= 0)
							{
								ShowMessage("Added to playlist: " + DisplaySong(s, &Config.song_status_format));
								if (Keypressed(input, Key.Enter))
									Mpd->PlayID(id);
							}
						}
					}
					FreeSongList(list);
					if (Keypressed(input, Key.Space))
						wCurrent->Go(wDown);
					break;
				}
#				ifdef HAVE_TAGLIB_H
				case csAlbumEditor:
				{
					void (Song::*set)(const string &) = 0;
					switch (mEditorTagTypes->GetRealChoice())
					{
						case 0:
							set = &Song::SetTitle;
							break;
						case 1:
							set = &Song::SetArtist;
							break;
						case 2:
							set = &Song::SetAlbum;
							break;
						case 3:
							set = &Song::SetYear;
							break;
						case 4:
							set = &Song::SetTrack;
							break;
						case 5:
							set = &Song::SetGenre;
							break;
						case 6:
							set = &Song::SetComment;
							break;
						case 7: // reset
							mEditorTags->Clear(0);
							ShowMessage("Changes reset");
							continue;
						case 8: // save
						{
							bool success = 1;
							ShowMessage("Writing changes...");
							for (int i = 0; i < mEditorTags->Size(); i++)
							{
								if (!WriteTags(mEditorTags->at(i)))
								{
									ShowMessage("Error writing tags!");
									success = 0;
									break;
								}
							}
							if (success)
							{
								ShowMessage("Tags updated!");
								mEditorTagTypes->HighlightColor(Config.main_highlight_color);
								mEditorTagTypes->Reset();
								wCurrent->Refresh();
								wCurrent = mEditorAlbums;
								mEditorAlbums->HighlightColor(Config.active_column_color);
								Mpd->UpdateDirectory("/");
							}
							else
								mEditorTags->Clear(0);
							continue;
						}
						default:
							break;
					}
					if (wCurrent == mEditorTagTypes)
					{
						LOCK_STATUSBAR;
						wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]" + mEditorTagTypes->GetOption() + "[/b]: ", 1);
						mEditorTags->at(mEditorTags->GetChoice()).GetEmptyFields(1);
						string new_tag = wFooter->GetString(mEditorTags->GetOption());
						mEditorTags->at(mEditorTags->GetChoice()).GetEmptyFields(0);
						UNLOCK_STATUSBAR;
						if (!new_tag.empty())
							for (int i = 0; i < mEditorTags->Size(); i++)
								(mEditorTags->at(i).*set)(new_tag);
					}
					else if (wCurrent == mEditorTags)
					{
						LOCK_STATUSBAR;
						wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]" + mEditorTagTypes->GetOption() + "[/b]: ", 1);
						mEditorTags->at(mEditorTags->GetChoice()).GetEmptyFields(1);
						string new_tag = wFooter->GetString(mEditorTags->GetOption());
						mEditorTags->at(mEditorTags->GetChoice()).GetEmptyFields(0);
						UNLOCK_STATUSBAR;
						if (new_tag != mEditorTags->GetOption())
							(mEditorTags->at(mEditorTags->GetChoice()).*set)(new_tag);
						mEditorTags->Go(wDown);
					}
				}
#				endif // HAVE_TAGLIB_H
				default:
					break;
			}
		}
		else if (Keypressed(input, Key.Space))
		{
			if (Config.space_selects || wCurrent == mPlaylist)
			{
				if (wCurrent == mPlaylist || (wCurrent == mBrowser && wCurrent->GetChoice() > (browsed_dir != "/" ? 1 : 0)) || (wCurrent == mSearcher && !vSearched.empty() && wCurrent->GetChoice() > search_engine_static_option) || wCurrent == mLibSongs || wCurrent == mPlaylistEditor)
				{
					int i = wCurrent->GetChoice();
					wCurrent->Select(i, !wCurrent->Selected(i));
					wCurrent->Go(wDown);
				}
			}
			else
			{
				if (current_screen == csBrowser)
				{
					const Item &item = mBrowser->at(mBrowser->GetChoice());
					switch (item.type)
					{
						case itDirectory:
						{
							if (browsed_dir != "/" && !mBrowser->GetChoice())
								continue; // do not let add parent dir.
							
							SongList list;
							Mpd->GetDirectoryRecursive(item.name, list);
						
							for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
								Mpd->QueueAddSong(**it);
							if (Mpd->CommitQueue())
							{
								ShowMessage("Added folder: " + item.name);
								Song &s = mPlaylist->at(mPlaylist->Size()-list.size());
								if (s.GetHash() != list[0]->GetHash())
									ShowMessage(message_part_of_songs_added);
							}
							FreeSongList(list);
							break;
						}
						case itSong:
						{
							Song &s = *item.song;
							if (Mpd->AddSong(s) != -1)
								ShowMessage("Added to playlist: " + DisplaySong(s, &Config.song_status_format));
							break;
						}
						case itPlaylist:
						{
							SongList list;
							Mpd->GetPlaylistContent(item.name, list);
							for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
								Mpd->QueueAddSong(**it);
							if (Mpd->CommitQueue())
							{
								ShowMessage("Loading playlist " + item.name + "...");
								Song &s = mPlaylist->at(mPlaylist->Size()-list.size());
								if (s.GetHash() != list[0]->GetHash())
									ShowMessage(message_part_of_songs_added);
							}
							FreeSongList(list);
							break;
						}
					}
					mBrowser->Go(wDown);
				}
				else if (current_screen == csSearcher && !vSearched.empty())
				{
					int id = mSearcher->GetChoice()-search_engine_static_option;
					if (id < 0)
						continue;
				
					Song &s = *vSearched[id];
					if (Mpd->AddSong(s) != -1)
						ShowMessage("Added to playlist: " + DisplaySong(s, &Config.song_status_format));
					mSearcher->Go(wDown);
				}
				else if (current_screen == csLibrary)
					goto ENTER_LIBRARY_SCREEN; // sorry, but that's stupid to copy the same code here.
				else if (current_screen == csPlaylistEditor)
					goto ENTER_PLAYLIST_EDITOR_SCREEN; // same what in library screen.
			}
		}
		else if (Keypressed(input, Key.VolumeUp))
		{
			if (current_screen == csLibrary && input == Key.VolumeUp[0])
			{
				found_pos = 0;
				vFoundPositions.clear();
				if (wCurrent == mLibArtists)
				{
					mLibArtists->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mLibAlbums;
					mLibAlbums->HighlightColor(Config.active_column_color);
					if (!mLibAlbums->Empty())
						continue;
				}
				if (wCurrent == mLibAlbums)
				{
					mLibAlbums->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mLibSongs;
					mLibSongs->HighlightColor(Config.active_column_color);
				}
			}
			else if (wCurrent == mPlaylistList && input == Key.VolumeUp[0])
			{
				found_pos = 0;
				vFoundPositions.clear();
				mPlaylistList->HighlightColor(Config.main_highlight_color);
				wCurrent->Refresh();
				wCurrent = mPlaylistEditor;
				mPlaylistEditor->HighlightColor(Config.active_column_color);
			}
			else if (current_screen == csAlbumEditor && input == Key.VolumeUp[0])
			{
				found_pos = 0;
				vFoundPositions.clear();
				if (wCurrent == mEditorAlbums)
				{
					mEditorAlbums->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mEditorTagTypes;
					mEditorTagTypes->HighlightColor(Config.active_column_color);
				}
				else if (wCurrent == mEditorTagTypes && mEditorTagTypes->GetChoice() < 7)
				{
					mEditorTagTypes->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mEditorTags;
					mEditorTags->HighlightColor(Config.active_column_color);
				}
			}
			else
				Mpd->SetVolume(Mpd->GetVolume()+1);
		}
		else if (Keypressed(input, Key.VolumeDown))
		{
			if (current_screen == csLibrary && input == Key.VolumeDown[0])
			{
				found_pos = 0;
				vFoundPositions.clear();
				if (wCurrent == mLibSongs)
				{
					mLibSongs->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mLibAlbums;
					mLibAlbums->HighlightColor(Config.active_column_color);
					if (!mLibAlbums->Empty())
						continue;
				}
				if (wCurrent == mLibAlbums)
				{
					mLibAlbums->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mLibArtists;
					mLibArtists->HighlightColor(Config.active_column_color);
				}
			}
			else if (wCurrent == mPlaylistEditor && input == Key.VolumeDown[0])
			{
				found_pos = 0;
				vFoundPositions.clear();
				mPlaylistEditor->HighlightColor(Config.main_highlight_color);
				wCurrent->Refresh();
				wCurrent = mPlaylistList;
				mPlaylistList->HighlightColor(Config.active_column_color);
			}
			else if (current_screen == csAlbumEditor && input == Key.VolumeDown[0])
			{
				found_pos = 0;
				vFoundPositions.clear();
				if (wCurrent == mEditorTags)
				{
					mEditorTags->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mEditorTagTypes;
					mEditorTagTypes->HighlightColor(Config.active_column_color);
				}
				else if (wCurrent == mEditorTagTypes)
				{
					mEditorTagTypes->HighlightColor(Config.main_highlight_color);
					wCurrent->Refresh();
					wCurrent = mEditorAlbums;
					mEditorAlbums->HighlightColor(Config.active_column_color);
				}
			}
			else
				Mpd->SetVolume(Mpd->GetVolume()-1);
		}
		else if (Keypressed(input, Key.Delete))
		{
			if (!mPlaylist->Empty() && current_screen == csPlaylist)
			{
				if (mPlaylist->IsAnySelected())
				{
					vector<int> list;
					mPlaylist->GetSelectedList(list);
					for (vector<int>::const_reverse_iterator it = list.rbegin(); it != list.rend(); it++)
					{
						Mpd->QueueDeleteSong(*it);
						mPlaylist->DeleteOption(*it);
					}
					ShowMessage("Selected items deleted!");
					redraw_me = 1;
				}
				else
				{
					block_playlist_update = 1;
					dont_change_now_playing = 1;
					mPlaylist->SetTimeout(50);
					while (!mPlaylist->Empty() && Keypressed(input, Key.Delete))
					{
						TraceMpdStatus();
						timer = time(NULL);
						Mpd->QueueDeleteSong(mPlaylist->GetChoice());
						mPlaylist->DeleteOption(mPlaylist->GetChoice());
						mPlaylist->Refresh();
						mPlaylist->ReadKey(input);
					}
					mPlaylist->SetTimeout(ncmpcpp_window_timeout);
					dont_change_now_playing = 0;
				}
				Mpd->CommitQueue();
			}
			else if (current_screen == csBrowser || wCurrent == mPlaylistList)
			{
				LOCK_STATUSBAR;
				int id = wCurrent->GetChoice();
				const string &name = wCurrent == mBrowser ? mBrowser->at(id).name : mPlaylistList->at(id);
				if (current_screen != csBrowser || mBrowser->at(id).type == itPlaylist)
				{
					wFooter->WriteXY(0, Config.statusbar_visibility, "Delete playlist " + name + " ? [y/n] ", 1);
					curs_set(1);
					int in = 0;
					do
					{
						TraceMpdStatus();
						wFooter->ReadKey(in);
					}
					while (in != 'y' && in != 'n');
					if (in == 'y')
					{
						Mpd->DeletePlaylist(name);
						ShowMessage("Playlist " + name + " deleted!");
						GetDirectory("/");
					}
					else
						ShowMessage("Aborted!");
					curs_set(0);
					mPlaylistList->Clear(0); // make playlists list update itself
				}
				UNLOCK_STATUSBAR;
			}
			else if (wCurrent == mPlaylistEditor && !mPlaylistEditor->Empty())
			{
				if (mPlaylistEditor->IsAnySelected())
				{
					vector<int> list;
					mPlaylistEditor->GetSelectedList(list);
					for (vector<int>::const_reverse_iterator it = list.rbegin(); it != list.rend(); it++)
					{
						Mpd->QueueDeleteFromPlaylist(mPlaylistList->GetOption(), *it);
						mPlaylistEditor->DeleteOption(*it);
					}
					ShowMessage("Selected items deleted from playlist '" + mPlaylistList->GetOption() + "'!");
					redraw_me = 1;
				}
				else
				{
					mPlaylistEditor->SetTimeout(50);
					while (!mPlaylistEditor->Empty() && Keypressed(input, Key.Delete))
					{
						TraceMpdStatus();
						timer = time(NULL);
						Mpd->QueueDeleteFromPlaylist(mPlaylistList->GetOption(), mPlaylistEditor->GetChoice());
						mPlaylistEditor->DeleteOption(mPlaylistEditor->GetChoice());
						mPlaylistEditor->Refresh();
						mPlaylistEditor->ReadKey(input);
					}
					mPlaylistEditor->SetTimeout(ncmpcpp_window_timeout);
				}
				Mpd->CommitQueue();
			}
		}
		else if (Keypressed(input, Key.Prev))
		{
			Mpd->Prev();
		}
		else if (Keypressed(input, Key.Next))
		{
			Mpd->Next();
		}
		else if (Keypressed(input, Key.Pause))
		{
			Mpd->Pause();
		}
		else if (Keypressed(input, Key.SavePlaylist))
		{
			LOCK_STATUSBAR;
			wFooter->WriteXY(0, Config.statusbar_visibility, "Save playlist as: ", 1);
			string playlist_name = wFooter->GetString();
			UNLOCK_STATUSBAR;
			if (playlist_name.find("/") != string::npos)
			{
				ShowMessage("Playlist name cannot contain slashes!");
				continue;
			}
			if (!playlist_name.empty())
			{
				if (Mpd->SavePlaylist(playlist_name))
				{
					ShowMessage("Playlist saved as: " + playlist_name);
					mPlaylistList->Clear(0); // make playlist's list update itself
				}
				else
				{
					LOCK_STATUSBAR;
					wFooter->WriteXY(0, Config.statusbar_visibility, "Playlist already exists, overwrite: " + playlist_name + " ? [y/n] ", 1);
					curs_set(1);
					int in = 0;
					messages_allowed = 0;
					while (in != 'y' && in != 'n')
					{
						Mpd->UpdateStatus();
						wFooter->ReadKey(in);
					}
					messages_allowed = 1;
					
					if (in == 'y')
					{
						Mpd->DeletePlaylist(playlist_name);
						if (Mpd->SavePlaylist(playlist_name))
							ShowMessage("Playlist overwritten!");
					}
					else
						ShowMessage("Aborted!");
					curs_set(0);
					mPlaylistList->Clear(0); // make playlist's list update itself
					UNLOCK_STATUSBAR;
				}
			}
			if (browsed_dir == "/" && !mBrowser->Empty())
				GetDirectory(browsed_dir);
		}
		else if (Keypressed(input, Key.Stop))
		{
			Mpd->Stop();
		}
		else if (Keypressed(input, Key.MvSongUp))
		{
			if (current_screen == csPlaylist)
			{
				block_playlist_update = 1;
				mPlaylist->SetTimeout(50);
				if (mPlaylist->IsAnySelected())
				{
					vector<int> list;
					mPlaylist->GetSelectedList(list);
					
					for (vector<int>::iterator it = list.begin(); it != list.end(); it++)
						if (*it == now_playing && list.front() > 0)
							mPlaylist->BoldOption(now_playing, 0);
					
					vector<int>origs(list);
					
					while (Keypressed(input, Key.MvSongUp) && list.front() > 0)
					{
						TraceMpdStatus();
						timer = time(NULL);
						for (vector<int>::iterator it = list.begin(); it != list.end(); it++)
							mPlaylist->Swap(--*it, *it);
						mPlaylist->Highlight(list[(list.size()-1)/2]);
						mPlaylist->Refresh();
						mPlaylist->ReadKey(input);
					}
					for (int i = 0; i < list.size(); i++)
						Mpd->QueueMove(origs[i], list[i]);
					Mpd->CommitQueue();
				}
				else
				{
					int from, to;
					from = to = mPlaylist->GetChoice();
					while (Keypressed(input, Key.MvSongUp) && to > 0)
					{
						TraceMpdStatus();
						timer = time(NULL);
						mPlaylist->Swap(to--, to);
						mPlaylist->Go(wUp);
						mPlaylist->Refresh();
						mPlaylist->ReadKey(input);
					}
					Mpd->Move(from, to);
				}
				mPlaylist->SetTimeout(ncmpcpp_window_timeout);
			}
			else if (wCurrent == mPlaylistEditor)
			{
				mPlaylistEditor->SetTimeout(50);
				if (mPlaylistEditor->IsAnySelected())
				{
					vector<int> list;
					mPlaylistEditor->GetSelectedList(list);
					
					vector<int>origs(list);
					
					while (Keypressed(input, Key.MvSongUp) && list.front() > 0)
					{
						TraceMpdStatus();
						timer = time(NULL);
						for (vector<int>::iterator it = list.begin(); it != list.end(); it++)
							mPlaylistEditor->Swap(--*it, *it);
						mPlaylistEditor->Highlight(list[(list.size()-1)/2]);
						mPlaylistEditor->Refresh();
						mPlaylistEditor->ReadKey(input);
					}
					for (int i = 0; i < list.size(); i++)
						if (origs[i] != list[i])
							Mpd->QueueMove(mPlaylistList->GetOption(), origs[i], list[i]);
					Mpd->CommitQueue();
				}
				else
				{
					int from, to;
					from = to = mPlaylistEditor->GetChoice();
					while (Keypressed(input, Key.MvSongUp) && to > 0)
					{
						TraceMpdStatus();
						timer = time(NULL);
						mPlaylistEditor->Swap(to--, to);
						mPlaylistEditor->Go(wUp);
						mPlaylistEditor->Refresh();
						mPlaylistEditor->ReadKey(input);
					}
					if (from != to)
						Mpd->Move(mPlaylistList->GetOption(), from, to);
				}
				mPlaylistEditor->SetTimeout(ncmpcpp_window_timeout);
			}
		}
		else if (Keypressed(input, Key.MvSongDown))
		{
			if (current_screen == csPlaylist)
			{
				block_playlist_update = 1;
				mPlaylist->SetTimeout(50);
				if (mPlaylist->IsAnySelected())
				{
					vector<int> list;
					mPlaylist->GetSelectedList(list);
					
					for (vector<int>::iterator it = list.begin(); it != list.end(); it++)
						if (*it == now_playing && list.back() < mPlaylist->Size()-1)
							mPlaylist->BoldOption(now_playing, 0);
					
					vector<int>origs(list);
					
					while (Keypressed(input, Key.MvSongDown) && list.back() < mPlaylist->Size()-1)
					{
						TraceMpdStatus();
						timer = time(NULL);
						for (vector<int>::reverse_iterator it = list.rbegin(); it != list.rend(); it++)
							mPlaylist->Swap(++*it, *it);
						mPlaylist->Highlight(list[(list.size()-1)/2]);
						mPlaylist->Refresh();
						mPlaylist->ReadKey(input);
					}
					for (int i = list.size()-1; i >= 0; i--)
						Mpd->QueueMove(origs[i], list[i]);
					Mpd->CommitQueue();
				}
				else
				{
					int from, to;
					from = to = mPlaylist->GetChoice();
					while (Keypressed(input, Key.MvSongDown) && to < mPlaylist->Size()-1)
					{
						TraceMpdStatus();
						timer = time(NULL);
						mPlaylist->Swap(to++, to);
						mPlaylist->Go(wDown);
						mPlaylist->Refresh();
						mPlaylist->ReadKey(input);
					}
					Mpd->Move(from, to);
				}
				mPlaylist->SetTimeout(ncmpcpp_window_timeout);
				
			}
			else if (wCurrent == mPlaylistEditor)
			{
				mPlaylistEditor->SetTimeout(50);
				if (mPlaylistEditor->IsAnySelected())
				{
					vector<int> list;
					mPlaylistEditor->GetSelectedList(list);
					
					vector<int>origs(list);
					
					while (Keypressed(input, Key.MvSongDown) && list.back() < mPlaylistEditor->Size()-1)
					{
						TraceMpdStatus();
						timer = time(NULL);
						for (vector<int>::reverse_iterator it = list.rbegin(); it != list.rend(); it++)
							mPlaylistEditor->Swap(++*it, *it);
						mPlaylistEditor->Highlight(list[(list.size()-1)/2]);
						mPlaylistEditor->Refresh();
						mPlaylistEditor->ReadKey(input);
					}
					for (int i = list.size()-1; i >= 0; i--)
						if (origs[i] != list[i])
							Mpd->QueueMove(mPlaylistList->GetOption(), origs[i], list[i]);
					Mpd->CommitQueue();
				}
				else
				{
					int from, to;
					from = to = mPlaylistEditor->GetChoice();
					while (Keypressed(input, Key.MvSongDown) && to < mPlaylistEditor->Size()-1)
					{
						TraceMpdStatus();
						timer = time(NULL);
						mPlaylistEditor->Swap(to++, to);
						mPlaylistEditor->Go(wDown);
						mPlaylistEditor->Refresh();
						mPlaylistEditor->ReadKey(input);
					}
					if (from != to)
						Mpd->Move(mPlaylistList->GetOption(), from, to);
				}
				mPlaylistEditor->SetTimeout(ncmpcpp_window_timeout);
			}
		}
		else if (Keypressed(input, Key.Add))
		{
			LOCK_STATUSBAR;
			wFooter->WriteXY(0, Config.statusbar_visibility, "Add: ", 1);
			string path = wFooter->GetString();
			UNLOCK_STATUSBAR;
			if (!path.empty())
			{
				SongList list;
				Mpd->GetDirectoryRecursive(path, list);
				if (!list.empty())
				{
					for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
						Mpd->QueueAddSong(**it);
					if (Mpd->CommitQueue())
					{
						Song &s = mPlaylist->at(mPlaylist->Size()-list.size());
						if (s.GetHash() != list[0]->GetHash())
							ShowMessage(message_part_of_songs_added);
					}
				}
				else
					Mpd->AddSong(path);
				FreeSongList(list);
			}
		}
		else if (Keypressed(input, Key.SeekForward) || Keypressed(input, Key.SeekBackward))
		{
			if (now_playing < 0)
				continue;
			if (!mPlaylist->at(now_playing).GetTotalLength())
			{
				ShowMessage("Unknown item length!");
				continue;
			}
			block_progressbar_update = 1;
			LOCK_STATUSBAR;
			
			int songpos, in;
			
			songpos = Mpd->GetElapsedTime();
			Song &s = mPlaylist->at(now_playing);
			
			while (1)
			{
				TraceMpdStatus();
				timer = time(NULL);
				mPlaylist->ReadKey(in);
				if (Keypressed(in, Key.SeekForward) || Keypressed(in, Key.SeekBackward))
				{
					if (songpos < s.GetTotalLength() && Keypressed(in, Key.SeekForward))
						songpos++;
					if (songpos < s.GetTotalLength() && songpos > 0 && Keypressed(in, Key.SeekBackward))
						songpos--;
					if (songpos < 0)
						songpos = 0;
					
					wFooter->Bold(1);
					string tracklength = "[" + ShowTime(songpos) + "/" + s.GetLength() + "]";
					wFooter->WriteXY(wFooter->GetWidth()-tracklength.length(), 1, tracklength);
					double progressbar_size = (double)songpos/(s.GetTotalLength());
					int howlong = wFooter->GetWidth()*progressbar_size;
					
					mvwhline(wFooter->RawWin(), 0, 0, 0, wFooter->GetWidth());
					mvwhline(wFooter->RawWin(), 0, 0, '=',howlong);
					mvwaddch(wFooter->RawWin(), 0, howlong, '>');
					wFooter->Bold(0);
					wFooter->Refresh();
				}
				else
					break;
			}
			Mpd->Seek(songpos);
			
			block_progressbar_update = 0;
			UNLOCK_STATUSBAR;
		}
		else if (Keypressed(input, Key.TogglePlaylistDisplayMode) && wCurrent == mPlaylist)
		{
			Config.columns_in_playlist = !Config.columns_in_playlist;
			ShowMessage("Playlist display mode: " + string(Config.columns_in_playlist ? "Columns" : "Classic"));
			mPlaylist->SetItemDisplayer(Config.columns_in_playlist ? DisplaySongInColumns : DisplaySong);
			mPlaylist->SetItemDisplayerUserData(Config.columns_in_playlist ? &Config.song_columns_list_format : &Config.song_list_format);
			mPlaylist->SetTitle(Config.columns_in_playlist ? DisplayColumns(Config.song_columns_list_format) : "");
			redraw_me = 1;
		}
		else if (Keypressed(input, Key.ToggleAutoCenter))
		{
			Config.autocenter_mode = !Config.autocenter_mode;
			ShowMessage("Auto center mode: " + string(Config.autocenter_mode ? "On" : "Off"));
		}
		else if (Keypressed(input, Key.UpdateDB))
		{
			if (current_screen == csBrowser)
				Mpd->UpdateDirectory(browsed_dir);
			else
				Mpd->UpdateDirectory("/");
		}
		else if (Keypressed(input, Key.GoToNowPlaying))
		{
			if (current_screen == csPlaylist && now_playing >= 0)
				mPlaylist->Highlight(now_playing);
		}
		else if (Keypressed(input, Key.ToggleRepeat))
		{
			Mpd->SetRepeat(!Mpd->GetRepeat());
		}
		else if (Keypressed(input, Key.ToggleRepeatOne))
		{
			Config.repeat_one_mode = !Config.repeat_one_mode;
			ShowMessage("'Repeat one' mode: " + string(Config.repeat_one_mode ? "On" : "Off"));
		}
		else if (Keypressed(input, Key.Shuffle))
		{
			Mpd->Shuffle();
		}
		else if (Keypressed(input, Key.ToggleRandom))
		{
			Mpd->SetRandom(!Mpd->GetRandom());
		}
		else if (Keypressed(input, Key.ToggleCrossfade))
		{
			Mpd->SetCrossfade(Mpd->GetCrossfade() ? 0 : Config.crossfade_time);
		}
		else if (Keypressed(input, Key.SetCrossfade))
		{
			LOCK_STATUSBAR;
			wFooter->WriteXY(0, Config.statusbar_visibility, "Set crossfade to: ", 1);
			string crossfade = wFooter->GetString(3);
			UNLOCK_STATUSBAR;
			int cf = StrToInt(crossfade);
			if (cf > 0)
			{
				Config.crossfade_time = cf;
				Mpd->SetCrossfade(cf);
			}
		}
		else if (Keypressed(input, Key.EditTags))
		{
#			ifdef HAVE_TAGLIB_H
			if (wCurrent == mLibArtists)
			{
				LOCK_STATUSBAR;
				wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Artist:[/b] ", 1);
				string new_artist = wFooter->GetString(mLibArtists->GetOption());
				UNLOCK_STATUSBAR;
				if (!new_artist.empty() && new_artist != mLibArtists->GetOption())
				{
					bool success = 1;
					SongList list;
					Mpd->StartSearch(1);
					Mpd->AddSearch(MPD_TAG_ITEM_ARTIST, mLibArtists->GetOption());
					Mpd->CommitSearch(list);
					for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
					{
						string path = Config.mpd_music_dir + "/" + (*it)->GetFile();
						TagLib::FileRef f(path.c_str());
						if (f.isNull())
						{
							success = 0;
							break;
						}
						f.tag()->setArtist(TO_WSTRING(new_artist));
						f.save();
					}
					if (success)
						Mpd->UpdateDirectory("/");
					FreeSongList(list);
					ShowMessage(success ? "Tags written succesfully!" : "Error while writing tags!");
				}
			}
			else if (wCurrent == mLibAlbums)
			{
				LOCK_STATUSBAR;
				wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Album:[/b] ", 1);
				string new_album = wFooter->GetString(vLibAlbums[mLibAlbums->GetOption()]);
				UNLOCK_STATUSBAR;
				if (!new_album.empty() && new_album != vLibAlbums[mLibAlbums->GetOption()])
				{
					bool success = 1;
					SongList list;
					Mpd->StartSearch(1);
					Mpd->AddSearch(MPD_TAG_ITEM_ARTIST, mLibArtists->GetOption());
					Mpd->AddSearch(MPD_TAG_ITEM_ALBUM, vLibAlbums[mLibAlbums->GetOption()]);
					Mpd->CommitSearch(list);
					for (SongList::const_iterator it = list.begin(); it != list.end(); it++)
					{
						string path = Config.mpd_music_dir + "/" + (*it)->GetFile();
						TagLib::FileRef f(path.c_str());
						if (f.isNull())
						{
							success = 0;
							break;
						}
						f.tag()->setAlbum(TO_WSTRING(new_album));
						f.save();
					}
					if (success)
						Mpd->UpdateDirectory("/");
					FreeSongList(list);
					ShowMessage(success ? "Tags written succesfully!" : "Error while writing tags!");
				}
			}
			else
#			endif
			if ((wCurrent == mPlaylist && !mPlaylist->Empty())
			||  (wCurrent == mBrowser && mBrowser->at(mBrowser->GetChoice()).type == itSong)
			||  (wCurrent == mSearcher && !vSearched.empty() && mSearcher->GetChoice() >= search_engine_static_option)
			||  (wCurrent == mLibSongs && !mLibSongs->Empty())
			||  (wCurrent == mPlaylistEditor && !mPlaylistEditor->Empty()))
			{
				int id = wCurrent->GetChoice();
				Song *s;
				switch (current_screen)
				{
					case csPlaylist:
						s = &mPlaylist->at(id);
						break;
					case csBrowser:
						s = mBrowser->at(id).song;
						break;
					case csSearcher:
						s = vSearched[id-search_engine_static_option];
						break;
					case csLibrary:
						s = &mLibSongs->at(id);
						break;
					case csPlaylistEditor:
						s = &mPlaylistEditor->at(id);
						break;
					default:
						break;
				}
				if (GetSongInfo(*s))
				{
					wPrev = wCurrent;
					wCurrent = mTagEditor;
					prev_screen = current_screen;
					current_screen = csTagEditor;
				}
				else
					ShowMessage("Cannot read file '" + Config.mpd_music_dir + "/" + s->GetFile() + "'!");
			}
			else if (wCurrent == mPlaylistList)
			{
				LOCK_STATUSBAR;
				wFooter->WriteXY(0, Config.statusbar_visibility, "[.b]Playlist:[/b] ", 1);
				string new_name = wFooter->GetString(mPlaylistList->GetOption());
				UNLOCK_STATUSBAR;
				if (!new_name.empty() && new_name != mPlaylistList->GetOption())
				{
					Mpd->Rename(mPlaylistList->GetOption(), new_name);
					ShowMessage("Playlist '" + mPlaylistList->GetOption() + "' renamed to '" + new_name + "'");
					mPlaylistList->Clear(0);
				}
			}
		}
		else if (Keypressed(input, Key.GoToContainingDir))
		{
			if ((wCurrent == mPlaylist && !mPlaylist->Empty())
			|| (wCurrent == mSearcher && !vSearched.empty() && mSearcher->GetChoice() >= search_engine_static_option)
			|| (wCurrent == mLibSongs && !mLibSongs->Empty())
			|| (wCurrent == mPlaylistEditor && !mPlaylistEditor->Empty()))
			{
				int id = wCurrent->GetChoice();
				Song *s;
				switch (current_screen)
				{
					case csPlaylist:
						s = &mPlaylist->at(id);
						break;
					case csSearcher:
						s = vSearched[id-search_engine_static_option];
						break;
					case csLibrary:
						s = &mLibSongs->at(id);
						break;
					case csPlaylistEditor:
						s = &mPlaylistEditor->at(id);
						break;
					default:
						break;
				}
				
				if (s->GetDirectory() == EMPTY_TAG) // for streams
					continue;
				
				string option = DisplaySong(*s);
				GetDirectory(s->GetDirectory());
				for (int i = 0; i < mBrowser->Size(); i++)
				{
					if (option == mBrowser->GetOption(i))
					{
						mBrowser->Highlight(i);
						break;
					}
				}
				goto SWITCHER_BROWSER_REDIRECT;
			}
		}
		else if (Keypressed(input, Key.StartSearching))
		{
			if (wCurrent == mSearcher)
			{
				mSearcher->Highlight(12); // highlight 'search' button
				goto ENTER_SEARCH_ENGINE_SCREEN;
			}
		}
		else if (Keypressed(input, Key.GoToPosition))
		{
			if (now_playing < 0)
				continue;
			if (!mPlaylist->at(now_playing).GetTotalLength())
			{
				ShowMessage("Unknown item length!");
				continue;
			}
			LOCK_STATUSBAR;
			wFooter->WriteXY(0, Config.statusbar_visibility, "Position to go (in %): ", 1);
			string position = wFooter->GetString(3);
			int newpos = StrToInt(position);
			if (newpos > 0 && newpos < 100 && !position.empty())
				Mpd->Seek(mPlaylist->at(now_playing).GetTotalLength()*newpos/100.0);
			UNLOCK_STATUSBAR;
		}
		else if (Keypressed(input, Key.ReverseSelection))
		{
			if (wCurrent == mPlaylist || wCurrent == mBrowser || (wCurrent == mSearcher && !vSearched.empty()) || wCurrent == mLibSongs || wCurrent == mPlaylistEditor)
			{
				for (int i = 0; i < wCurrent->Size(); i++)
					wCurrent->Select(i, !wCurrent->Selected(i) && !wCurrent->IsStatic(i));
				// hackish shit begins
				if (wCurrent == mBrowser && browsed_dir != "/")
					wCurrent->Select(0, 0); // [..] cannot be selected, uhm.
				if (wCurrent == mSearcher)
					wCurrent->Select(13, 0); // 'Reset' cannot be selected, omgplz.
				// hacking shit ends. need better solution :/
				ShowMessage("Selection reversed!");
			}
		}
		else if (Keypressed(input, Key.DeselectAll))
		{
			if (wCurrent == mPlaylist || wCurrent == mBrowser || wCurrent == mSearcher || wCurrent == mLibSongs || wCurrent == mPlaylistEditor)
			{
				if (wCurrent->IsAnySelected())
				{
					for (int i = 0; i < wCurrent->Size(); i++)
						wCurrent->Select(i, 0);
					ShowMessage("Items deselected!");
				}
			}
		}
		else if (Keypressed(input, Key.AddSelected))
		{
			if (wCurrent == mPlaylist || wCurrent == mBrowser || wCurrent == mSearcher || wCurrent == mLibSongs || wCurrent == mPlaylistEditor)
			{
				if (wCurrent->IsAnySelected())
				{
					vector<int> list;
					wCurrent->GetSelectedList(list);
					SongList result;
					for (vector<int>::const_iterator it = list.begin(); it != list.end(); it++)
					{
						switch (current_screen)
						{
							case csPlaylist:
							{
								Song *s = new Song(mPlaylist->at(*it));
								result.push_back(s);
								break;
							}
							case csBrowser:
							{
								const Item &item = mBrowser->at(*it);
								switch (item.type)
								{
									case itDirectory:
									{
										if (browsed_dir != "/")
										Mpd->GetDirectoryRecursive(browsed_dir + "/" + item.name, result);
										else
										Mpd->GetDirectoryRecursive(item.name, result);
										break;
									}
									case itSong:
									{
										Song *s = new Song(*item.song);
										result.push_back(s);
										break;
									}
									case itPlaylist:
									{
										Mpd->GetPlaylistContent(item.name, result);
										break;
									}
								}
								break;
							}
							case csSearcher:
							{
								Song *s = new Song(*vSearched[*it-search_engine_static_option]);
								result.push_back(s);
								break;
							}
							case csLibrary:
							{
								Song *s = new Song(mLibSongs->at(*it));
								result.push_back(s);
								break;
							}
							case csPlaylistEditor:
							{
								Song *s = new Song(mPlaylistEditor->at(*it));
								result.push_back(s);
								break;
							}
							default:
								break;
						}
					}
					
					const int dialog_width = COLS*0.8;
					const int dialog_height = LINES*0.6;
					Menu<string> *mDialog = new Menu<string>((COLS-dialog_width)/2, (LINES-dialog_height)/2, dialog_width, dialog_height, "Add selected items to...", clYellow, brGreen);
					mDialog->SetTimeout(ncmpcpp_window_timeout);
					
					mDialog->AddOption("Current MPD playlist");
					mDialog->AddOption("Create new playlist (m3u file)");
					mDialog->AddSeparator();
					TagList playlists;
					Mpd->GetPlaylists(playlists);
					for (TagList::const_iterator it = playlists.begin(); it != playlists.end(); it++)
						mDialog->AddOption("'" + *it + "' playlist");
					mDialog->AddSeparator();
					mDialog->AddOption("Cancel");
					
					mDialog->Display();
					
					while (!Keypressed(input, Key.Enter))
					{
						mDialog->Refresh();
						mDialog->ReadKey(input);
						
						if (Keypressed(input, Key.Up))
							mDialog->Go(wUp);
						else if (Keypressed(input, Key.Down))
							mDialog->Go(wDown);
						else if (Keypressed(input, Key.PageUp))
							mDialog->Go(wPageUp);
						else if (Keypressed(input, Key.PageDown))
							mDialog->Go(wPageDown);
						else if (Keypressed(input, Key.Home))
							mDialog->Go(wHome);
						else if (Keypressed(input, Key.End))
							mDialog->Go(wEnd);
					}
					
					int id = mDialog->GetChoice();
					
					redraw_me = 1;
					if (current_screen == csLibrary)
					{
						REFRESH_MEDIA_LIBRARY_SCREEN;
					}
					else if (current_screen == csPlaylistEditor)
					{
						REFRESH_PLAYLIST_EDITOR_SCREEN;
					}
					else
						wCurrent->Refresh(1);
					
					if (id == 0)
					{
						for (SongList::const_iterator it = result.begin(); it != result.end(); it++)
							Mpd->QueueAddSong(**it);
						if (Mpd->CommitQueue())
						{
							ShowMessage("Selected items added!");
							Song &s = mPlaylist->at(mPlaylist->Size()-result.size());
							if (s.GetHash() != result[0]->GetHash())
								ShowMessage(message_part_of_songs_added);
						}
					}
					else if (id == 1)
					{
						LOCK_STATUSBAR;
						wFooter->WriteXY(0, Config.statusbar_visibility, "Save playlist as: ", 1);
						string playlist = wFooter->GetString();
						UNLOCK_STATUSBAR;
						if (!playlist.empty())
						{
							for (SongList::const_iterator it = result.begin(); it != result.end(); it++)
								Mpd->QueueAddToPlaylist(playlist, **it);
							Mpd->CommitQueue();
							ShowMessage("Selected items added to playlist '" + playlist + "'!");
						}
						
					}
					else if (id > 1 && id < mDialog->Size()-1)
					{
						for (SongList::const_iterator it = result.begin(); it != result.end(); it++)
							Mpd->QueueAddToPlaylist(playlists[id-3], **it);
						Mpd->CommitQueue();
						ShowMessage("Selected items added to playlist '" + playlists[id-3] + "'!");
					}
					
					if (id != mDialog->Size()-1)
					{
						// refresh playlist's lists
						if (browsed_dir == "/")
							GetDirectory("/");
						mPlaylistList->Clear(0); // make playlist editor update itself
					}
					delete mDialog;
					FreeSongList(result);
				}
				else
					ShowMessage("No selected items!");
			}
		}
		else if (Keypressed(input, Key.Crop))
		{
			if (mPlaylist->IsAnySelected())
			{
				for (int i = 0; i < mPlaylist->Size(); i++)
				{
					if (!mPlaylist->Selected(i) && i != now_playing)
						Mpd->QueueDeleteSongId(mPlaylist->at(i).GetID());
				}
				// if mpd deletes now playing song deletion will be sluggishly slow
				// then so we have to assure it will be deleted at the very end.
				if (!mPlaylist->Selected(now_playing))
					Mpd->QueueDeleteSongId(mPlaylist->at(now_playing).GetID());
				
				ShowMessage("Deleting all items but selected...");
				Mpd->CommitQueue();
				ShowMessage("Items deleted!");
			}
			else
			{
				if (now_playing < 0)
				{
					ShowMessage("Nothing is playing now!");
					continue;
				}
				for (int i = 0; i < now_playing; i++)
					Mpd->QueueDeleteSongId(mPlaylist->at(i).GetID());
				for (int i = now_playing+1; i < mPlaylist->Size(); i++)
					Mpd->QueueDeleteSongId(mPlaylist->at(i).GetID());
				ShowMessage("Deleting all items except now playing one...");
				Mpd->CommitQueue();
				ShowMessage("Items deleted!");
			}
		}
		else if (Keypressed(input, Key.Clear))
		{
			ShowMessage("Clearing playlist...");
			Mpd->ClearPlaylist();
			ShowMessage("Cleared playlist!");
		}
		else if (Keypressed(input, Key.FindForward) || Keypressed(input, Key.FindBackward))
		{
			if ((current_screen != csHelp && current_screen != csSearcher)
			||  (current_screen == csSearcher && !vSearched.empty()))
			{
				string how = Keypressed(input, Key.FindForward) ? "forward" : "backward";
				found_pos = -1;
				vFoundPositions.clear();
				LOCK_STATUSBAR;
				wFooter->WriteXY(0, Config.statusbar_visibility, "Find " + how + ": ", 1);
				string findme = wFooter->GetString();
				UNLOCK_STATUSBAR;
				timer = time(NULL);
				if (findme.empty())
					continue;
				transform(findme.begin(), findme.end(), findme.begin(), tolower);
				
				ShowMessage("Searching...");
				for (int i = (wCurrent == mSearcher ? search_engine_static_option-1 : 0); i < wCurrent->Size(); i++)
				{
					string name = Window::OmitBBCodes(wCurrent->GetOption(i));
					transform(name.begin(), name.end(), name.begin(), tolower);
					if (name.find(findme) != string::npos && !wCurrent->IsStatic(i))
					{
						vFoundPositions.push_back(i);
						if (Keypressed(input, Key.FindForward)) // forward
						{
							if (found_pos < 0 && i >= wCurrent->GetChoice())
								found_pos = vFoundPositions.size()-1;
						}
						else // backward
						{
							if (i <= wCurrent->GetChoice())
								found_pos = vFoundPositions.size()-1;
						}
					}
				}
				ShowMessage("Searching finished!");
				
				if (Config.wrapped_search ? vFoundPositions.empty() : found_pos < 0)
					ShowMessage("Unable to find \"" + findme + "\"");
				else
				{
					wCurrent->Highlight(vFoundPositions[found_pos < 0 ? 0 : found_pos]);
					if (wCurrent == mPlaylist)
					{
						timer = time(NULL);
						mPlaylist->Highlighting(1);
					}
				}
			}
		}
		else if (Keypressed(input, Key.NextFoundPosition) || Keypressed(input, Key.PrevFoundPosition))
		{
			if (!vFoundPositions.empty())
			{
				try
				{
					wCurrent->Highlight(vFoundPositions.at(Keypressed(input, Key.NextFoundPosition) ? ++found_pos : --found_pos));
				}
				catch (std::out_of_range)
				{
					if (Config.wrapped_search)
					{
						if (Keypressed(input, Key.NextFoundPosition))
						{
							wCurrent->Highlight(vFoundPositions.front());
							found_pos = 0;
						}
						else
						{
							wCurrent->Highlight(vFoundPositions.back());
							found_pos = vFoundPositions.size()-1;
						}
					}
					else
						found_pos = Keypressed(input, Key.NextFoundPosition) ? vFoundPositions.size()-1 : 0;
				}
			}
		}
		else if (Keypressed(input, Key.ToggleFindMode))
		{
			Config.wrapped_search = !Config.wrapped_search;
			ShowMessage("Search mode: " + string(Config.wrapped_search ? "Wrapped" : "Normal"));
		}
		else if (Keypressed(input, Key.ToggleSpaceMode))
		{
			Config.space_selects = !Config.space_selects;
			ShowMessage("Space mode: " + string(Config.space_selects ? "Select/deselect" : "Add") + " item");
		}
		else if (Keypressed(input, Key.Lyrics))
		{
			if (wCurrent == sLyrics)
			{
				wCurrent->Window::Clear();
				current_screen = prev_screen;
				wCurrent = wPrev;
				redraw_me = 1;
				if (current_screen == csLibrary)
				{
					REFRESH_MEDIA_LIBRARY_SCREEN;
				}
				else if (current_screen == csPlaylistEditor)
				{
					REFRESH_PLAYLIST_EDITOR_SCREEN;
				}
			}
			else if (
			    (wCurrent == mPlaylist && !mPlaylist->Empty())
			||  (wCurrent == mBrowser && mBrowser->at(mBrowser->GetChoice()).type == itSong)
			||  (wCurrent == mSearcher && !vSearched.empty() && mSearcher->GetChoice() >= search_engine_static_option)
			||  (wCurrent == mLibSongs && !mLibSongs->Empty())
			||  (wCurrent == mPlaylistEditor && !mPlaylistEditor->Empty()))
			{
				Song *s;
				int id = wCurrent->GetChoice();
				switch (current_screen)
				{
					case csPlaylist:
						s = &mPlaylist->at(id);
						break;
					case csBrowser:
						s = mBrowser->at(id).song;
						break;
					case csSearcher:
						s = vSearched[id-search_engine_static_option]; // first one is 'Reset'
						break;
					case csLibrary:
						s = &mLibSongs->at(id);
						break;
					case csPlaylistEditor:
						s = &mPlaylistEditor->at(id);
						break;
					default:
						break;
				}
				if (s->GetArtist() != UNKNOWN_ARTIST && s->GetTitle() != UNKNOWN_TITLE)
				{
					wPrev = wCurrent;
					prev_screen = current_screen;
					wCurrent = sLyrics;
					wCurrent->Clear();
					current_screen = csLyrics;
					song_lyrics = "Lyrics: " + s->GetArtist() + " - " + s->GetTitle();
					sLyrics->WriteXY(0, 0, "Fetching lyrics...");
					sLyrics->Refresh();
					sLyrics->Add(GetLyrics(s->GetArtist(), s->GetTitle()));
				}
			}
		}
		else if (Keypressed(input, Key.Help))
		{
			if (wCurrent != sHelp)
			{
				wCurrent = sHelp;
				wCurrent->Hide();
				current_screen = csHelp;
			}
		}
		else if (Keypressed(input, Key.ScreenSwitcher))
		{
			if (wCurrent == mPlaylist)
				goto SWITCHER_BROWSER_REDIRECT;
			else
				goto SWITCHER_PLAYLIST_REDIRECT;
		}
		else if (Keypressed(input, Key.Playlist))
		{
			SWITCHER_PLAYLIST_REDIRECT:
			if (wCurrent != mPlaylist && current_screen != csTagEditor)
			{
				found_pos = 0;
				vFoundPositions.clear();
				wCurrent = mPlaylist;
				wCurrent->Hide();
				current_screen = csPlaylist;
				redraw_me = 1;
			}
		}
		else if (Keypressed(input, Key.Browser))
		{
			SWITCHER_BROWSER_REDIRECT:
			if (browsed_dir.empty())
				browsed_dir = "/";
			
			mBrowser->Empty() ? GetDirectory(browsed_dir) : UpdateItemList(mBrowser);
			
			if (wCurrent != mBrowser && current_screen != csTagEditor)
			{
				found_pos = 0;
				vFoundPositions.clear();
				wCurrent = mBrowser;
				wCurrent->Hide();
				current_screen = csBrowser;
				redraw_me = 1;
			}
		}
		else if (Keypressed(input, Key.SearchEngine))
		{
			if (current_screen != csTagEditor && current_screen != csSearcher)
			{
				found_pos = 0;
				vFoundPositions.clear();
				if (vSearched.empty())
					PrepareSearchEngine(searched_song);
				wCurrent = mSearcher;
				wCurrent->Hide();
				current_screen = csSearcher;
				redraw_me = 1;
				if (!vSearched.empty())
				{
					wCurrent->WriteXY(0, 0, "Updating list...");
					UpdateFoundList(vSearched, mSearcher);
				}
			}
		}
		else if (Keypressed(input, Key.MediaLibrary))
		{
			if (current_screen != csLibrary)
			{
				found_pos = 0;
				vFoundPositions.clear();
				
				mLibArtists->HighlightColor(Config.active_column_color);
				mLibAlbums->HighlightColor(Config.main_highlight_color);
				mLibSongs->HighlightColor(Config.main_highlight_color);
				
				mPlaylist->Hide(); // hack, should be wCurrent, but it doesn't always have 100% width
				
				redraw_me = 1;
				REFRESH_MEDIA_LIBRARY_SCREEN;
				
				wCurrent = mLibArtists;
				current_screen = csLibrary;
				
				UpdateSongList(mLibSongs);
			}
		}
		else if (Keypressed(input, Key.PlaylistEditor))
		{
			if (current_screen != csPlaylistEditor)
			{
				found_pos = 0;
				vFoundPositions.clear();
				
				mPlaylistList->HighlightColor(Config.active_column_color);
				mPlaylistEditor->HighlightColor(Config.main_highlight_color);
				
				mPlaylist->Hide(); // hack, should be wCurrent, but it doesn't always have 100% width
				
				redraw_me = 1;
				REFRESH_PLAYLIST_EDITOR_SCREEN;
				
				wCurrent = mPlaylistList;
				current_screen = csPlaylistEditor;
				
				UpdateSongList(mPlaylistEditor);
			}
		}
		else if (Keypressed(input, Key.AlbumEditor))
		{
			if (current_screen != csAlbumEditor)
			{
				found_pos = 0;
				vFoundPositions.clear();
				
				mEditorAlbums->HighlightColor(Config.active_column_color);
				mEditorTagTypes->HighlightColor(Config.main_highlight_color);
				mEditorTags->HighlightColor(Config.main_highlight_color);
				
				mPlaylist->Hide(); // hack, should be wCurrent, but it doesn't always have 100% width
				
				redraw_me = 1;
				REFRESH_ALBUM_EDITOR_SCREEN;
				
				if (mEditorTagTypes->Empty())
				{
					mEditorTagTypes->AddOption("Title");
					mEditorTagTypes->AddOption("Artist");
					mEditorTagTypes->AddOption("Album");
					mEditorTagTypes->AddOption("Year");
					mEditorTagTypes->AddOption("Track");
					mEditorTagTypes->AddOption("Genre");
					mEditorTagTypes->AddOption("Comment");
					mEditorTagTypes->AddSeparator();
					mEditorTagTypes->AddOption("Reset");
					mEditorTagTypes->AddOption("Save");
				}
				
				wCurrent = mEditorAlbums;
				current_screen = csAlbumEditor;
			}
		}
		else if (Keypressed(input, Key.Quit))
			main_exit = 1;
		
		// key mapping end
	}
	Mpd->Disconnect();
	curs_set(1);
	endwin();
	printf("\n");
	return 0;
}

