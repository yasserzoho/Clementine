/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <QAbstractItemModel>
#include <QList>

#include <boost/shared_ptr.hpp>

#include "playlistitem.h"
#include "playlistsequence.h"
#include "core/song.h"
#include "smartplaylists/generator_fwd.h"

class LibraryBackend;
class PlaylistBackend;
class PlaylistFilter;
class Queue;
class RadioModel;
class TaskManager;

class QSortFilterProxyModel;
class QUndoStack;

namespace PlaylistUndoCommands {
  class InsertItems;
  class RemoveItems;
  class MoveItems;
}

typedef QMap<int, Qt::Alignment> ColumnAlignmentMap;
Q_DECLARE_METATYPE(Qt::Alignment);
Q_DECLARE_METATYPE(ColumnAlignmentMap);

// Objects that may prevent a song being added to the playlist. When there
// is something about to be inserted into it, Playlist notifies all of it's
// listeners about the fact and every one of them picks 'invalid' songs.
class SongInsertVetoListener : public QObject {
  Q_OBJECT

public:
  // Listener returns a list of 'invalid' songs. 'old_songs' are songs that are
  // currently in the playlist and 'new_songs' are the songs about to be added if
  // nobody exercises a veto.
  virtual SongList AboutToInsertSongs(const SongList& old_songs, const SongList& new_songs) = 0;
};

class Playlist : public QAbstractListModel {
  Q_OBJECT

  friend class PlaylistUndoCommands::InsertItems;
  friend class PlaylistUndoCommands::RemoveItems;
  friend class PlaylistUndoCommands::MoveItems;

 public:
  Playlist(PlaylistBackend* backend,
           TaskManager* task_manager,
           LibraryBackend* library,
           int id,
           QObject* parent = 0);
  ~Playlist();

  // Always add new columns to the end of this enum - the values are persisted
  enum Column {
    Column_Title = 0,
    Column_Artist,
    Column_Album,
    Column_AlbumArtist,
    Column_Composer,
    Column_Length,
    Column_Track,
    Column_Disc,
    Column_Year,
    Column_Genre,

    Column_BPM,
    Column_Bitrate,
    Column_Samplerate,
    Column_Filename,
    Column_BaseFilename,
    Column_Filesize,
    Column_Filetype,
    Column_DateCreated,
    Column_DateModified,

    Column_Rating,
    Column_PlayCount,
    Column_SkipCount,
    Column_LastPlayed,
    Column_Score,

    Column_Comment,

    ColumnCount
  };

  enum Role {
    Role_IsCurrent = Qt::UserRole + 1,
    Role_IsPaused,
    Role_StopAfter,
    Role_QueuePosition,
    Role_CanSetRating,
  };

  static const char* kRowsMimetype;
  static const char* kPlayNowMimetype;

  static const int kInvalidSongPriority;
  static const QRgb kInvalidSongColor;

  static const int kDynamicHistoryPriority;
  static const QRgb kDynamicHistoryColor;

  static const char* kSettingsGroup;

  static bool CompareItems(int column, Qt::SortOrder order,
                           PlaylistItemPtr a, PlaylistItemPtr b);

  static QString column_name(Column column);
  static bool column_is_editable(Playlist::Column column);
  static bool set_column_value(Song& song, Column column, const QVariant& value);

  // Persistence
  void Save() const;
  void Restore();

  // Accessors
  QSortFilterProxyModel* proxy() const;
  Queue* queue() const { return queue_; }

  int id() const { return id_; }

  int current_row() const;
  int last_played_row() const;
  int next_row() const;
  int previous_row() const;

  const QModelIndex current_index() const;

  bool stop_after_current() const;
  bool is_dynamic() const { return dynamic_playlist_; }

  const PlaylistItemPtr& item_at(int index) const { return items_[index]; }
  const bool has_item_at(int index) const { return index >= 0 && index < rowCount(); }

  PlaylistItemPtr current_item() const { return current_item_; }

  PlaylistItem::Options current_item_options() const;
  Song current_item_metadata() const;

  PlaylistItemList library_items_by_id(int id) const;

  SongList GetAllSongs() const;
  PlaylistItemList GetAllItems() const;
  quint64 GetTotalLength() const; // in seconds

  void set_sequence(PlaylistSequence* v);
  PlaylistSequence* sequence() const { return playlist_sequence_; }

  QUndoStack* undo_stack() const { return undo_stack_; }

  ColumnAlignmentMap column_alignments() const { return column_alignments_; }
  void set_column_alignments(const ColumnAlignmentMap& column_alignments);
  void set_column_align_left(int column);
  void set_column_align_center(int column);
  void set_column_align_right(int column);

  // Scrobbling
  int scrobble_point() const { return scrobble_point_; }
  bool has_scrobbled() const { return has_scrobbled_; }
  void set_scrobbled(bool v) { has_scrobbled_ = v; }

  // Changing the playlist
  void InsertItems              (const PlaylistItemList& items,     int pos = -1, bool play_now = false, bool enqueue = false);
  void InsertLibraryItems       (const SongList& items,             int pos = -1, bool play_now = false, bool enqueue = false);
  void InsertSongs              (const SongList& items,             int pos = -1, bool play_now = false, bool enqueue = false);
  void InsertSongsOrLibraryItems(const SongList& items,             int pos = -1, bool play_now = false, bool enqueue = false);
  void InsertSmartPlaylist      (smart_playlists::GeneratorPtr gen, int pos = -1, bool play_now = false, bool enqueue = false);
  void InsertUrls               (const QList<QUrl>& urls,           int pos = -1, bool play_now = false, bool enqueue = false);
  // Removes items with given indices from the playlist. This operation is not undoable.
  void RemoveItemsWithoutUndo   (const QList<int>& indices);

  // If this playlist contains the current item, this method will apply the "valid" flag on it.
  // If the "valid" flag is false, the song will be greyed out. Otherwise the grey color will
  // be undone.
  // If the song is a local file and it's valid but non existent or invalid but exists, the
  // song will be reloaded to even out the situation because obviously something has changed.
  // This returns true if this playlist had current item when the method was invoked.
  bool ApplyValidityOnCurrentSong(const QUrl& url, bool valid);
  // Grays out and reloads all deleted songs in this playlist.
  void InvalidateDeletedSongs();

  void StopAfter(int row);
  void ReloadItems(const QList<int>& rows);

  // Changes rating of a song to the given value asynchronously
  void RateSong(const QModelIndex& index, double rating);

  // Registers an object which will get notifications when new songs
  // are about to be inserted into this playlist.
  void AddSongInsertVetoListener(SongInsertVetoListener* listener);
  // Unregisters a SongInsertVetoListener object.
  void RemoveSongInsertVetoListener(SongInsertVetoListener* listener);

  // QAbstractListModel
  int rowCount(const QModelIndex& = QModelIndex()) const { return items_.count(); }
  int columnCount(const QModelIndex& = QModelIndex()) const { return ColumnCount; }
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
  bool setData(const QModelIndex &index, const QVariant &value, int role);
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
  Qt::ItemFlags flags(const QModelIndex &index) const;
  QStringList mimeTypes() const;
  Qt::DropActions supportedDropActions() const;
  QMimeData* mimeData(const QModelIndexList& indexes) const;
  bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent);
  void sort(int column, Qt::SortOrder order);
  bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex());

 public slots:
  void set_current_row(int index);
  void Paused();
  void Playing();
  void Stopped();
  void IgnoreSorting(bool value) { ignore_sorting_ = value; }

  void ClearStreamMetadata();
  void SetStreamMetadata(const QUrl& url, const Song& song);
  void ItemChanged(PlaylistItemPtr item);

  void Clear();
  void Shuffle();

  void ShuffleModeChanged(PlaylistSequence::ShuffleMode mode);

  void RepopulateDynamicPlaylist();
  void TurnOffDynamicPlaylist();

 signals:
  void RestoreFinished();
  void CurrentSongChanged(const Song& metadata);
  void EditingFinished(const QModelIndex& index);
  void PlayRequested(const QModelIndex& index);

  // Signals that the underlying list of items was changed, meaning that
  // something was added to it, removed from it or the ordering changed.
  void PlaylistChanged();
  void DynamicModeChanged(bool dynamic);

  void LoadTracksError(const QString& message);

 private:
  void SetCurrentIsPaused(bool paused);
  void UpdateScrobblePoint();
  void ReshuffleIndices();
  int NextVirtualIndex(int i) const;
  int PreviousVirtualIndex(int i) const;
  bool FilterContainsVirtualIndex(int i) const;
  void TurnOnDynamicPlaylist(smart_playlists::GeneratorPtr gen);

  void InsertRadioStations(const RadioModel* model,
                           const QModelIndexList& items,
                           int pos, bool play_now, bool enqueue);

  template<typename T>
  void InsertSongItems(const SongList& songs, int pos, bool play_now, bool enqueue);

  // Modify the playlist without changing the undo stack.  These are used by
  // our friends in PlaylistUndoCommands
  void InsertItemsWithoutUndo(const PlaylistItemList& items, int pos,
                              bool enqueue = false);
  PlaylistItemList RemoveItemsWithoutUndo(int pos, int count);
  void MoveItemsWithoutUndo(const QList<int>& source_rows, int pos);
  void MoveItemsWithoutUndo(int start, const QList<int>& dest_rows);

  void RemoveItemsNotInQueue();

  void InformOfCurrentSongChange(const QModelIndex& top_left, const QModelIndex& bottom_right,
                                 const Song& metadata);

 private slots:
  void TracksAboutToBeDequeued(const QModelIndex&, int begin, int end);
  void TracksDequeued();
  void TracksEnqueued(const QModelIndex&, int begin, int end);
  void QueueLayoutChanged();
  void SongSaveComplete();
  void ItemReloadComplete();
  void ItemsLoaded();
  void SongInsertVetoListenerDestroyed();

 private:
  bool is_loading_;
  PlaylistFilter* proxy_;
  Queue* queue_;

  QList<QModelIndex> temp_dequeue_change_indexes_;

  PlaylistBackend* backend_;
  TaskManager* task_manager_;
  LibraryBackend* library_;
  int id_;

  PlaylistItemList items_;
  QList<int> virtual_items_; // Contains the indices into items_ in the order
                             // that they will be played.
  // A map of library ID to playlist item - for fast lookups when library
  // items change.
  QMultiMap<int, PlaylistItemPtr> library_items_by_id_;

  QPersistentModelIndex current_item_index_;
  QPersistentModelIndex last_played_item_index_;
  QPersistentModelIndex stop_after_;
  bool current_is_paused_;
  int current_virtual_index_;

  PlaylistItemPtr current_item_;

  bool is_shuffled_;

  int scrobble_point_;
  bool has_scrobbled_;

  PlaylistSequence* playlist_sequence_;

  // Hack to stop QTreeView::setModel sorting the playlist
  bool ignore_sorting_;

  QUndoStack* undo_stack_;

  smart_playlists::GeneratorPtr dynamic_playlist_;
  ColumnAlignmentMap column_alignments_;

  QList<SongInsertVetoListener*> veto_listeners_;
};

QDataStream& operator <<(QDataStream&, const Playlist*);
QDataStream& operator >>(QDataStream&, Playlist*&);

#endif // PLAYLIST_H
