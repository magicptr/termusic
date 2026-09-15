#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

#include <iostream>

#include "app/history.hpp"
#include "app/reconnect.hpp"
#include "app/playback.hpp"
#include "app/keymap.hpp"
#include "app/interaction.hpp"
#include "app/workspace_tree.hpp"
#include "app/state.hpp"
#include "app/diagnostics.hpp"
#include "config/config.hpp"
#include "config/paths.hpp"
#include "extensions/extension_registry.hpp"
#include "ui/metrics.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"
#include "util/text.hpp"
#include "visualizer/analyzer.hpp"

int main() {
  using namespace termusic;

  static_assert(isDefaultPlaylistName("Default"));
  static_assert(!isDefaultPlaylistName("default"));
  {
    const auto playlists = playlistsWithDefault(
        {"Road Trip", "Default", "Night Drive"});
    assert((playlists ==
            std::vector<std::string>{"Default", "Road Trip", "Night Drive"}));
    assert(playlistsWithDefault({}) ==
           std::vector<std::string>{"Default"});
  }

  // --- vault / core directory model ----------------------------------------
  // Round 55: the tree IS the navigation. Two roots, like a filesystem:
  // `vault` holds the media, `core` holds the configuration files. Neither is
  // a page tab, and MPD's own queue is never a browsable collection.
  {
    WorkspaceTree tree;
    const auto &nodes = tree.visible();

    assert(nodes[0].type == TreeNodeType::VaultRoot);
    assert(nodes[0].label == "Vault");
    assert(nodes[0].depth == 0);
    assert(nodes[0].expandable && nodes[0].expanded);

    // vault's children: Library, History, PlayLists (all one level in).
    assert(nodes[1].type == TreeNodeType::Database);
    assert(nodes[1].label == "Library");
    assert(nodes[1].depth == 1);
    assert(!nodes[1].expandable);
    assert(nodes[2].type == TreeNodeType::History);
    assert(nodes[2].label == "History");
    assert(nodes[2].depth == 1);
    assert(nodes[3].type == TreeNodeType::Group);
    assert(nodes[3].label == "PlayLists");
    assert(nodes[3].depth == 1);
    assert(nodes[3].expandable);

    // No playlists are set yet, so PlayLists has NO child of its own: the tree
    // goes straight from the folder to `Core`. MPD's runtime queue is a
    // playback mechanism, not a collection, and never gets a row here.
    assert(nodes.size() == 5);

    // Index lookups follow the model, not any display string.
    assert(tree.databaseIndex() == 1);
    assert(tree.historyIndex() == 2);

    // core is the second root, collapsed by default, holding config FILES.
    const int core_index = static_cast<int>(nodes.size()) - 1;
    assert(nodes[static_cast<std::size_t>(core_index)].type ==
           TreeNodeType::CoreRoot);
    assert(nodes[static_cast<std::size_t>(core_index)].label == "Core");
    assert(!nodes[static_cast<std::size_t>(core_index)].expanded);
    // `core` holds operable settings entries, never file names.
    for (const ConfigFileEntry &entry : kCoreSections) {
      assert(!entry.label.empty());
      assert(std::string(entry.label).find(".toml") == std::string::npos);
    }

    // Default is the protected virtual first row; saved playlists follow it.
    tree.setPlaylists({{"Default", "Default"}, {"Night Drive", "Night Drive"}});
    const auto &with_user = tree.visible();
    assert(with_user[4].type == TreeNodeType::Playlist);
    assert(with_user[4].label == "Default");
    assert(with_user[4].id == "Default");
    assert(with_user[4].depth == 2);
    assert(!with_user[4].expandable && with_user[4].isCollection());
    assert(with_user[5].label == "Night Drive");
    assert(with_user[5].depth == 2);

    // Collapsing PlayLists hides only the playlists.
    tree.setCursor(3);
    assert(tree.collapse());
    {
      const auto &collapsed = tree.visible();
      assert(collapsed[0].label == "Vault");
      assert(collapsed[1].label == "Library");
      assert(collapsed[2].label == "History");
      assert(collapsed[3].label == "PlayLists");
      assert(collapsed.size() == 5);
      assert(tree.databaseIndex() == 1);
      assert(tree.historyIndex() == 2);
    }

    // Expanding core appends the config files at depth 1, and the earlier
    // rows are untouched.
    tree.setCursor(0); // vault
    tree.toLast();
    assert(tree.current() != nullptr);
    assert(tree.current()->type == TreeNodeType::CoreRoot);
    assert(tree.expand());
    {
      const auto &with_core = tree.visible();
      bool saw_config = false;
      for (const TreeNode &node : with_core) {
        if (node.type != TreeNodeType::CoreSection)
          continue;
        saw_config = true;
        assert(node.depth == 1);
        assert(node.isCollection());
        assert(!node.expandable);
      }
      assert(saw_config);
    }
  }

  // --- Playback history store ----------------------------------------------
  // Round 53: application-owned, newest-first display, duplicates kept, hard
  // cap of 100 with FIFO eviction, and durable across restarts.
  {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "termusic_history_test.toml";
    std::filesystem::remove(path);

    const auto song = [](std::string uri) {
      Song s;
      s.uri = std::move(uri);
      s.title = "T";
      s.artist = "A";
      s.album = "B";
      return s;
    };

    HistoryStore history(path);
    assert(history.empty());
    assert(history.kMaxEntries == 100);

    // A B A C: duplicates are occurrences, never deduplicated.
    history.record(song("A"));
    history.record(song("B"));
    history.record(song("A"));
    history.record(song("C"));
    assert(history.size() == 4);
    const auto &stored = history.entries();
    assert(stored[0].uri == "A"); // oldest first in STORAGE
    assert(stored[1].uri == "B");
    assert(stored[2].uri == "A");
    assert(stored[3].uri == "C");

    // Display order is newest first: C A B A.
    const std::vector<Song> view = history.songsNewestFirst();
    assert(view.size() == 4);
    assert(view[0].uri == "C");
    assert(view[1].uri == "A");
    assert(view[2].uri == "B");
    assert(view[3].uri == "A");

    // An empty URI is not an occurrence.
    history.record(song(""));
    assert(history.size() == 4);

    // Cap: entry 101 evicts the oldest, the newest 100 survive.
    for (int i = 0; i < 101; ++i)
      history.record(song("X" + std::to_string(i)));
    assert(history.size() == 100);
    assert(history.entries().front().uri == "X1");   // X0 evicted
    assert(history.entries().back().uri == "X100");  // newest kept
    assert(history.songsNewestFirst().front().uri == "X100");

    // Persistence: the same entries come back, in the same order.
    std::string error;
    assert(history.save(&error));
    HistoryStore reloaded(path);
    assert(reloaded.load());
    assert(reloaded.size() == 100);
    assert(reloaded.entries().front().uri == "X1");
    assert(reloaded.entries().back().uri == "X100");
    assert(reloaded.songsNewestFirst().front().uri == "X100");
    assert(reloaded.entries().back().title == "T");
    assert(reloaded.entries().back().artist == "A");
    assert(reloaded.entries().back().album == "B");

    // Every occurrence is stamped with WHEN it played, and the stamp survives
    // a save/load round trip: the History view shows a real time, not a
    // fabricated one.
    {
      history.clear();
      history.recordAt(song("S1"), 1700000000LL);
      history.recordAt(song("S2"), 1700000123LL);
      assert(history.entries().front().played_at == 1700000000LL);
      assert(history.entries().back().played_at == 1700000123LL);

      // The newest-first view and its parallel time column stay in step.
      const std::vector<Song> timed = history.songsNewestFirst();
      const std::vector<long long> times = history.timesNewestFirst();
      assert(timed.size() == times.size());
      assert(times[0] == 1700000123LL && timed[0].uri == "S2");
      assert(times[1] == 1700000000LL && timed[1].uri == "S1");
      // History rows carry their time on the Song itself, so the renderer
      // needs no side table.
      assert(timed[0].played_at_epoch == 1700000123LL);

      assert(history.save(&error));
      HistoryStore timed_reload(path);
      assert(timed_reload.load());
      assert(timed_reload.timesNewestFirst()[0] == 1700000123LL);
      assert(timed_reload.entries().front().played_at == 1700000000LL);
    }

    // --- Manual deletion is occurrence-based and persistent ---------------
    {
      history.clear();
      const auto add = [&](std::string uri, long long at) {
        history.recordAt(song(std::move(uri)), at);
      };
      add("A", 100);
      add("B", 200);
      add("A", 300);
      add("C", 400);
      assert(history.size() == 4);

      // Every occurrence has its own id, so the two A records are separable.
      const long long first_a = history.entries()[0].id;
      const long long second_a = history.entries()[2].id;
      assert(first_a != 0 && second_a != 0 && first_a != second_a);

      // Deleting the SECOND A removes exactly that occurrence.
      assert(history.removeRecord(second_a));
      assert(history.size() == 3);
      assert(history.entries()[0].uri == "A");
      assert(history.entries()[1].uri == "B");
      assert(history.entries()[2].uri == "C");
      // The first A survived: identity is the record, never the URI.
      assert(history.entries()[0].id == first_a);
      // Deleting it again is a no-op, not a second removal.
      assert(!history.removeRecord(second_a));
      assert(history.size() == 3);

      // The deletion is persistent: a reload must not bring the row back.
      std::string history_error;
      assert(history.save(&history_error));
      HistoryStore after_delete(path);
      assert(after_delete.load());
      assert(after_delete.size() == 3);
      assert(after_delete.entries()[1].uri == "B");
      assert(!after_delete.hasRecord(second_a));

      // Ids stay unique across a reload, so a stale id can never address a
      // different record.
      after_delete.recordAt(song("D"), 500);
      assert(after_delete.entries().back().id != second_a);
      assert(after_delete.entries().back().id != first_a);
    }

    // --- Visual-range deletion (spec §20, §41) -----------------------------
    {
      history.clear();
      for (const char *uri : {"A", "B", "C", "D", "E"})
        history.recordAt(song(uri), 100);
      assert(history.size() == 5);
      // Rows 1..3 of storage order are B C D.
      assert(history.removeRange(1, 3) == 3);
      assert(history.size() == 2);
      assert(history.entries()[0].uri == "A");
      assert(history.entries()[1].uri == "E");
      // The range is resolved in one pass, so asking beyond the end is safe.
      assert(history.removeRange(1, 99) == 1);
      assert(history.size() == 1);
      assert(history.removeRange(0, 0) == 0);
      assert(history.size() == 1);
    }

    // --- Manual deletion then new playback: the cap still applies ----------
    // Spec §24: 100 entries, delete 10, play 15 -> 100, oldest 5 evicted.
    {
      history.clear();
      for (int i = 0; i < 100; ++i)
        history.recordAt(song("H" + std::to_string(i)), 1000 + i);
      assert(history.size() == 100);
      assert(history.removeRange(0, 10) == 10);
      assert(history.size() == 90);
      assert(history.entries().front().uri == "H10");
      for (int i = 0; i < 15; ++i)
        history.recordAt(song("N" + std::to_string(i)), 2000 + i);
      assert(history.size() == 100);
      // The oldest five of the surviving 90 were evicted ...
      assert(history.entries().front().uri == "H15");
      // ... and the newest occurrence is the one just played.
      assert(history.entries().back().uri == "N14");
    }

    // --- A file written before ids existed still loads and deletes ---------
    {
      std::ofstream out(path, std::ios::trunc);
      out << "[[entry]]\nuri = \"old.a\"\nplayed_at = 10\n"
          << "[[entry]]\nuri = \"old.b\"\nplayed_at = 20\n";
    }
    HistoryStore legacy(path);
    assert(legacy.load());
    assert(legacy.size() == 2);
    // Ids were assigned on load and are unique, so deletion works on a file
    // that never carried them.
    const long long legacy_id = legacy.entries()[1].id;
    assert(legacy_id != 0);
    assert(legacy.entries()[0].id != legacy_id);
    assert(legacy.removeRecord(legacy_id));
    assert(legacy.size() == 1);
    assert(legacy.entries()[0].uri == "old.a");

    // The history VIEW carries the record id, which is what lets the UI delete
    // the row the cursor is on without matching by URI.
    history.clear();
    history.recordAt(song("A"), 100);
    history.recordAt(song("A"), 200);
    const std::vector<Song> id_view = history.songsNewestFirst();
    assert(id_view.size() == 2);
    assert(id_view[0].history_record_id != 0);
    assert(id_view[0].history_record_id != id_view[1].history_record_id);
    assert(id_view[0].played_at_epoch == 200);

    // An entry recorded by an older version of the file -- or a hand-edited
    // one -- has no timestamp: it stays untimed rather than guessing.
    {
      std::ofstream out(path, std::ios::trunc);
      out << "[[entry]]\nuri = \"old\"\ntitle = \"T\"\n"
          << "[[entry]]\nuri = \"bad\"\nplayed_at = \"not a number\"\n";
    }
    HistoryStore untimed(path);
    assert(untimed.load());
    assert(untimed.size() == 2);
    assert(untimed.entries()[0].played_at == 0);
    assert(untimed.entries()[1].played_at == 0);
    assert(untimed.timesNewestFirst()[0] == 0);

    // A missing file is not an error: it just means empty history, and no
    // warning is produced for a first run.
    std::filesystem::remove(path);
    HistoryStore missing(path);
    std::string missing_warning;
    assert(!missing.load(&missing_warning));
    assert(missing.empty());
    assert(missing_warning.empty());

    // A hand-edited file beyond the cap keeps the NEWEST entries.
    {
      std::ofstream out(path, std::ios::trunc);
      for (int i = 0; i < 130; ++i)
        out << "[[entry]]\nuri = \"H" << i << "\"\n";
    }
    HistoryStore overfull(path);
    assert(overfull.load());
    assert(overfull.size() == 100);
    assert(overfull.entries().front().uri == "H30");
    assert(overfull.entries().back().uri == "H129");
    std::filesystem::remove(path);
  }

  // --- Playback context: which collection owns the playing row --------------
  // The product rule is "from which collection playback starts, that collection
  // becomes the playback context". These are the two decisions that rule needs,
  // exercised without a backend, a queue or a terminal.
  {
    using termusic::PlaybackCollection;

    const auto song = [](std::string uri) {
      Song s;
      s.uri = std::move(uri);
      s.title = s.uri;
      return s;
    };
    const auto make = [&](std::initializer_list<const char *> uris) {
      std::vector<Song> tracks;
      for (const char *uri : uris)
        tracks.push_back(song(uri));
      assignOccurrenceCounters(tracks);
      return tracks;
    };
    const auto kind = [](PlaybackCollection::Kind k, std::string name = {}) {
      PlaybackCollection c;
      c.kind = k;
      c.name = std::move(name);
      return c;
    };

    // --- The duplicate counter is what makes "the second A" addressable -----
    auto abac = make({"A", "B", "A", "C"});
    assert(abac[0].playback_occurrence == 1);
    assert(abac[2].playback_occurrence == 2);

    // Entering the SECOND A must resolve to row 2, not row 0.
    {
      Song second_a = song("A");
      second_a.playback_occurrence = 2;
      assert(occurrenceIndexIn(abac, second_a) == 2);
      Song first_a = song("A");
      first_a.playback_occurrence = 1;
      assert(occurrenceIndexIn(abac, first_a) == 0);
      // A target that is not in the list at all is not silently mapped to a
      // same-URI row.
      assert(occurrenceIndexIn(abac, song("ZZZ")) == -1);
      // An unspecified counter means "the first one", which is what a Song from
      // Library carries.
      assert(occurrenceIndexIn(abac, song("A")) == 0);
    }

    // --- The Library / default test (spec §15) ------------------------------
    const auto library = make({"A", "B", "C"});
    const auto default_list = make({"A", "B", "C"});
    const PlaybackCollection library_ctx = kind(PlaybackCollection::Kind::Library);
    const PlaybackCollection default_ctx =
        kind(PlaybackCollection::Kind::Playlist, "default");

    Song playing_b = song("B");
    playing_b.playback_occurrence = 1;

    // B started from `default`: only `default` marks it.
    assert(playingRowIn(default_ctx, default_ctx, default_list, playing_b,
                        false) == 1);
    assert(playingRowIn(library_ctx, default_ctx, library, playing_b, false) ==
           -1);

    // B started from Library: only Library marks it. Same URI in both lists,
    // and the URI alone could never have told them apart.
    assert(playingRowIn(library_ctx, library_ctx, library, playing_b, false) ==
           1);
    assert(playingRowIn(default_ctx, library_ctx, default_list, playing_b,
                        false) == -1);

    // --- A custom playlist with the same URI (spec §1, §2, §37) -------------
    const auto rock = make({"X", "B", "Y"});
    const PlaybackCollection rock_ctx =
        kind(PlaybackCollection::Kind::Playlist, "Rock");
    assert(playingRowIn(rock_ctx, rock_ctx, rock, playing_b, false) == 1);
    assert(playingRowIn(library_ctx, rock_ctx, library, playing_b, false) == -1);
    assert(playingRowIn(default_ctx, rock_ctx, default_list, playing_b, false) ==
           -1);

    // Browsing Rock while Library owns playback stays unmarked, and vice versa:
    // the displayed list has to be the owner.
    assert(playingRowIn(rock_ctx, library_ctx, rock, playing_b, false) == -1);

    // --- Duplicate URI inside one list (spec §13, §39) ----------------------
    // Only the SECOND A is marked; a URI match would have marked both.
    Song second_a = song("A");
    second_a.playback_occurrence = 2;
    assert(playingRowIn(default_ctx, default_ctx, abac, second_a, false) == 2);
    Song first_a = song("A");
    first_a.playback_occurrence = 1;
    assert(playingRowIn(default_ctx, default_ctx, abac, first_a, false) == 0);

    // Previous/Next inside that list are the neighbours of the SECOND A.
    const int at = playingRowIn(default_ctx, default_ctx, abac, second_a, false);
    assert(at == 2);
    assert(abac[static_cast<std::size_t>(at) - 1U].uri == "B");
    assert(abac[static_cast<std::size_t>(at) + 1U].uri == "C");

    // --- History is occurrence-keyed, not URI-keyed (spec §16, §21, §33) ----
    const PlaybackCollection history_ctx =
        kind(PlaybackCollection::Kind::History);
    std::vector<Song> history_rows = {song("C"), song("A"), song("B"),
                                      song("A")};
    history_rows[0].history_record_id = 41;
    history_rows[1].history_record_id = 42;
    history_rows[2].history_record_id = 43;
    history_rows[3].history_record_id = 44;
    assignOccurrenceCounters(history_rows);

    Song playing_record = song("A");
    playing_record.history_record_id = 44; // the LAST A, i.e. row 3
    assert(playingRowIn(history_ctx, history_ctx, history_rows, playing_record,
                        true) == 3);

    // The other A is a DIFFERENT record and must not be marked.
    Song other_record = song("A");
    other_record.history_record_id = 42;
    assert(playingRowIn(history_ctx, history_ctx, history_rows, other_record,
                        true) == 1);

    // The record was deleted from storage while it keeps playing: this view has
    // nothing to mark, and no same-URI row may be substituted for it.
    Song deleted_record = song("A");
    deleted_record.history_record_id = 99;
    assert(playingRowIn(history_ctx, history_ctx, history_rows, deleted_record,
                        true) == -1);

    // --- An unknown / external context marks nothing (spec §35) -------------
    const PlaybackCollection unknown;
    assert(!unknown.valid());
    assert(playingRowIn(library_ctx, unknown, library, playing_b, false) == -1);
    assert(playingRowIn(default_ctx, unknown, default_list, playing_b, false) ==
           -1);
    assert(playingRowIn(history_ctx, unknown, history_rows, playing_record,
                        true) == -1);

    // --- At most one marker, always (spec §14) ------------------------------
    const std::vector<const std::vector<termusic::Song> *> every_list = {
        &library, &default_list, &rock, &abac};
    for (const auto *tracks : every_list) {
      const int row =
          playingRowIn(default_ctx, default_ctx, *tracks, Song{}, false);
      assert(row == -1); // an empty target marks nothing
    }
    {
      int markers = 0;
      for (int index = 0; index < static_cast<int>(abac.size()); ++index) {
        Song row = abac[static_cast<std::size_t>(index)];
        row.playback_occurrence = index + 1; // pretend each row is the one
        if (playingRowIn(default_ctx, default_ctx, abac, row, false) == index)
          ++markers;
      }
      // Whatever the run is on, exactly the row it names answers -- never two.
      assert(markers == 1);
    }
  }

  // --- Slider coordinate system --------------------------------------------
  // Every one of these assertions guards the off-by-one that made the fill run
  // one cell past the thumb: fill used progress * cells while the thumb used
  // round(progress * (cells - 1)).
  {
    using termusic::ui::SliderCellKind;
    using termusic::ui::SliderGeometry;
    using termusic::ui::sliderCells;

    const std::vector<double> probes = {0.0,  0.001, 0.005, 0.01, 0.02,
                                        0.05, 0.1,   0.25, 0.5,  0.75,
                                        0.9,  0.95,  0.96, 0.97, 0.98,
                                        0.99, 0.995, 0.999, 1.0};
    const std::vector<int> widths = {1, 2, 3, 10, 30, 100};

    for (const int cells : widths) {
      for (const double progress : probes) {
        const SliderGeometry geometry =
            SliderGeometry::fromProgress(cells, progress);

        // (5) width invariant: the renderer emits exactly N cells, never N+1.
        const auto kinds = sliderCells(geometry);
        assert(static_cast<int>(kinds.size()) == cells);

        // Exactly one thumb, and it is inside the track.
        int thumbs = 0;
        for (int index = 0; index < cells; ++index) {
          if (kinds[static_cast<std::size_t>(index)] == SliderCellKind::Thumb)
            ++thumbs;
        }
        assert(thumbs == 1);
        assert(geometry.thumb_cell >= 0);
        assert(geometry.thumb_cell <= cells - 1);

        // Fill ends flush with the thumb: no filled cell at or after the thumb.
        assert(geometry.full_cells == geometry.thumb_cell);
        for (int index = geometry.thumb_cell; index < cells; ++index) {
          assert(kinds[static_cast<std::size_t>(index)] !=
                 SliderCellKind::Filled);
        }

        // exact_position is always derived from (cells - 1).
        const double expected =
            std::clamp(progress, 0.0, 1.0) * static_cast<double>(cells - 1);
        assert(std::abs(geometry.exact_position - expected) < 1e-12);
        assert(geometry.fraction >= 0.0 && geometry.fraction < 1.0);

        // (4) round-trip progress -> cell -> progress stays in range and is
        // closest to the original cell.
        const double back = SliderGeometry::progressFromPosition(
            cells, static_cast<double>(geometry.thumb_cell));
        assert(back >= 0.0 && back <= 1.0);
        const int round_tripped = SliderGeometry::fromProgress(cells, back)
                                      .thumb_cell;
        assert(round_tripped == geometry.thumb_cell);
      }

      // (12) left edge and (11) right edge, densely sampled.
      assert(SliderGeometry::fromProgress(cells, 0.0).thumb_cell == 0);
      assert(SliderGeometry::fromProgress(cells, 1.0).thumb_cell == cells - 1);
      // Left edge: the thumb stays in the track and only occupies cell 0
      // while the float position is still below the 0.5 rounding boundary.
      for (const double progress : {0.0, 0.001, 0.005, 0.01, 0.02, 0.05}) {
        const SliderGeometry geometry =
            SliderGeometry::fromProgress(cells, progress);
        assert(geometry.thumb_cell >= 0 && geometry.thumb_cell <= cells - 1);
        assert((geometry.thumb_cell == 0) == (geometry.exact_position < 0.5));
      }
      // Right edge: it reaches the last cell as soon as it rounds there, and
      // never beyond.
      for (const double progress :
           {0.95, 0.96, 0.97, 0.98, 0.99, 0.995, 0.999, 1.0}) {
        const SliderGeometry geometry =
            SliderGeometry::fromProgress(cells, progress);
        assert(geometry.thumb_cell >= 0 && geometry.thumb_cell <= cells - 1);
        assert((geometry.thumb_cell == cells - 1) ==
               (geometry.exact_position >= static_cast<double>(cells) - 1.5));
      }

      // Monotonic and never out of range.
      int previous = -1;
      for (int step = 0; step <= 1000; ++step) {
        const int cell =
            SliderGeometry::fromProgress(cells, step / 1000.0).thumb_cell;
        assert(cell >= previous && cell <= cells - 1);
        previous = cell;
      }
    }

    // (6) N = 1 must not divide by zero or index out of range.
    for (const double progress : probes) {
      const SliderGeometry one = SliderGeometry::fromProgress(1, progress);
      assert(one.track_cells == 1);
      assert(one.thumb_cell == 0);
      assert(one.exact_position == 0.0);
      assert(sliderCells(one).size() == 1U);
    }
    assert(SliderGeometry::progressFromPosition(1, 0.0) == 0.0);
    assert(SliderGeometry::progressFromPosition(1, 5.0) == 0.0);

    // Out-of-range input is clamped, never propagated.
    assert(SliderGeometry::fromProgress(30, -1.0).thumb_cell == 0);
    assert(SliderGeometry::fromProgress(30, 2.0).thumb_cell == 29);
    assert(SliderGeometry::progressFromPosition(30, 0.0) == 0.0);
    assert(SliderGeometry::progressFromPosition(30, 29.0) == 1.0);
    assert(SliderGeometry::progressFromPosition(30, 100.0) == 1.0);

    // The same model must drive the volume slider: 0% and 100% volume land on
    // the first and last cell too.
    assert(SliderGeometry::fromProgress(20, 0.0 / 100.0).thumb_cell == 0);
    assert(SliderGeometry::fromProgress(20, 100.0 / 100.0).thumb_cell == 19);
  }

  // --- Analyzer lifecycle: restart, band-count change, companion drift ------
  {
    VisualizerAnalyzer analyzer;
    std::vector<std::int16_t> pcm(static_cast<std::size_t>(4096) * 2U, 0);
    for (std::size_t i = 0; i < pcm.size(); ++i)
      pcm[i] = static_cast<std::int16_t>(
          std::sin(static_cast<double>(i) * 0.05) * 8000.0);

    // (1)(2) 100 start -> PCM -> analyze -> stop cycles. This is the exact
    // lifecycle that produced the Round 17 crash.
    int restarts = 0;
    for (int cycle = 0; cycle < 100; ++cycle) {
      analyzer.start("/nonexistent/termusic-lifecycle.fifo", 44100, 2, 96,
                     1.0F, 30, [] {});
      for (int frame = 0; frame < 3; ++frame)
        analyzer.analyzePcm(pcm);
      assert(analyzer.companionSizesConsistent());
      assert(analyzer.allocatedBandCount() == 96U);
      analyzer.stop();
      // stop() must not half-clear the companions.
      assert(analyzer.companionSizesConsistent());
      ++restarts;
    }
    assert(restarts == 100);

    // (7) Re-analysing at the same band count must never reallocate.
    const std::size_t resets_before = analyzer.bandStateResets();
    analyzer.start("/nonexistent/termusic-lifecycle.fifo", 44100, 2, 96, 1.0F,
                   30, [] {});
    for (int frame = 0; frame < 20; ++frame)
      analyzer.analyzePcm(pcm);
    assert(analyzer.bandStateResets() == resets_before);
    assert(analyzer.companionSizesConsistent());
    analyzer.stop();

    // (6) Band-count transitions. start() clamps to [32, 96].
    std::size_t expected_resets = analyzer.bandStateResets();
    for (const int bands : {32, 64, 96, 64, 32, 96}) {
      analyzer.start("/nonexistent/termusic-lifecycle.fifo", 44100, 2, bands,
                     1.0F, 30, [] {});
      analyzer.analyzePcm(pcm);
      assert(analyzer.allocatedBandCount() == static_cast<std::size_t>(bands));
      assert(analyzer.companionSizesConsistent());
      ++expected_resets; // one legitimate reallocation per real change
      assert(analyzer.bandStateResets() == expected_resets);
      analyzer.stop();
    }

    // (8) Companion drift must be detected, not silently reset.
    analyzer.start("/nonexistent/termusic-lifecycle.fifo", 44100, 2, 96, 1.0F,
                   30, [] {});
    analyzer.analyzePcm(pcm);
    assert(analyzer.companionSizesConsistent());
#ifndef NDEBUG
    analyzer.debugCorruptCompanion();
    assert(!analyzer.companionSizesConsistent());
#endif
    analyzer.stop();
  }

  Song fallback;
  fallback.uri = "Artist/Album/track.flac";
  assert(fallback.displayTitle() == "track.flac");
  assert(fallback.displayArtist() == "Unknown Artist");
  assert(fallback.displayAlbum() == "Unknown Album");
  assert(util::formatDuration(187.0) == "3:07");
  assert(util::displayWidth("音乐") == 4);
  assert(util::displayWidth(util::ellipsize("一二三四", 5)) <= 5);

  // The sidebar's playback block draws every one of its three lines through
  // this helper, so the three promises it depends on are pinned here: the
  // result never exceeds the budget, a cut never splits a codepoint (a Chinese
  // or Japanese character disappears whole, never as broken bytes), and wide
  // characters count as two cells.
  {
    struct Line {
      std::string text;
      int budget;
    };
    const std::vector<Line> lines = {
        {"Nightfall", 24},                           // ASCII, fits
        {"Aurora Fields", 12},                       // ASCII, truncated
        {"小镇姑娘和她的很长很长的播放列表名称测试", 18}, // Chinese, truncated
        {"さくらと日本語のとても長い楽曲名のテスト", 16},  // Japanese, truncated
        {"\uF001 Loud Noise", 9},                    // Nerd Font glyph
        {"Nightfall (Live at the Roundhouse, 2019)", 34},
    };
    for (const Line &line : lines) {
      const std::string clipped = util::ellipsize(line.text, line.budget);
      assert(util::displayWidth(clipped) <= line.budget);
      assert(!clipped.empty()); // each budget leaves room for the ellipsis
      // Valid UTF-8 after the cut: no half character is ever emitted.
      int continuation = 0;
      for (const char raw : clipped) {
        const auto byte = static_cast<unsigned char>(raw);
        if (continuation > 0) {
          assert((byte & 0xC0U) == 0x80U);
          --continuation;
          continue;
        }
        if ((byte & 0x80U) == 0x00U) {
          continue;
        }
        if ((byte & 0xE0U) == 0xC0U) {
          continuation = 1;
        } else if ((byte & 0xF0U) == 0xE0U) {
          continuation = 2;
        } else if ((byte & 0xF8U) == 0xF0U) {
          continuation = 3;
        } else {
          assert(false); // a lone continuation byte: the cut split a glyph
        }
      }
      assert(continuation == 0);
    }
    // A wide cut keeps the widest prefix that fits: "小镇" is four cells and
    // the ellipsis one, so the fifth cell is not wasted on a half character.
    assert(util::ellipsize("小镇姑娘", 5) == "小镇…");
    // With no room for the ellipsis the line is dropped, never overflowed.
    assert(util::ellipsize("Nightfall", 1).empty());
  }

  PlayerState playing;
  playing.state = PlaybackState::Playing;
  playing.elapsed_seconds = 10.0;
  playing.duration_seconds = 20.0;
  playing.sampled_at =
      std::chrono::steady_clock::now() - std::chrono::milliseconds(500);
  const double elapsed =
      effectiveElapsed(playing, std::chrono::steady_clock::now());
  assert(elapsed >= 10.4 && elapsed <= 10.7);
  assert(progressRatio(playing, std::chrono::steady_clock::now()) > 0.5);

  const auto config_path =
      std::filesystem::temp_directory_path() / "termusic-core-test.toml";
  ConfigStore store(config_path);
  Config config;
  // --- Round 45B: keymap hardening -----------------------------------------
  {
    // Every action carries metadata, so no action can have an empty config id.
    std::vector<std::string> ids;
    for (const Action action : allActions()) {
      const std::string_view id = actionId(action);
      assert(!id.empty() && "every action needs a stable config id");
      ids.emplace_back(id);
    }
    std::sort(ids.begin(), ids.end());
    assert(std::adjacent_find(ids.begin(), ids.end()) == ids.end() &&
           "action config ids must be unique");
    assert(Action::None == static_cast<Action>(0));

    // The shipped keymap must be conflict free.
    Keymap keymap;
    keymap.loadDefaults();
    const std::vector<std::string> problems = keymap.validate();
    assert(problems.empty() && "default keymap must validate cleanly");

    // Round 48 defaults: h/l are pane movement, i toggles immersive.
    assert(keymap.primaryBinding(KeyContext::Tree, Action::MoveRight) == "l");
    assert(keymap.primaryBinding(KeyContext::Tracks, Action::MoveLeft) == "h");
    assert(keymap.primaryBinding(std::vector<KeyContext>{KeyContext::Global},
                                 Action::ToggleImmersive) == "i");
    assert(keymap.primaryBinding(std::vector<KeyContext>{KeyContext::Global},
                                 Action::OpenImmersive)
               .empty() &&
           "g n is no longer a default");
    const std::vector<KeyContext> immersive{KeyContext::Immersive,
                                            KeyContext::Global};
    assert(keymap.primaryBinding(immersive, Action::VolumeDown) == "j");
    assert(keymap.primaryBinding(immersive, Action::VolumeUp) == "k");
    assert(keymap.primaryBinding(immersive, Action::Previous) == "h");
    assert(keymap.primaryBinding(immersive, Action::Next) == "l");
    assert(keymap.primaryBinding(immersive, Action::SeekBackward) == ",");
    assert(keymap.primaryBinding(immersive, Action::SeekForward) == ".");


    const auto binds = [&](KeyContext context, const char *action,
                           std::vector<std::string> sequences) {
      return keymap.applyOverride(context, action, sequences);
    };

    // Exact same-context collision: rejected, defaults intact.
    assert(!binds(KeyContext::Tree, "move_up", {"j"}) &&
           "an exact collision must be rejected");
    assert(keymap.primaryBinding(KeyContext::Tree, Action::MoveUp) == "k");
    assert(keymap.primaryBinding(KeyContext::Tree, Action::MoveDown) == "j");

    // Prefix ambiguity, both orders, rejected identically.
    assert(!binds(KeyContext::Tree, "create_playlist", {"g"}) &&
           "a bare prefix that shadows a sequence must be rejected");

    // A valid shared prefix is fine: `g` alone is not an action.
    assert(binds(KeyContext::Global, "open_immersive", {"g n"}));
    assert(binds(KeyContext::Global, "focus_tracks", {"g g"}));

    // Shadowing is context priority, not a conflict: a Global binding is
    // reported as shadowed where a nearer context owns the same key.
    {
      Keymap shadow;
      shadow.loadDefaults();
      // Global "n" is shadowed by Tracks' NextMatch (`n`), which is what makes
      // it a legal-but-shadowed configuration rather than a rejection.
      assert(shadow.applyOverride(KeyContext::Global, "previous_match", {"n"}));
      const auto shadowed =
          shadow.shadowedIn(KeyContext::Global, "n");
      bool tracks_shadows = false;
      for (const auto &[context, action] : shadowed) {
        if (context == KeyContext::Tracks && action == Action::NextMatch)
          tracks_shadows = true;
      }
      assert(tracks_shadows && "Tracks must shadow Global's n");
      // And it is a legal configuration, not a rejected one.
      assert(shadow.validate().empty());
      // Reset restores the shipped binding and clears the custom flag: Global
      // has no default for PreviousMatch, so the binding disappears entirely.
      assert(shadow.resetBinding(KeyContext::Global, Action::PreviousMatch));
      assert(shadow.primaryBinding(KeyContext::Global, Action::PreviousMatch)
                 .empty());
    }

    // Cross-context duplicates are valid: `j` moves in both Tree and Tracks,
    // and conflict checking is context-local.
    assert(binds(KeyContext::Tracks, "move_down", {"j"}));
    assert(keymap.primaryBinding(KeyContext::Tree, Action::MoveDown) == "j");
    assert(keymap.primaryBinding(KeyContext::Tracks, Action::MoveDown) == "j");

    // Invalid token rejected, and it must not disturb other bindings.
    // Round 49: the FIRST key after construction must dispatch, with no
    // warm-up event and no pre-existing pending sequence.
    {
      const std::vector<KeyContext> tree{KeyContext::Tree, KeyContext::Library,
                                         KeyContext::Global};
      const std::vector<KeyContext> global{KeyContext::Global};
      Keymap first;
      first.loadDefaults();
      assert(!first.pending());
      assert(first.feed(tree, "j").action == Action::MoveDown);
      Keymap second;
      second.loadDefaults();
      assert(second.feed(tree, "l").action == Action::MoveRight);
      Keymap third;
      third.loadDefaults();
      assert(third.feed(global, "i").action == Action::ToggleImmersive);
      Keymap fourth;
      fourth.loadDefaults();
      assert(fourth.feed(global, "q").action == Action::Quit);
      Keymap fifth;
      fifth.loadDefaults();
      // Round 50: `H` was the "previous section" shortcut and is gone, so the
      // token resolves to nothing at all rather than to a removed action.
      assert(fifth.feed(global, "H").result == Keymap::Result::None);
      assert(fifth.feed(global, "H").action == Action::None);
    }

    // An invalid token is rejected and leaves the default in place.
    assert(!binds(KeyContext::Tree, "create_playlist", {"Ctrl+Banana"}));
    assert(keymap.primaryBinding(KeyContext::Tree, Action::CreatePlaylist) ==
           "a");

    // User replacement removes the default.
    assert(binds(KeyContext::Tree, "create_playlist", {"z"}));
    assert(keymap.primaryBinding(KeyContext::Tree, Action::CreatePlaylist) == "z");
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Tree}, "z").action ==
           Action::CreatePlaylist);
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Tree}, "a")
               .result == Keymap::Result::None &&
           "a replaced default must no longer resolve");

    // Aliases: both resolve, the first is the advertised primary.
    assert(binds(KeyContext::Tree, "create_playlist", {"z", "a"}));
    assert(keymap.primaryBinding(KeyContext::Tree, Action::CreatePlaylist) == "z");
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Tree}, "a").action ==
           Action::CreatePlaylist);

    // Sequences: shared prefix resolves both, and a dead end resets cleanly.
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Global}, "g").result ==
           Keymap::Result::Pending);
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Global}, "n").action ==
           Action::OpenImmersive);
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Global}, "g").result ==
           Keymap::Result::Pending);
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Global}, "x").result ==
           Keymap::Result::None &&
           "a dead end must not fire an unrelated action");
    assert(!keymap.pending() && "a dead end must clear the pending sequence");
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Global}, "g").result ==
           Keymap::Result::Pending);
    assert(keymap.feed(std::vector<KeyContext>{KeyContext::Global}, "g").action ==
           Action::FocusTracks);

    // A nearer context's prefix outranks a broader context's exact binding.
    // Otherwise a custom global `g` makes Tree's built-in `g g` unreachable.
    {
      Keymap priority;
      priority.loadDefaults();
      const std::vector<KeyContext> tree{KeyContext::Tree,
                                         KeyContext::Library,
                                         KeyContext::Global};
      const std::vector<KeyContext> global{KeyContext::Global};
      assert(priority.applyOverride(KeyContext::Global, "open_immersive",
                                    {"g"}));
      assert(priority.effectOf(KeyContext::Global, Action::OpenImmersive) ==
             Keymap::Effect::Shadowed);
      assert(priority.feed(tree, "g").result == Keymap::Result::Pending);
      assert(priority.feed(tree, "g").action == Action::MoveToFirst);
      assert(priority.feed(global, "g").action == Action::OpenImmersive);

      // Pending chords are scoped to the context in which they started.
      assert(priority.applyOverride(KeyContext::Settings, "focus_tracks",
                                    {"g x"}));
      assert(priority.feed(tree, "g").result == Keymap::Result::Pending);
      assert(priority.feed(std::vector<KeyContext>{KeyContext::Settings,
                                                   KeyContext::Global},
                           "x")
                 .result == Keymap::Result::None);
      assert(!priority.pending());
    }
  }

  // Single keymap authority: the retired Config::keymap is gone, so these
  // invariants are asserted against the live Keymap instead.
  {
    Keymap defaults;
    defaults.loadDefaults();
    const std::vector<KeyContext> global{KeyContext::Global};
    const std::vector<KeyContext> tracks{KeyContext::Tracks, KeyContext::Library,
                                         KeyContext::Global};
    assert(defaults.primaryBinding(tracks, Action::MoveToFirst) == "g g");
    assert(defaults.primaryBinding(tracks, Action::MoveToLast) == "G");
    // Round 50 removals: the global transport, volume, reconnect, database,
    // section and help shortcuts are gone from the shipped keymap.
    assert(defaults.primaryBinding(global, Action::Previous).empty());
    assert(defaults.primaryBinding(global, Action::Next).empty());
    assert(defaults.primaryBinding(global, Action::VolumeUp).empty());
    assert(defaults.primaryBinding(global, Action::VolumeDown).empty());
    assert(defaults.primaryBinding(global, Action::Reconnect).empty());
    assert(defaults.primaryBinding(global, Action::UpdateDatabase).empty());
    assert(defaults.primaryBinding(global, Action::SectionPrevious).empty());
    assert(defaults.primaryBinding(global, Action::SectionNext).empty());
    assert(defaults.primaryBinding(global, Action::Help).empty());
    // `q` survives as the reserved quit: one fixed binding, no row.
    assert(defaults.primaryBinding(global, Action::Quit) == "q");
    assert(defaults.primaryBinding(global, Action::TogglePlay) == "Space");
    // Retired page-model commands must have no binding at all.
    assert(defaults.primaryBinding(global, Action::NewPlaylist).empty());
    assert(defaults.primaryBinding(global, Action::AddToQueue).empty());
    assert(defaults.primaryBinding(global, Action::LoadPlaylistToQueue).empty());
    assert(defaults.primaryBinding(global, Action::ToggleQueue).empty());
    // `a` creates a playlist from either Library pane, but never in Visual.
    const std::vector<KeyContext> tree{KeyContext::Tree, KeyContext::Library,
                                       KeyContext::Global};
    const std::vector<KeyContext> visual{KeyContext::Visual, KeyContext::Global};
    assert(defaults.primaryBinding(tree, Action::CreatePlaylist) == "a");
    assert(defaults.primaryBinding(tracks, Action::CreatePlaylist) == "a");
    assert(defaults.primaryBinding(tracks, Action::MoveQueueItemUp) == "K");
    assert(defaults.primaryBinding(tracks, Action::MoveQueueItemDown) == "J");
    assert(defaults.primaryBinding(visual, Action::CreatePlaylist).empty());
    assert(defaults.primaryBinding(visual, Action::PasteRegister).empty());
    // Visual must not inherit normal-pane activation either.
    assert(defaults.primaryBinding(visual, Action::Activate).empty());
  }

  // --- Round 50: fewer shortcuts, and configurability as metadata ----------
  {
    const auto resolvesTo = [](KeyContext context, const char *token) {
      Keymap probe;
      probe.loadDefaults();
      return probe.feed(context, token);
    };
    const std::vector<KeyContext> global{KeyContext::Global};
    const std::vector<KeyContext> tree{KeyContext::Tree, KeyContext::Library,
                                       KeyContext::Global};
    const std::vector<KeyContext> tracks{KeyContext::Tracks,
                                         KeyContext::Library,
                                         KeyContext::Global};
    const std::vector<KeyContext> settings{KeyContext::Settings,
                                           KeyContext::Global};
    const auto resolvesIn = [&](const std::vector<KeyContext> &chain,
                                const char *token) {
      Keymap probe;
      probe.loadDefaults();
      return probe.feed(chain, token);
    };

    // 1. The removed global shortcuts are no-ops, in every workspace context.
    for (const char *token : {"]", "[", "+", "-", "R", "u", "H", "L", "<", ">",
                              "?"}) {
      assert(resolvesIn(global, token).result == Keymap::Result::None);
      assert(resolvesIn(global, token).action == Action::None);
      assert(resolvesIn(tree, token).action == Action::None);
      assert(resolvesIn(tracks, token).action == Action::None);
    }

    // 2. Tree keeps exactly one spelling per move, and its half-page keys are
    //    PageDown/PageUp. Tracks keeps Ctrl+d/Ctrl+u; Tree does not.
    for (const char *token : {"Down", "Up"}) {
      assert(resolvesIn(tree, token).action == Action::None);
      assert(resolvesIn(tracks, token).action == Action::None);
      assert(resolvesTo(KeyContext::Immersive, token).action == Action::None);
    }
    assert(resolvesIn(tree, "j").action == Action::MoveDown);
    assert(resolvesIn(tree, "k").action == Action::MoveUp);
    assert(resolvesIn(tracks, "j").action == Action::MoveDown);
    assert(resolvesIn(tracks, "k").action == Action::MoveUp);
    assert(resolvesIn(tree, "Ctrl+d").action == Action::None);
    assert(resolvesIn(tree, "Ctrl+u").action == Action::None);
    assert(resolvesIn(tree, "PageDown").action == Action::HalfPageDown);
    assert(resolvesIn(tree, "PageUp").action == Action::HalfPageUp);
    assert(resolvesIn(tracks, "Ctrl+d").action == Action::HalfPageDown);
    assert(resolvesIn(tracks, "Ctrl+u").action == Action::HalfPageUp);
    assert(resolvesIn(tracks, "PageDown").action == Action::HalfPageDown);
    assert(resolvesIn(tracks, "PageUp").action == Action::HalfPageUp);

    // 3. Settings has no `r` reset. The key falls through to Global's repeat
    //    toggle instead of silently resetting a binding.
    assert(resolvesIn(settings, "r").action == Action::ToggleRepeat);
    assert(resolvesTo(KeyContext::Settings, "r").action != Action::ResetBinding);
    {
      Keymap defaults;
      defaults.loadDefaults();
      assert(defaults.primaryBinding(KeyContext::Settings, Action::ResetBinding)
                 .empty());
      // The recovery binding is still shipped: `R` resets all bindings.
      assert(defaults.primaryBinding(KeyContext::Settings,
                                     Action::ResetAllBindings) == "R");
    }

    // 4. Configurability is metadata on the action, not a convention.
    assert(!actionConfigurableIn(Action::Quit, KeyContext::Global));
    assert(!actionConfigurableIn(Action::Help, KeyContext::Global));
    assert(!actionConfigurableIn(Action::Reconnect, KeyContext::Global));
    assert(!actionConfigurableIn(Action::UpdateDatabase, KeyContext::Global));
    assert(!actionConfigurableIn(Action::SectionPrevious, KeyContext::Global));
    assert(!actionConfigurableIn(Action::SectionNext, KeyContext::Global));
    assert(!actionConfigurableIn(Action::ResetBinding, KeyContext::Settings));
    // Transport and volume stay configurable where they are still bound:
    // Immersive, and nowhere else.
    assert(actionConfigurableIn(Action::Next, KeyContext::Immersive));
    assert(actionConfigurableIn(Action::Previous, KeyContext::Immersive));
    assert(actionConfigurableIn(Action::VolumeUp, KeyContext::Immersive));
    assert(actionConfigurableIn(Action::VolumeDown, KeyContext::Immersive));
    assert(!actionConfigurableIn(Action::Next, KeyContext::Global));
    assert(!actionConfigurableIn(Action::VolumeUp, KeyContext::Global));
    // The entries the user keeps.
    assert(actionConfigurableIn(Action::ToggleRepeat, KeyContext::Global));
    assert(actionConfigurableIn(Action::ToggleShuffle, KeyContext::Global));
    assert(actionConfigurableIn(Action::MoveDown, KeyContext::Tree));
    assert(actionConfigurableIn(Action::MoveDown, KeyContext::Tracks));
    assert(actionConfigurableIn(Action::HalfPageDown, KeyContext::Tree));
    // The recovery entry stays configurable through TOML, but is never an
    // editable row.
    assert(actionConfigurableIn(Action::ResetAllBindings, KeyContext::Settings));
    assert(!actionEditable(Action::ResetAllBindings));

    // 5. An obsolete override is refused and leaves the shipped binding alone.
    //    This is the user's old `[keybindings.global] quit = "l j"`.
    {
      Keymap legacy;
      legacy.loadDefaults();
      std::string problem;
      assert(!legacy.applyOverride(KeyContext::Global, "quit", {"l j"},
                                   &problem));
      assert(problem.find("not configurable") != std::string::npos);
      assert(legacy.primaryBinding(KeyContext::Global, Action::Quit) == "q");
      assert(legacy.feed(global, "q").action == Action::Quit);
      for (const char *action :
           {"next", "previous", "volume_up", "volume_down", "reconnect",
            "update_database", "section_previous", "section_next", "help"}) {
        std::string why;
        assert(!legacy.applyOverride(KeyContext::Global, action, {"z"}, &why));
        assert(why.find("not configurable") != std::string::npos);
      }
      assert(!legacy.applyOverride(KeyContext::Settings, "reset_binding",
                                   {"z"}));
      // Nothing was mutated by the refusals.
      assert(legacy.validate().empty());
      assert(legacy.primaryBinding(global, Action::ToggleRepeat) == "r");
      assert(legacy.primaryBinding(global, Action::ToggleImmersive) == "i");
    }

    // 6. The reserved quit key cannot be taken by any other action, in any
    //    context, and a chord containing it is refused too: `q` would quit
    //    before the chord could ever complete.
    {
      for (const KeyContext context :
           {KeyContext::Global, KeyContext::Tree, KeyContext::Tracks,
            KeyContext::Visual, KeyContext::Immersive, KeyContext::Settings,
            KeyContext::Library}) {
        Keymap reserved;
        reserved.loadDefaults();
        std::string problem;
        assert(!reserved.applyOverride(context, "create_playlist", {"q"},
                                       &problem));
        assert(problem.find("reserved") != std::string::npos);
        assert(!reserved.applyOverride(context, "move_down", {"g q"}));
      }
      Keymap reserved;
      reserved.loadDefaults();
      assert(reserved.primaryBinding(KeyContext::Tree, Action::MoveDown) == "j");
      assert(reserved.feed(tree, "q").action == Action::Quit);
      assert(reserved.validate().empty());
    }

    // 7. The editor's row filter, asserted where it is decided. A removed
    //    entry produces NO row -- neither an editable one nor a blank one.
    {
      const auto showsRow = [](const Keymap &map, KeyContext context,
                               Action action) {
        return actionEditable(action) &&
               actionConfigurableIn(action, context) &&
               !map.bindingsFor(context, action).empty();
      };
      Keymap map;
      map.loadDefaults();
      assert(!showsRow(map, KeyContext::Global, Action::Quit));
      assert(!showsRow(map, KeyContext::Global, Action::Help));
      assert(!showsRow(map, KeyContext::Global, Action::Next));
      assert(!showsRow(map, KeyContext::Global, Action::Previous));
      assert(!showsRow(map, KeyContext::Global, Action::VolumeUp));
      assert(!showsRow(map, KeyContext::Global, Action::VolumeDown));
      assert(!showsRow(map, KeyContext::Global, Action::Reconnect));
      assert(!showsRow(map, KeyContext::Global, Action::UpdateDatabase));
      assert(!showsRow(map, KeyContext::Global, Action::SectionPrevious));
      assert(!showsRow(map, KeyContext::Global, Action::SectionNext));
      assert(!showsRow(map, KeyContext::Settings, Action::ResetBinding));
      assert(!showsRow(map, KeyContext::Settings, Action::ResetAllBindings));
      // The complete Global section after the cleanup: seven editable rows.
      const std::vector<std::pair<Action, const char *>> expected_global = {
          {Action::PageLibrary, "1"},   {Action::PageSettings, "2"},
          {Action::TogglePlay, "Space"}, {Action::ToggleRepeat, "r"},
          {Action::ToggleShuffle, "s"}, {Action::Cancel, "Esc"},
          {Action::ToggleImmersive, "i"},
      };
      int rows = 0;
      for (const auto &[action, binding] : map.listing(KeyContext::Global)) {
        (void)binding;
        // `listing()` is the raw keymap; the reserved quit binding is in there
        // but must never become a row.
        if (!showsRow(map, KeyContext::Global, action))
          continue;
        ++rows;
      }
      assert(rows == static_cast<int>(expected_global.size()));
      assert(map.primaryBinding(KeyContext::Global, Action::Quit) == "q");
      assert(!showsRow(map, KeyContext::Global, Action::Quit));
      for (const auto &[action, binding] : expected_global)
        assert(map.primaryBinding(KeyContext::Global, action) == binding);
      // Removed aliases shrink the binding LISTS rather than adding rows.
      assert(map.bindingsFor(KeyContext::Tree, Action::MoveDown).size() == 1U);
      assert(map.bindingsFor(KeyContext::Tree, Action::MoveUp).size() == 1U);
      assert(map.bindingsFor(KeyContext::Tree, Action::HalfPageDown).size() ==
             1U);
      assert(map.bindingsFor(KeyContext::Tree, Action::HalfPageUp).size() == 1U);
      assert(map.bindingsFor(KeyContext::Tracks, Action::MoveDown).size() == 1U);
      assert(map.bindingsFor(KeyContext::Tracks, Action::HalfPageDown).size() ==
             2U);
      assert(map.bindingsFor(KeyContext::Tracks, Action::HalfPageUp).size() ==
             2U);
      // Immersive is untouched by this round.
      const std::vector<KeyContext> immersive{KeyContext::Immersive,
                                              KeyContext::Global};
      assert(map.primaryBinding(immersive, Action::Previous) == "h");
      assert(map.primaryBinding(immersive, Action::Next) == "l");
      assert(map.primaryBinding(immersive, Action::VolumeDown) == "j");
      assert(map.primaryBinding(immersive, Action::VolumeUp) == "k");
      assert(map.primaryBinding(immersive, Action::SeekBackward) == ",");
      assert(map.primaryBinding(immersive, Action::SeekForward) == ".");
      assert(map.primaryBinding(immersive, Action::TogglePlay) == "Space");
      assert(map.primaryBinding(immersive, Action::ToggleImmersive) == "i");
    }
  }

  config.seek_step = 9;
  config.mpd_host = "musicbox";
  config.visualizer_fifo = "/tmp/test-mpd.fifo";
  config.theme_name = "monokai-pro";
  config.plugins_enabled = false;
  config.plugin_directory = "/tmp/termusic-plugins";
  config.plugin_settings["lyrics"]["provider"] = "local";
  config.plugin_settings["lyrics"]["token"] = "part#1\\\"quoted\\path\nnext";
  config.mpd_password = "secret#value\\\"with\\slashes\t";
  config.keybindings["global"]["toggle_immersive"] = {"i", "Ctrl+i"};
  config.keybindings["immersive"]["seek_backward"] = {","};
  std::string error;
  assert(store.save(config, &error));
  Config loaded = store.load(&error);
  assert(loaded.seek_step == 9);
  assert(loaded.mpd_host == "musicbox");
  assert(loaded.visualizer_fifo == "/tmp/test-mpd.fifo");
  assert(loaded.theme_name == "monokai-pro");
  assert(!loaded.plugins_enabled);
  assert(loaded.plugin_directory == "/tmp/termusic-plugins");
  assert(loaded.plugin_settings.at("lyrics").at("provider") == "local");
  assert(loaded.plugin_settings.at("lyrics").at("token") ==
         config.plugin_settings.at("lyrics").at("token"));
  assert(loaded.mpd_password == config.mpd_password);
  assert(loaded.keybindings.at("global").at("toggle_immersive") ==
         config.keybindings.at("global").at("toggle_immersive"));
  assert(loaded.keybindings.at("immersive").at("seek_backward") ==
         config.keybindings.at("immersive").at("seek_backward"));
  std::filesystem::remove(config_path);

  // ---------------------------------------------------------------------------
  // Release configuration contract.
  //
  // Everything here is about the FILE and the RESOLUTION, not the UI: the
  // generated default document must parse back to the built-in defaults, an
  // unrelated (or unknown) setting must survive a save, the write must be
  // atomic, invalid values must be reported instead of crashing, and the
  // documented precedence (command line > environment > file > defaults) must
  // hold exactly.
  // ---------------------------------------------------------------------------
  {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("termusic-release-config-" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto file = directory / "config.toml";

    // 1. --print-default-config text is a valid document that means exactly
    //    "built-in defaults": it can be written out and handed straight back.
    {
      std::ofstream out(file);
      out << defaultConfigText();
      out.close();
      ConfigStore local_store(file);
      const ConfigLoad load = local_store.loadDetailed();
      assert(load.ok());
      assert(load.warnings.empty());
      const Config defaults;
      const Config &parsed = load.config;
      assert(parsed.schema_version == defaults.schema_version);
      assert(parsed.start_page == defaults.start_page);
      assert(parsed.stop_on_exit == defaults.stop_on_exit);
      assert(parsed.seek_step == defaults.seek_step);
      assert(parsed.volume_step == defaults.volume_step);
      assert(parsed.mpd_host == defaults.mpd_host);
      assert(parsed.mpd_port == defaults.mpd_port);
      assert(parsed.mpd_timeout_ms == defaults.mpd_timeout_ms);
      assert(parsed.auto_reconnect == defaults.auto_reconnect);
      assert(parsed.theme_name == defaults.theme_name);
      assert(parsed.icon_set == defaults.icon_set);
      // `visualizer.style` and `visualizer.enabled` no longer exist: the
      // Spectrum is the only visualizer and the display mode decides whether it
      // is shown, so a stored value for either is read and ignored. The keys
      // that remain must still round-trip.
      assert(parsed.visualizer_palette == defaults.visualizer_palette);
      assert(parsed.visualizer_refresh_hz == defaults.visualizer_refresh_hz);
      assert(parsed.visualizer_bar_density == defaults.visualizer_bar_density);
      assert(parsed.visualizer_fifo == defaults.visualizer_fifo);
      assert(parsed.history_enabled == defaults.history_enabled);
      assert(parsed.history_max_entries == defaults.history_max_entries);
      assert(parsed.plugins_enabled == defaults.plugins_enabled);
      assert(parsed.mpd_password.empty());
      assert(parsed.preserved.empty());
    }

    // 2. A setting termusic does not model survives a save, whether it sits in
    //    a section termusic knows or in one it has never heard of.
    {
      std::ofstream out(file);
      out << "schema_version = 1\n"
             "[mpd]\nport = 6601\n"
             "future_option = \"kept\"\n"
             "[future_section]\n"
             "shiny = \"yes\"\n"
             "count = 3\n"
             "[appearance]\ntheme = \"catppuccin-macchiato\"\n";
      out.close();
      ConfigStore local_store(file);
      Config load = local_store.load();
      assert(load.mpd_port == 6601);
      assert(load.theme_name == "catppuccin-macchiato");
      assert(load.preserved.size() == 3U);
      load.seek_step = 11; // an unrelated change, as Core would make
      std::string save_error;
      assert(local_store.save(load, &save_error));
      Config again = local_store.load();
      assert(again.seek_step == 11);
      assert(again.mpd_port == 6601);
      assert(again.theme_name == "catppuccin-macchiato");
      assert(again.preserved.size() == 3U);
      bool kept_mpd = false;
      bool kept_section = false;
      for (const auto &entry : again.preserved) {
        if (entry.section == "mpd" && entry.key == "future_option" &&
            entry.value == "\"kept\"")
          kept_mpd = true;
        if (entry.section == "future_section" && entry.key == "shiny" &&
            entry.value == "\"yes\"")
          kept_section = true;
      }
      assert(kept_mpd);
      assert(kept_section);
    }

    // 3. Atomic: no temporary file is left behind, and a save that cannot
    //    replace the file leaves the previous contents intact.
    {
      ConfigStore local_store(file);
      Config before = local_store.load();
      std::string save_error;
      assert(local_store.save(before, &save_error));
      assert(!std::filesystem::exists(file.string() + ".tmp"));
      const std::string original = [&] {
        std::ifstream in(file);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
      }();
      // A directory where the file should be cannot be replaced by a rename.
      const auto blocked = directory / "blocked";
      std::filesystem::create_directories(blocked);
      ConfigStore blocked_store(blocked);
      std::string blocked_error;
      assert(!blocked_store.save(before, &blocked_error));
      assert(!blocked_error.empty());
      assert(std::filesystem::is_directory(blocked));
      std::ifstream in(file);
      const std::string after((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
      assert(after == original);
    }

    // 4. Validation: nonsense is reported with the value that was used instead,
    //    and nothing crashes.
    {
      std::ofstream out(file);
      out << "schema_version = 1\n"
             "[mpd]\nport = 99999\ntimeout_ms = -5\n"
             "[general]\nseek_step = 900\nvolume_step = 0\n"
             "[visualizer]\nbar_density = 4000\nrefresh_hz = 2\n"
             "[history]\nmax_entries = 0\n";
      out.close();
      ConfigStore local_store(file);
      const ConfigLoad load = local_store.loadDetailed();
      assert(!load.ok());
      assert(load.errors.size() >= 6U);
      // Every invalid value falls back to the SAFE default, not to a clamp.
      assert(load.config.mpd_port == kDefaultMpdPort);
      assert(load.config.mpd_timeout_ms == kDefaultMpdTimeoutMs);
      assert(load.config.seek_step == 5);
      assert(load.config.volume_step == 5);
      assert(load.config.visualizer_bar_density == 64);
      assert(load.config.visualizer_refresh_hz == 60);
      assert(load.config.history_max_entries == 100);
      // A password never appears in a diagnostic, whatever went wrong.
      for (const auto &message : load.errors)
        assert(message.find("password") == std::string::npos);
    }

    // 5. Precedence: command line > environment > file > defaults, and the
    //    environment's password@host form is honored (and hidden).
    {
      Config from_file;
      from_file.mpd_host = "10.0.0.1";
      from_file.mpd_port = 6601;
      from_file.mpd_password = "file-secret";
      Environment environment;
      CliOverrides cli;

      ConnectionSettings resolved =
          resolveConnection(from_file, cli, environment, true);
      assert(resolved.host == "10.0.0.1");
      assert(resolved.port == 6601);
      assert(resolved.host_source == "config.toml");
      assert(resolved.password == "file-secret");

      environment.mpd_host = "192.168.1.20";
      environment.mpd_port = "6602";
      resolved = resolveConnection(from_file, cli, environment, true);
      assert(resolved.host == "192.168.1.20");
      assert(resolved.port == 6602);
      assert(resolved.host_source == "environment (MPD_HOST)");
      assert(resolved.port_source == "environment (MPD_PORT)");

      cli.host_set = true;
      cli.host = "10.0.0.8";
      cli.port_set = true;
      cli.port = 6621;
      resolved = resolveConnection(from_file, cli, environment, true);
      assert(resolved.host == "10.0.0.8");
      assert(resolved.port == 6621);
      assert(resolved.host_source == "command line");

      // MPD_HOST may carry the password, the way every other MPD client reads
      // it, and it wins over the file's password.
      environment.mpd_host = "env-secret@192.168.1.30";
      cli.host_set = false;
      cli.port_set = false;
      resolved = resolveConnection(from_file, cli, environment, true);
      assert(resolved.host == "192.168.1.30");
      assert(resolved.password == "env-secret");

      // An absent file must not be reported as the source of a value.
      Config defaults;
      resolved = resolveConnection(defaults, cli, Environment{}, false);
      assert(resolved.host == kDefaultMpdHost);
      assert(resolved.port == kDefaultMpdPort);
      assert(resolved.host_source == "default");
      assert(resolved.timeout_ms == kDefaultMpdTimeoutMs);

      // The never-zero timeout contract survives every layer.
      cli.timeout_set = true;
      cli.timeout_ms = 0;
      resolved = resolveConnection(defaults, cli, Environment{}, false);
      assert(resolved.timeout_ms == kDefaultMpdTimeoutMs);
    }

    // 6. The diagnostics log masks a secret and never grows a second line.
    {
      // The value is masked, the quoting and the key are kept: the line still
      // reads as the sentence it was, without the secret.
      assert(DiagnosticsLog::redact("mpd: password = \"hunter2\" rejected") ==
             "mpd: password = \"<hidden>\" rejected");
      assert(DiagnosticsLog::redact("password: hunter2") ==
             "password: <hidden>");
      // The libmpdclient "password@host" form: the secret goes, the host
      // stays, because a host name is what makes the line diagnosable.
      assert(DiagnosticsLog::redact("MPD_HOST=secret@host") ==
             "MPD_HOST=<hidden>@host");
      assert(DiagnosticsLog::redact("MPD_HOST=musicbox.lan") ==
             "MPD_HOST=musicbox.lan");
      assert(DiagnosticsLog::redact("nothing to hide") == "nothing to hide");
      assert(DiagnosticsLog::redact("password_file = \"/etc/x\"") ==
             "password_file = \"/etc/x\"");
      const auto log_path = directory / "cache" / "termusic.log";
      DiagnosticsLog log(log_path);
      log.write("first\nsecond");
      log.write("mpd: password: \"secret\" failed");
      std::ifstream in(log_path);
      std::string first;
      std::string second;
      std::getline(in, first);
      std::getline(in, second);
      assert(first.find("first second") != std::string::npos);
      assert(second.find("secret") == std::string::npos);
      assert(second.find("<hidden>") != std::string::npos);
      std::string extra;
      assert(!std::getline(in, extra));
    }

    std::filesystem::remove_all(directory);
  }

#if !defined(_WIN32)
  // 7. XDG resolution: the documented variables are honored, a relative value
  //    is ignored (as the specification requires), and no path is invented
  //    inside the working directory.
  {
    const auto saved_config = std::getenv("XDG_CONFIG_HOME");
    const auto saved_data = std::getenv("XDG_DATA_HOME");
    const auto saved_cache = std::getenv("XDG_CACHE_HOME");
    const auto saved_home = std::getenv("HOME");
    const auto restore = [](const char *name, const char *value) {
      if (value != nullptr)
        ::setenv(name, value, 1);
      else
        ::unsetenv(name);
    };

    ::setenv("HOME", "/home/example", 1);
    ::unsetenv("XDG_CONFIG_HOME");
    ::unsetenv("XDG_DATA_HOME");
    ::unsetenv("XDG_CACHE_HOME");
    AppPaths paths = resolveAppPaths();
    assert(paths.configFile() ==
           "/home/example/.config/termusic/config.toml");
    assert(paths.data_directory == "/home/example/.local/share/termusic");
    assert(paths.cache_directory == "/home/example/.cache/termusic");
    assert(paths.pluginDirectory() ==
           "/home/example/.local/share/termusic/plugins");
    assert(paths.historyFile() ==
           "/home/example/.local/share/termusic/history.toml");

    ::setenv("XDG_CONFIG_HOME", "/xdg/config", 1);
    ::setenv("XDG_DATA_HOME", "/xdg/data", 1);
    ::setenv("XDG_CACHE_HOME", "/xdg/cache", 1);
    paths = resolveAppPaths();
    assert(paths.configFile() == "/xdg/config/termusic/config.toml");
    assert(paths.data_directory == "/xdg/data/termusic");
    assert(paths.cache_directory == "/xdg/cache/termusic");

    // A relative XDG value is ignored, so termusic can never be talked into
    // writing into the working directory.
    ::setenv("XDG_CONFIG_HOME", ".", 1);
    paths = resolveAppPaths();
    assert(paths.configFile() ==
           "/home/example/.config/termusic/config.toml");

    // --config wins over everything, and an explicit relative path is honored
    // because the user typed it.
    paths = resolveAppPaths("/tmp/explicit.toml");
    assert(paths.configFile() == "/tmp/explicit.toml");
    paths = resolveAppPaths("local.toml");
    assert(paths.configFile() == "local.toml");

    ::unsetenv("XDG_CONFIG_HOME");
    ::setenv("HOME", "", 1);
    ::unsetenv("XDG_CONFIG_HOME");
    ::unsetenv("XDG_DATA_HOME");
    paths = resolveAppPaths();
    assert(paths.configFile().empty());
    assert(paths.historyFile().empty());

    restore("XDG_CONFIG_HOME", saved_config);
    restore("XDG_DATA_HOME", saved_data);
    restore("XDG_CACHE_HOME", saved_cache);
    restore("HOME", saved_home);
  }
#endif

  // --- Automatic reconnect: a long-lived, bounded ladder ---------------------
  {
    // The ladder must never be empty, never be zero (a tight loop) and never
    // grow past half a minute.
    assert(reconnectDelay(0) == std::chrono::milliseconds(3000));
    assert(reconnectDelay(1) == std::chrono::milliseconds(3000));
    assert(reconnectDelay(2) == std::chrono::milliseconds(5000));
    assert(reconnectDelay(3) == std::chrono::milliseconds(10000));
    assert(reconnectDelay(4) == std::chrono::milliseconds(15000));
    assert(reconnectDelay(5) == std::chrono::milliseconds(30000));
    // There is no attempt limit: the delay stays at the cap forever instead of
    // ever refusing to try again.
    for (int attempt : {6, 20, 500, 100000}) {
      assert(reconnectDelay(attempt) == std::chrono::milliseconds(30000));
    }
    assert(std::string(reconnectLadderText()).find("3s") == 0U);
  }

  ui::ThemeRegistry themes;
  assert(themes.list().size() == 7U);
  // Catppuccin Mocha is the default theme, and it is registered first so that
  // an unknown or legacy id (an old `theme = "nord"` configuration included)
  // resolves to it.
  assert(themes.resolveId("missing") == "catppuccin-mocha");
  assert(themes.resolveId("default") == "catppuccin-mocha");
  assert(themes.resolveId("nord") == "catppuccin-mocha");
  // The cycle walks the registration order and wraps at the end.
  assert(themes.nextId("catppuccin-mocha") == "kanagawa");
  assert(themes.nextId("catppuccin-macchiato") == "catppuccin-mocha");
  {
    const ui::Theme &mocha = themes.resolve("catppuccin-mocha");
    const ui::Theme &kanagawa = themes.resolve("kanagawa");
    // A few load-bearing Mocha values, asserted against the official palette.
    assert(mocha.background == ftxui::Color::RGB(0x1E, 0x1E, 0x2E)); // Base
    assert(mocha.background_deep == ftxui::Color::RGB(0x11, 0x11, 0x1B)); // Crust
    assert(mocha.text == ftxui::Color::RGB(0xCD, 0xD6, 0xF4));
    assert(mocha.tree_cursor_bg == ftxui::Color::RGB(0xF5, 0xC2, 0xE7)); // Pink
    assert(mocha.track_cursor_bg == ftxui::Color::RGB(0xCB, 0xA6, 0xF7)); // Mauve
    assert(mocha.playing == ftxui::Color::RGB(0x89, 0xDC, 0xEB));        // Sky
    assert(mocha.active_collection ==
           ftxui::Color::RGB(0x74, 0xC7, 0xEC)); // Sapphire
    assert(mocha.progress_filled == ftxui::Color::RGB(0xCB, 0xA6, 0xF7));
    assert(mocha.volume_fill == ftxui::Color::RGB(0x94, 0xE2, 0xD5)); // Teal
    assert(mocha.error == ftxui::Color::RGB(0xF3, 0x8B, 0xA8));       // Red
    // The two cursors must never collapse to the same colour.
    assert(mocha.tree_cursor_bg != mocha.track_cursor_bg);
    // The active-collection marker and the playing marker stay distinguishable.
    assert(mocha.active_collection != mocha.playing);
    // Kanagawa keeps its own palette rather than inheriting Mocha values.
    assert(kanagawa.background == ftxui::Color::RGB(0x1F, 0x1F, 0x28));
    assert(kanagawa.background != mocha.background);
    assert(kanagawa.text != mocha.text);
    assert(kanagawa.border != mocha.border);
  }

  extensions::ExtensionRegistry extensions;
  extensions::SettingDescriptor setting;
  setting.id = "test.enabled";
  setting.section = "Test";
  setting.label = "Enabled";
  assert(extensions.registerSetting(setting));
  setting.label = "Duplicate";
  assert(!extensions.registerSetting(setting));
  assert(extensions.registerUiBlock(
      {.id = "test.second",
       .slot = "settings.extensions",
       .title = "Second",
       .order = 20,
       .render_text = [] { return std::string("second"); }}));
  assert(extensions.registerUiBlock(
      {.id = "test.first",
       .slot = "settings.extensions",
       .title = "First",
       .order = 10,
       .render_text = [] { return std::string("first"); }}));
  const auto blocks = extensions.blocksFor("settings.extensions");
  assert(blocks.size() == 2U);
  assert(blocks.front().get().id == "test.first");

  VisualizerAnalyzer analyzer;
  constexpr int channels = 2;
  constexpr int sample_rate = 44100;
  std::vector<std::int16_t> pcm(2048U * channels);
  for (std::size_t frame = 0; frame < 2048U; ++frame) {
    const double wave = std::sin(2.0 * std::numbers::pi * 440.0 *
                                 static_cast<double>(frame) / sample_rate);
    const auto sample = static_cast<std::int16_t>(wave * 20000.0);
    pcm[frame * channels] = sample;
    pcm[frame * channels + 1U] = sample;
  }
  analyzer.analyzePcm(pcm);
  const SpectrumSnapshot spectrum = analyzer.snapshot();
  assert(spectrum.bars.size() == 64U);
  assert(*std::max_element(spectrum.bars.begin(), spectrum.bars.end()) > 0.0F);

  // --- Human-readable sizes and play times ---------------------------------
  // The Library table shows a real byte count from MPD, never a guess: an
  // unknown size stays "--" and an unknown timestamp stays "--".
  {
    assert(termusic::util::formatFileSize(0.0) == "--");
    assert(termusic::util::formatFileSize(-1.0) == "--");
    assert(termusic::util::formatFileSize(872.0) == "872 B");
    assert(termusic::util::formatFileSize(1024.0) == "1.0 KB");
    assert(termusic::util::formatFileSize(1536.0) == "1.5 KB");
    assert(termusic::util::formatFileSize(1024.0 * 1024.0 * 45.2) == "45.2 MB");
    assert(termusic::util::formatFileSize(1024.0 * 1024.0 * 1024.0 * 1.44) ==
           "1.4 GB");
    // The widest value the nine-cell Size column has to hold.
    const std::string widest = termusic::util::formatFileSize(
        1024.0 * 1024.0 * 1024.0 * 1024.0 * 1023.9);
    assert(termusic::util::displayWidth(widest) <= 9);

    assert(termusic::util::formatPlayedAt(0) == "--");
    assert(termusic::util::formatPlayedAt(-5) == "--");
    const std::string played = termusic::util::formatPlayedAt(1700000000LL);
    // Locale-independent structure: "YYYY-MM-DD HH:MM:SS".
    assert(played.size() == 19);
    assert(played[4] == '-' && played[7] == '-' && played[10] == ' ');
    assert(played[13] == ':' && played[16] == ':');
    assert(termusic::util::displayWidth(played) == 19);
    for (const char c : played)
      assert((c >= '0' && c <= '9') || c == '-' || c == ' ' || c == ':');
  }

  // --- Core section kinds AND order ----------------------------------------
  // The tree's children carry their semantic kind in the MODEL, so the renderer
  // maps a kind to an icon and never looks a section up by name. The ORDER of
  // kCoreSections is the order of the tree and of the panes: frequently used
  // configuration first, the two read-only pages last, and no Connection entry
  // (the MPD server settings belong to General).
  {
    int information = 0;
    int settings = 0;
    for (const ConfigFileEntry &entry : kCoreSections) {
      if (entry.kind == CoreSectionKind::Information)
        ++information;
      else
        ++settings;
    }
    assert(information == 2 && "About and Help are the information sections");
    assert(settings == static_cast<int>(kCoreSections.size()) - 2);
    // Each entry's `section` index must equal its position, because the tree
    // row and the pane are wired through that index.
    for (std::size_t index = 0; index < kCoreSections.size(); ++index) {
      assert(kCoreSections[index].section == static_cast<int>(index));
    }
    // The canonical order, by stable id.
    const std::array<std::string_view, 6> expected_order = {
        "core:general", "core:appearance", "core:keybindings",
        "core:plugins", "core:about",    "core:help"};
    for (std::size_t index = 0; index < expected_order.size(); ++index) {
      assert(kCoreSections[index].id == expected_order[index]);
    }
    // General is first, Help is last, and nothing is called Connection.
    assert(kCoreSections.front().id == "core:general");
    assert(kCoreSections.back().id == "core:help");
    for (const ConfigFileEntry &entry : kCoreSections) {
      assert(entry.id != "core:connection");
      assert(entry.label != "Connection");
    }
    // The two information entries are About and Help, named by ID (not by
    // label text); every other entry is a settings module.
    for (const ConfigFileEntry &entry : kCoreSections) {
      const bool document = entry.id == "core:help" || entry.id == "core:about";
      assert((entry.kind == CoreSectionKind::Information) == document);
    }
    // The tree copies the kind onto the nodes it builds, which is what the
    // renderer reads. `core` starts collapsed, so the root is expanded first --
    // exactly what pressing `2` does in the application.
    termusic::WorkspaceTree tree;
    for (std::size_t index = 0; index < tree.visible().size(); ++index) {
      if (tree.visible()[index].id == "core")
        tree.setCursor(static_cast<int>(index));
    }
    tree.expandCurrentRoot();
    int children = 0;
    for (const termusic::TreeNode &node : tree.visible()) {
      if (node.type != termusic::TreeNodeType::CoreSection)
        continue;
      ++children;
      // The tree must present them in the SAME order as the registry.
      assert(node.id == kCoreSections[static_cast<std::size_t>(children - 1)].id);
      const bool document = node.id == "core:help" || node.id == "core:about";
      assert(document || node.core_kind == CoreSectionKind::Settings);
      if (document)
        assert(node.core_kind == CoreSectionKind::Information);
    }
    assert(children == static_cast<int>(kCoreSections.size()));
    std::cout << "core: " << information << " information + " << settings
              << " settings sections, kinds carried by the tree nodes\n";
  }

  // --- Built-in themes -----------------------------------------------------
  // The registry IS the Appearance -> Theme list: its order is the order the
  // choice offers, the first entry is the default, and an unknown id (an old
  // configuration that still says "nord", for instance) falls back to it.
  {
    termusic::ui::ThemeRegistry builtins;
    const auto list = builtins.list();
    assert(list.size() == 7);
    const std::array<std::string_view, 7> expected = {
        "catppuccin-mocha", "kanagawa", "material-palenight", "monokai-pro",
        "github-dark",      "oxocarbon", "catppuccin-macchiato"};
    for (std::size_t index = 0; index < expected.size(); ++index) {
      assert(list[index].id == expected[index]);
      assert(!list[index].name.empty());
    }
    for (const termusic::ui::ThemeInfo &info : list)
      assert(info.id != "nord");
    // The default theme is the FIRST entry, and it is what an unknown id
    // resolves to -- the fallback that keeps an old `theme = "nord"` config
    // working without a migration.
    assert(builtins.resolveId("catppuccin-mocha") == "catppuccin-mocha");
    assert(builtins.resolveId("nord") == "catppuccin-mocha");
    assert(builtins.resolveId("no-such-theme") == "catppuccin-mocha");
    for (const std::string_view id : expected) {
      const termusic::ui::Theme &theme = builtins.resolve(std::string(id));
      // Every built-in must define its own focus/active pair: the focus band
      // and the active-collection tint are never the same colour, and neither
      // is left at another palette's default.
      assert(theme.tree_cursor_bg != theme.active_collection);
    }
    const termusic::ui::Theme &legacy = builtins.resolve("nord");
    const termusic::ui::Theme &fallback_mocha =
        builtins.resolve("catppuccin-mocha");
    assert(legacy.tree_cursor_bg == fallback_mocha.tree_cursor_bg);
    std::cout << "themes: " << list.size()
              << " built-ins, default catppuccin-mocha, unknown id falls back\n";
  }

  // --- Icons ---------------------------------------------------------------
  // One table for the whole UI: every icon resolves in BOTH families (so a
  // terminal without a Nerd Font degrades instead of showing a box), content
  // icons are exactly one cell wide, and the fallbacks are plain Unicode
  // symbols rather than emoji, whose width varies between terminals.
  {
    using termusic::ui::Icon;
    using termusic::ui::IconSet;
    using termusic::ui::iconGlyph;
    using termusic::ui::isContentIcon;
    const Icon all[] = {
        Icon::Shuffle,  Icon::Previous, Icon::Play,     Icon::Pause,
        Icon::Next,     Icon::Repeat,   Icon::Speaker,  Icon::Folder,
        Icon::FolderOpen, Icon::Library, Icon::History, Icon::Playlist,
        Icon::Music,      Icon::Document, Icon::Settings,
    };
    for (const Icon icon : all) {
      for (const IconSet set : {IconSet::NerdFont, IconSet::Unicode}) {
        const std::string glyph = iconGlyph(icon, set);
        assert(!glyph.empty());
        assert(glyph != "?");
        // A CONTENT icon sits in a fixed one-cell column, so it must be exactly
        // one cell wide in both families. The transport fallbacks are two
        // cells wide on purpose: they are drawn inside a fixed button box.
        const int width = termusic::util::displayWidth(glyph);
        assert(isContentIcon(icon) ? width == 1 : width >= 1);
        assert(width <= 2);
        // No emoji presentation, no variation selectors: the width of those
        // depends on the terminal, which would move the column it sits in.
        for (const char c : glyph)
          assert(static_cast<unsigned char>(c) < 0xF0U ||
                 static_cast<unsigned char>(c) == 0xEFU);
      }
    }
    // Content icons are distinct from the transport ones, and the two folder
    // states differ, so a row can never be ambiguous.
    assert(isContentIcon(Icon::Folder) && isContentIcon(Icon::Music));
    assert(!isContentIcon(Icon::Play));
    assert(iconGlyph(Icon::Folder, IconSet::NerdFont) !=
           iconGlyph(Icon::FolderOpen, IconSet::NerdFont));
    assert(iconGlyph(Icon::Music, IconSet::Unicode) == "\u266a");
    std::printf("icons:     both families, one cell, no emoji\n");
  }

  // --- Track-table column budgets ------------------------------------------
  // Each collection kind gets its own columns, and every budget must fit the
  // panel it is drawn in: the marker plus the columns may never exceed the
  // table width, or FTXUI would compress the row and re-flow the table.
  {
    using termusic::ui::computeMetrics;
    using termusic::ui::LayoutMode;
    using termusic::ui::TrackColumns;

    // The columns are drawn in the Track Buffer PANE, so they must fit that
    // pane -- not the terminal. Budgeting them against the terminal width was
    // the bug that pushed Duration and Size past the pane edge.
    const auto check_fit = [](const TrackColumns &columns,
                              const termusic::ui::UiMetrics &m,
                              const char *what) {
      (void)what;
      const int table = std::max(12, m.track_buffer_width - 2) - 2;
      // 2 cells for the "▶ " marker, then the columns.
      assert(2 + columns.total() <= table);
    };

    for (const int width : {60, 76, 100, 120, 150, 180, 240}) {
      const auto m = computeMetrics(width, 46, 2, false);

      // Library carries a file size. On a terminal wide enough for the real
      // nine-cell column it is always shown; below that it shrinks, and on a
      // minimal terminal it is dropped rather than starving the title.
      check_fit(m.library_columns, m, "library");
      assert(m.library_columns.title > 0);
      if (width >= 120) {
        assert(m.library_columns.album > 0);
        assert(m.library_columns.size == 9);
      }
      // The size column is the first thing the Library gives up, and it keeps
      // its full nine cells only while there is room for the text columns.
      assert(m.library_columns.size == (width >= 90 ? 9 : 0));
      // Every non-minimal width keeps the real duration column.
      if (width >= 65)
        assert(m.library_columns.duration > 0);

      // History answers "what, and when": title, artist, played time. It never
      // carries an album, a duration or a size -- dropping those is exactly
      // what pays for the 19-cell timestamp.
      check_fit(m.history_columns, m, "history");
      assert(m.history_columns.title > 0);
      if (width >= 65) {
        assert(m.history_columns.album == 0);
        assert(m.history_columns.duration == 0);
        assert(m.history_columns.size == 0);
        // Every non-minimal width keeps the COMPLETE timestamp: it is the
        // reason the History view exists, so the row number and the artist
        // shrink before it does.
        assert(m.history_columns.played == 19);
        assert(m.history_columns.artist > 0);
        assert(termusic::util::displayWidth("Played At") <=
               m.history_columns.played);
        assert(termusic::util::displayWidth(
                   termusic::util::formatPlayedAt(1789084001LL)) ==
               m.history_columns.played);
      }

      // Playlist shows exactly what `listplaylistinfo` can report: no size and
      // no timestamp, because MPD does not send either for a saved playlist.
      check_fit(m.playlist_columns, m, "playlist");
      assert(m.playlist_columns.title > 0);
      assert(m.playlist_columns.size == 0);
      assert(m.playlist_columns.played == 0);
      assert(m.playlist_columns.duration > 0);
      if (width >= 120)
        assert(m.playlist_columns.album > 0);
    }

    // The layout-mode buckets still land where the responsive design expects.
    assert(computeMetrics(180, 46).mode == LayoutMode::Large);
    assert(computeMetrics(100, 32).mode == LayoutMode::Medium);
    assert(computeMetrics(80, 24).mode == LayoutMode::Compact);
    assert(computeMetrics(50, 18).mode == LayoutMode::Minimal);

    // A minimal terminal keeps one text column and the duration, because a
    // single unreadable column is not a table.
    const auto minimal = computeMetrics(50, 18);
    assert(minimal.library_columns.artist == 0);
    assert(minimal.library_columns.album == 0);
    assert(minimal.library_columns.size == 0);
    assert(minimal.library_columns.played == 0);
    assert(minimal.library_columns.duration == 6);
  }

  return 0;
}
