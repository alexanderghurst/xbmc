/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "GUIDialogMusicInfo.h"
#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogFileBrowser.h"
#include "dialogs/GUIDialogSelect.h"
#include "GUIPassword.h"
#include "GUIUserMessages.h"
#include "music/MusicDatabase.h"
#include "music/tags/MusicInfoTag.h"
#include "filesystem/File.h"
#include "FileItem.h"
#include "profiles/ProfilesManager.h"
#include "storage/MediaManager.h"
#include "settings/MediaSourceSettings.h"
#include "input/Key.h"
#include "guilib/LocalizeStrings.h"
#include "utils/URIUtils.h"
#include "utils/StringUtils.h"
#include "TextureCache.h"
#include "music/MusicThumbLoader.h"
#include "music/windows/GUIWindowMusicNav.h"
#include "filesystem/Directory.h"

using namespace XFILE;

#define CONTROL_BTN_REFRESH      6
#define CONTROL_USERRATING       7
#define CONTROL_BTN_GET_THUMB   10
#define CONTROL_BTN_GET_FANART  12

#define CONTROL_LIST            50

CGUIDialogMusicInfo::CGUIDialogMusicInfo(void)
    : CGUIDialog(WINDOW_DIALOG_MUSIC_INFO, "DialogMusicInfo.xml")
      , m_albumItem(new CFileItem)
{
  m_bRefresh = false;
  m_albumSongs = new CFileItemList;
  m_loadType = KEEP_IN_MEMORY;
  m_startUserrating = -1;
  m_needsUpdate = false;
  m_bViewReview = false;
  m_hasUpdatedThumb = false; 
  m_bArtistInfo = false;
}

CGUIDialogMusicInfo::~CGUIDialogMusicInfo(void)
{
  delete m_albumSongs;
}

bool CGUIDialogMusicInfo::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_WINDOW_DEINIT:
    {
      if (m_startUserrating != m_albumItem->GetMusicInfoTag()->GetUserrating())
      {
        CMusicDatabase db;
        if (db.Open())
        {
          m_needsUpdate = true;
          db.SetAlbumUserrating(m_albumItem->GetMusicInfoTag()->GetAlbumId(), m_albumItem->GetMusicInfoTag()->GetUserrating());
          db.Close();
        }
      }

      CGUIMessage msg(GUI_MSG_LABEL_RESET, GetID(), CONTROL_LIST);
      OnMessage(msg);
      m_albumSongs->Clear();
    }
    break;

  case GUI_MSG_WINDOW_INIT:
    {
      CGUIDialog::OnMessage(message);
      m_bViewReview = true;
      m_bRefresh = false;
      Update();
      return true;
    }
    break;


  case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      if (iControl == CONTROL_USERRATING)
      {
        OnSetUserrating();
      }
      else if (iControl == CONTROL_BTN_REFRESH)
      {
        m_bRefresh = true;
        Close();
        return true;
      }
      else if (iControl == CONTROL_BTN_GET_THUMB)
      {
        OnGetThumb();
      }
      else if (iControl == CONTROL_LIST)
      {
        int iAction = message.GetParam1();
        if (m_bArtistInfo && (ACTION_SELECT_ITEM == iAction || ACTION_MOUSE_LEFT_CLICK == iAction))
        {
          CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), iControl);
          g_windowManager.SendMessage(msg);
          int iItem = msg.GetParam1();
          if (iItem < 0 || iItem >= static_cast<int>(m_albumSongs->Size()))
            break;
          CFileItemPtr item = m_albumSongs->Get(iItem);
          OnSearch(item.get());
          return true;
        }
      }
      else if (iControl == CONTROL_BTN_GET_FANART)
      {
        OnGetFanart();
      }
    }
    break;
  }

  return CGUIDialog::OnMessage(message);
}

bool CGUIDialogMusicInfo::OnAction(const CAction &action)
{
  int userrating = m_albumItem->GetMusicInfoTag()->GetUserrating();
  if (action.GetID() == ACTION_INCREASE_RATING)
  {
    SetUserrating(userrating + 1);
    return true;
  }
  else if (action.GetID() == ACTION_DECREASE_RATING)
  {
    SetUserrating(userrating - 1);
    return true;
  }
  else if (action.GetID() == ACTION_SHOW_INFO)
  {
    Close();
    return true;
  }
  return CGUIDialog::OnAction(action);
}

void CGUIDialogMusicInfo::SetAlbum(const CAlbum& album, const std::string &path)
{
  m_album = album;
  SetSongs(m_album.songs);
  *m_albumItem = CFileItem(path, true);
  m_albumItem->GetMusicInfoTag()->SetAlbum(m_album);
  CMusicDatabase::SetPropertiesFromAlbum(*m_albumItem, m_album);

  // Load all album and related artist art (to CGUIListItem.m_art)
  // This includes artist fanart set as fallback album fanart
  CMusicThumbLoader loader;
  loader.LoadItem(m_albumItem.get());

  m_startUserrating = m_album.iUserrating;
  m_hasUpdatedThumb = false;
  m_bArtistInfo = false;
  m_needsUpdate = false;

  // CurrentDirectory() returns m_albumSongs (a convenient CFileItemList)
  // Set content so can return dialog CONTAINER_CONTENT as "albums"
  m_albumSongs->SetContent("albums");
  // Copy art from ListItem so CONTAINER_ART returns album art
  m_albumSongs->SetArt(m_albumItem->GetArt());
}

void CGUIDialogMusicInfo::SetArtist(const CArtist& artist, const std::string &path)
{
  m_artist = artist;
  SetDiscography();
  *m_albumItem = CFileItem(path, true);
  m_albumItem->SetLabel(artist.strArtist);
  m_albumItem->GetMusicInfoTag()->SetAlbumArtist(m_artist.strArtist);
  m_albumItem->GetMusicInfoTag()->SetArtist(m_artist.strArtist);
  m_albumItem->GetMusicInfoTag()->SetLoaded(true);
  m_albumItem->GetMusicInfoTag()->SetGenre(m_artist.genre);
  m_albumItem->GetMusicInfoTag()->SetDatabaseId(m_artist.idArtist, MediaTypeArtist);
  CMusicDatabase::SetPropertiesFromArtist(*m_albumItem,m_artist);

  CMusicThumbLoader loader;
  loader.LoadItem(m_albumItem.get());

  m_hasUpdatedThumb = false;
  m_bArtistInfo = true;

  // CurrentDirectory() returns m_albumSongs (a convenient CFileItemList)
  // Set content so can return dialog CONTAINER_CONTENT as "artists"
  m_albumSongs->SetContent("artists"); 
  // Copy art from ListItem so CONTAINER_ART returns artist art
  m_albumSongs->SetArt(m_albumItem->GetArt());
}

void CGUIDialogMusicInfo::SetSongs(const VECSONGS &songs) const
{
  m_albumSongs->Clear();
  CMusicThumbLoader loader;
  for (unsigned int i = 0; i < songs.size(); i++)
  {
    const CSong& song = songs[i];
    CFileItemPtr item(new CFileItem(song));
    // Load the song art and related artist(s) (that may be different from album artist) art
    loader.LoadItem(item.get());
    m_albumSongs->Add(item);
  }
}

void CGUIDialogMusicInfo::SetDiscography() const
{
  m_albumSongs->Clear();
  CMusicDatabase database;
  database.Open();

  std::vector<int> albumsByArtist;
  database.GetAlbumsByArtist(m_artist.idArtist, albumsByArtist);

  // Sort the discography by year
  auto discography = m_artist.discography;
  std::sort(discography.begin(), discography.end(), [](const std::pair<std::string, std::string> &left, const std::pair<std::string, std::string> &right) {
    return left.second < right.second;
  });

  for (unsigned int i=0; i < discography.size(); ++i)
  {
    CFileItemPtr item(new CFileItem(discography[i].first));
    item->SetLabel2(discography[i].second);

    CMusicThumbLoader loader;
    int idAlbum = -1;
    for (std::vector<int>::const_iterator album = albumsByArtist.begin(); album != albumsByArtist.end(); ++album)
    {
      if (StringUtils::EqualsNoCase(database.GetAlbumById(*album), item->GetLabel()))
      {
        idAlbum = *album;
        item->GetMusicInfoTag()->SetDatabaseId(idAlbum, "album");
        // Load all the album art and related artist(s) art (could be other collaborating artists)
        loader.LoadItem(item.get());
        break;
      }
    }
    if (idAlbum == -1) 
      item->SetArt("thumb", "DefaultAlbumCover.png");

    m_albumSongs->Add(item);
  }
}

void CGUIDialogMusicInfo::Update()
{
  if (m_bArtistInfo)
  {
    SET_CONTROL_VISIBLE(CONTROL_BTN_GET_FANART);
    SET_CONTROL_HIDDEN(CONTROL_USERRATING);

    CGUIMessage message(GUI_MSG_LABEL_BIND, GetID(), CONTROL_LIST, 0, 0, m_albumSongs);
    OnMessage(message);

  }
  else
  {
    SET_CONTROL_VISIBLE(CONTROL_USERRATING);
    SET_CONTROL_HIDDEN(CONTROL_BTN_GET_FANART);

    CGUIMessage message(GUI_MSG_LABEL_BIND, GetID(), CONTROL_LIST, 0, 0, m_albumSongs);
    OnMessage(message);

  }

  // disable the GetThumb button if the user isn't allowed it
  CONTROL_ENABLE_ON_CONDITION(CONTROL_BTN_GET_THUMB, CProfilesManager::GetInstance().GetCurrentProfile().canWriteDatabases() || g_passwordManager.bMasterUser);
}

void CGUIDialogMusicInfo::SetLabel(int iControl, const std::string& strLabel)
{
  if (strLabel.empty())
  {
    SET_CONTROL_LABEL(iControl, 416);
  }
  else
  {
    SET_CONTROL_LABEL(iControl, strLabel);
  }
}

void CGUIDialogMusicInfo::OnInitWindow()
{
  SET_CONTROL_LABEL(CONTROL_BTN_REFRESH, 184);
  SET_CONTROL_LABEL(CONTROL_USERRATING, 38023);
  SET_CONTROL_LABEL(CONTROL_BTN_GET_THUMB, 13405);
  SET_CONTROL_LABEL(CONTROL_BTN_GET_FANART, 20413);

  if (m_bArtistInfo)
    SET_CONTROL_HIDDEN(CONTROL_USERRATING);
  else
    SET_CONTROL_HIDDEN(CONTROL_BTN_GET_FANART);

  CGUIDialog::OnInitWindow();
}

void CGUIDialogMusicInfo::SetUserrating(int userrating) const
{
  if (userrating < 0) userrating = 0;
  if (userrating > 10) userrating = 10;
  if (userrating != m_albumItem->GetMusicInfoTag()->GetUserrating())
  {
    m_albumItem->GetMusicInfoTag()->SetUserrating(userrating);
    // send a message to all windows to tell them to update the fileitem (eg playlistplayer, media windows)
    CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 0, m_albumItem);
    g_windowManager.SendMessage(msg);
  }
}

// Get Thumb from user choice.
// Options are:
// 1.  Current thumb
// 2.  AllMusic.com thumb
// 3.  Local thumb
// 4.  No thumb (if no Local thumb is available)

//! @todo Currently no support for "embedded thumb" as there is no easy way to grab it
//!       without sending a file that has this as it's album to this class
void CGUIDialogMusicInfo::OnGetThumb()
{
  CFileItemList items;

  // Current thumb
  if (CFile::Exists(m_albumItem->GetArt("thumb")))
  {
    CFileItemPtr item(new CFileItem("thumb://Current", false));
    item->SetArt("thumb", m_albumItem->GetArt("thumb"));
    item->SetLabel(g_localizeStrings.Get(20016));
    items.Add(item);
  }

  // Grab the thumbnail(s) from the web
  std::vector<std::string> thumbs;
  if (m_bArtistInfo)
    m_artist.thumbURL.GetThumbURLs(thumbs);
  else
    m_album.thumbURL.GetThumbURLs(thumbs);

  for (unsigned int i = 0; i < thumbs.size(); ++i)
  {
    std::string strItemPath;
    strItemPath = StringUtils::Format("thumb://Remote%i", i);
    CFileItemPtr item(new CFileItem(strItemPath, false));
    item->SetArt("thumb", thumbs[i]);
    item->SetIconImage("DefaultPicture.png");
    item->SetLabel(g_localizeStrings.Get(20015));
    
    //! @todo Do we need to clear the cached image?
    //    CTextureCache::GetInstance().ClearCachedImage(thumb);
    items.Add(item);
  }

  // local thumb
  std::string localThumb;
  bool existsThumb = false;
  if (m_bArtistInfo)
  {
    CMusicDatabase database;
    database.Open();
    // First look for thumb in the artists folder, the primary location
    std::string strArtistPath = m_artist.strPath;
    // Get path when don't already have it.
    bool artistpathfound = !strArtistPath.empty();
    if (!artistpathfound)
      artistpathfound = database.GetArtistPath(m_artist, strArtistPath);
    if (artistpathfound)
    {
      localThumb = URIUtils::AddFileToFolder(strArtistPath, "folder.jpg");
      existsThumb = CFile::Exists(localThumb);
    }
    // If not there fall back local to music files (historic location for those album artists with a unique folder)
    if (!existsThumb)
    {
      artistpathfound = database.GetOldArtistPath(m_artist.idArtist, strArtistPath);
      if (artistpathfound)
      {
        localThumb = URIUtils::AddFileToFolder(strArtistPath, "folder.jpg");
        existsThumb = CFile::Exists(localThumb);
      }
    }
  }
  else
  {
    localThumb = m_albumItem->GetUserMusicThumb();
    existsThumb = CFile::Exists(localThumb);
  }
  if (existsThumb)
  {
    CFileItemPtr item(new CFileItem("thumb://Local", false));
    item->SetArt("thumb", localThumb);
    item->SetLabel(g_localizeStrings.Get(20017));
    items.Add(item);
  }
  else
  {
    CFileItemPtr item(new CFileItem("thumb://None", false));
    if (m_bArtistInfo)
      item->SetIconImage("DefaultArtist.png");
    else
      item->SetIconImage("DefaultAlbumCover.png");
    item->SetLabel(g_localizeStrings.Get(20018));
    items.Add(item);
  }

  std::string result;
  bool flip=false;
  VECSOURCES sources(*CMediaSourceSettings::GetInstance().GetSources("music"));
  AddItemPathToFileBrowserSources(sources, *m_albumItem);
  g_mediaManager.GetLocalDrives(sources);
  if (!CGUIDialogFileBrowser::ShowAndGetImage(items, sources, g_localizeStrings.Get(1030), result, &flip))
    return;   // user cancelled

  if (result == "thumb://Current")
    return;   // user chose the one they have

  std::string newThumb;
  if (StringUtils::StartsWith(result, "thumb://Remote"))
  {
    int number = atoi(result.substr(14).c_str());
    newThumb = thumbs[number];
  }
  else if (result == "thumb://Local")
    newThumb = localThumb;
  else if (CFile::Exists(result))
    newThumb = result;

  // update thumb in the database
  CMusicDatabase db;
  if (db.Open())
  {
    db.SetArtForItem(m_albumItem->GetMusicInfoTag()->GetDatabaseId(), m_albumItem->GetMusicInfoTag()->GetType(), "thumb", newThumb);
    db.Close();
  }

  m_albumItem->SetArt("thumb", newThumb);
  m_hasUpdatedThumb = true;

  // tell our GUI to completely reload all controls (as some of them
  // are likely to have had this image in use so will need refreshing)
  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_REFRESH_THUMBS);
  g_windowManager.SendMessage(msg);
  // Update our screen
  Update();
}


// Allow user to select a Fanart
void CGUIDialogMusicInfo::OnGetFanart()
{
  CFileItemList items;

  if (m_albumItem->HasArt("fanart"))
  {
    CFileItemPtr itemCurrent(new CFileItem("fanart://Current",false));
    itemCurrent->SetArt("thumb", m_albumItem->GetArt("fanart"));
    itemCurrent->SetLabel(g_localizeStrings.Get(20440));
    items.Add(itemCurrent);
  }

  // Grab the thumbnails from the web
  for (unsigned int i = 0; i < m_artist.fanart.GetNumFanarts(); i++)
  {
    std::string strItemPath = StringUtils::Format("fanart://Remote%i",i);
    CFileItemPtr item(new CFileItem(strItemPath, false));
    std::string thumb = m_artist.fanart.GetPreviewURL(i);
    item->SetArt("thumb", CTextureUtils::GetWrappedThumbURL(thumb));
    item->SetIconImage("DefaultPicture.png");
    item->SetLabel(g_localizeStrings.Get(20441));

    //! @todo Do we need to clear the cached image?
    //    CTextureCache::GetInstance().ClearCachedImage(thumb);
    items.Add(item);
  }

  // Grab a local fanart 
  std::string strLocal;
  CMusicDatabase database;
  database.Open();
  // First look for fanart in the artists folder, the primary location
  std::string strArtistPath = m_artist.strPath;
  // Get path when don't already have it.
  bool artistpathfound = !strArtistPath.empty();
  if (!artistpathfound)
    artistpathfound = database.GetArtistPath(m_artist, strArtistPath);
  if (artistpathfound)
  {
    CFileItem item(strArtistPath, true);
    strLocal = item.GetLocalFanart();
  }
  // If not there fall back local to music files (historic location for those album artists with a unique folder)
  if (strLocal.empty())
  {
    artistpathfound = database.GetOldArtistPath(m_artist.idArtist, strArtistPath);
    if (artistpathfound)
    {
      CFileItem item(strArtistPath, true);
      strLocal = item.GetLocalFanart();
    }
  }

  if (!strLocal.empty())
  {
    CFileItemPtr itemLocal(new CFileItem("fanart://Local",false));
    itemLocal->SetArt("thumb", strLocal);
    itemLocal->SetLabel(g_localizeStrings.Get(20438));

    //! @todo Do we need to clear the cached image?
    CTextureCache::GetInstance().ClearCachedImage(strLocal);
    items.Add(itemLocal);
  }
  else
  {
    CFileItemPtr itemNone(new CFileItem("fanart://None", false));
    itemNone->SetIconImage("DefaultArtist.png");
    itemNone->SetLabel(g_localizeStrings.Get(20439));
    items.Add(itemNone);
  }

  std::string result;
  bool flip = false;
  VECSOURCES sources(*CMediaSourceSettings::GetInstance().GetSources("music"));
  AddItemPathToFileBrowserSources(sources, *m_albumItem);
  g_mediaManager.GetLocalDrives(sources);
  if (!CGUIDialogFileBrowser::ShowAndGetImage(items, sources, g_localizeStrings.Get(20437), result, &flip, 20445))
    return;   // user cancelled

  // delete the thumbnail if that's what the user wants, else overwrite with the
  // new thumbnail
  if (StringUtils::EqualsNoCase(result, "fanart://Current"))
   return;

  if (StringUtils::EqualsNoCase(result, "fanart://Local"))
    result = strLocal;

  if (StringUtils::StartsWith(result, "fanart://Remote"))
  {
    int iFanart = atoi(result.substr(15).c_str());
    m_artist.fanart.SetPrimaryFanart(iFanart);
    result = m_artist.fanart.GetImageURL();
  }
  else if (StringUtils::EqualsNoCase(result, "fanart://None") || !CFile::Exists(result))
    result.clear();

  if (flip && !result.empty())
    result = CTextureUtils::GetWrappedImageURL(result, "", "flipped");

  // update thumb in the database
  CMusicDatabase db;
  if (db.Open())
  {
    db.SetArtForItem(m_albumItem->GetMusicInfoTag()->GetDatabaseId(), m_albumItem->GetMusicInfoTag()->GetType(), "fanart", result);
    db.Close();
  }

  m_albumItem->SetArt("fanart", result);
  m_hasUpdatedThumb = true;
  // tell our GUI to completely reload all controls (as some of them
  // are likely to have had this image in use so will need refreshing)
  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_REFRESH_THUMBS);
  g_windowManager.SendMessage(msg);
  // Update our screen
  Update();
}

void CGUIDialogMusicInfo::OnSearch(const CFileItem* pItem)
{
  CMusicDatabase database;
  database.Open();
  if (pItem->HasMusicInfoTag() &&
      pItem->GetMusicInfoTag()->GetDatabaseId() > 0)
  {
    CAlbum album;
    if (database.GetAlbum(pItem->GetMusicInfoTag()->GetDatabaseId(), album))
    {
      std::string strPath;
      database.GetAlbumPath(pItem->GetMusicInfoTag()->GetDatabaseId(), strPath);
      SetAlbum(album,strPath);
      Update();
    }
  }
}

CFileItemPtr CGUIDialogMusicInfo::GetCurrentListItem(int offset)
{
  return m_albumItem;
}

void CGUIDialogMusicInfo::AddItemPathToFileBrowserSources(VECSOURCES &sources, const CFileItem &item)
{
  std::string itemDir;

  if (item.HasMusicInfoTag() && item.GetMusicInfoTag()->GetType() == MediaTypeSong)
    itemDir = URIUtils::GetParentPath(item.GetMusicInfoTag()->GetURL());
  else
    itemDir = item.GetPath();

  if (!itemDir.empty() && CDirectory::Exists(itemDir))
  {
    CMediaSource itemSource;
    itemSource.strName = g_localizeStrings.Get(36041);
    itemSource.strPath = itemDir;
    sources.push_back(itemSource);
  }
}

void CGUIDialogMusicInfo::OnSetUserrating() const
{
  CGUIDialogSelect *dialog = g_windowManager.GetWindow<CGUIDialogSelect>(WINDOW_DIALOG_SELECT);
  if (dialog)
  {
    // If we refresh and then try to set the rating there will be an items already here...
    dialog->Reset();

    dialog->SetHeading(CVariant{ 38023 });
    dialog->Add(g_localizeStrings.Get(38022));
    for (int i = 1; i <= 10; i++)
      dialog->Add(StringUtils::Format("%s: %i", g_localizeStrings.Get(563).c_str(), i));

    dialog->SetSelected(m_albumItem->GetMusicInfoTag()->GetUserrating());

    dialog->Open();

    int iItem = dialog->GetSelectedItem();
    if (iItem < 0)
      return;

    SetUserrating(iItem);
  }
}

void CGUIDialogMusicInfo::ShowFor(CFileItem item)
{
  auto window = g_windowManager.GetWindow<CGUIWindowMusicNav>(WINDOW_MUSIC_NAV);
  if (window)
    window->OnItemInfo(&item);
}
