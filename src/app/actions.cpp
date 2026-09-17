#include "app/actions.hpp"

#include <array>

namespace termusic {
namespace {

struct ActionInfo {
  Action action;
  std::string_view id;
  std::string_view label;
  /// False for system/recovery actions that must not be offered as ordinary
  /// editable rows in the keybinding editor. They stay valid Actions and stay
  /// configurable through TOML; they simply are not rebindable from the table.
  bool editable = true;
  /// Contexts where this entry is USER-CONFIGURABLE: an editable row is offered
  /// AND a `[keybindings.<context>]` override is accepted. The bits follow
  /// `allContexts()` order -- see the names below.
  ContextSet configurable = kAllContexts;
};

// allContexts() order, pinned by the assertion at the bottom of this file:
//   Global, Library, Tree, Tracks, Visual, Immersive, Settings
constexpr ContextSet kGlobal = 1u << 0;
constexpr ContextSet kLibrary = 1u << 1;
constexpr ContextSet kTree = 1u << 2;
constexpr ContextSet kTracks = 1u << 3;
constexpr ContextSet kVisual = 1u << 4;
constexpr ContextSet kImmersive = 1u << 5;
constexpr ContextSet kSettings = 1u << 6;

// Single source of truth for action metadata: the keymap settings page and the
// key resolver both read from here.
// Deduced size, never a hand-maintained count.
constexpr auto kActionInfo = std::to_array<ActionInfo>({
    // `q` is the reserved quit key, not a user shortcut: the action stays for
    // dispatch, but there is no editable Quit row and no override for it.
    {Action::Quit, "quit", "Quit", false, kNoContext},
    {Action::PageLibrary, "page_library", "Go to Vault"},
    {Action::PageSettings, "page_settings", "Go to Core"},
    // Help has no shortcut any more: the overlay is only reachable if code
    // dispatches the action directly.
    {Action::Help, "help", "Toggle help", false, kNoContext},
    {Action::FocusNext, "focus_next", "Focus next panel"},
    {Action::TogglePlay, "toggle_play", "Play / Pause"},
    // Transport and volume are reached through the Player Bar and through
    // Immersive; only the immersive bindings are configurable.
    {Action::Next, "next", "Next track", true, kImmersive},
    {Action::Previous, "previous", "Previous track", true, kImmersive},
    {Action::SeekForward, "seek_forward", "Seek forward", true, kImmersive},
    {Action::SeekBackward, "seek_backward", "Seek backward", true, kImmersive},
    {Action::VolumeUp, "volume_up", "Volume up", true, kImmersive},
    {Action::VolumeDown, "volume_down", "Volume down", true, kImmersive},
    {Action::ToggleRepeat, "toggle_repeat", "Toggle repeat", true, kImmersive},
    {Action::ToggleShuffle, "toggle_shuffle", "Toggle shuffle"},
    {Action::Search, "search", "Search"},
    {Action::Cancel, "cancel", "Cancel / close"},
    // The connection actions still exist for the Core > MPD pane and for the
    // backend; their global keyboard shortcuts are gone.
    {Action::Reconnect, "reconnect", "Reconnect to MPD", false, kNoContext},
    {Action::UpdateDatabase, "update_database", "Update MPD database", false,
     kNoContext},
    {Action::MoveDown, "move_down", "Move down"},
    {Action::MoveUp, "move_up", "Move up"},
    {Action::MoveToFirst, "move_first", "Jump to first"},
    {Action::MoveToLast, "move_last", "Jump to last"},
    {Action::HalfPageDown, "half_page_down", "Half page down"},
    {Action::HalfPageUp, "half_page_up", "Half page up"},
    {Action::Activate, "activate", "Activate selection"},
    {Action::GoParent, "go_parent", "Up / seek back"},
    {Action::EnterDirectory, "enter_directory", "Open / seek forward"},
    {Action::PrevPlaylist, "prev_playlist", "Previous playlist"},
    {Action::NextPlaylist, "next_playlist", "Next playlist"},
    {Action::ToggleQueue, "toggle_queue", "Open Default playlist"},
    {Action::AddToQueue, "add_to_queue", "Add to Default"},
    {Action::RemoveSelected, "remove_selected", "Remove selection"},
    {Action::ClearQueue, "clear_queue", "Clear Default"},
    {Action::MoveQueueItemUp, "move_queue_up", "Move Default item up"},
    {Action::MoveQueueItemDown, "move_queue_down", "Move Default item down"},
    {Action::LoadPlaylistToQueue, "load_playlist", "Load playlist into queue"},
    {Action::AddToPlaylist, "add_to_playlist", "Add to playlist"},
    {Action::NewPlaylist, "new_playlist", "New playlist"},
    {Action::RenamePlaylist, "rename_playlist", "Rename playlist"},
    {Action::DeletePlaylist, "delete_playlist", "Delete playlist"},

    // --- Semantic keymap actions (Round 45A) ---------------------------------
    // `1` and `2` are the only top-level navigation now, so the section
    // commands keep no configurable entry.
    {Action::SectionPrevious, "section_previous", "Previous section", false,
     kNoContext},
    {Action::SectionNext, "section_next", "Next section", false, kNoContext},
    {Action::OpenImmersive, "open_immersive", "Open now playing"},
    {Action::ToggleImmersive, "toggle_immersive", "Toggle now playing"},
    {Action::FocusTree, "focus_tree", "Focus library tree"},
    {Action::FocusTracks, "focus_tracks", "Focus track list"},
    {Action::MoveLeft, "move_left", "Move Left"},
    {Action::MoveRight, "move_right", "Move Right"},
    {Action::CreatePlaylist, "create_playlist", "Create playlist"},
    {Action::PasteRegister, "paste_register", "Paste register"},
    {Action::EnterVisual, "enter_visual", "Start visual selection"},
    {Action::YankCurrent, "yank_current", "Yank current track"},
    {Action::YankSelection, "yank_selection", "Yank selection"},
    {Action::DeleteCurrent, "delete_current", "Remove current entry"},
    {Action::DeleteSelection, "delete_selection", "Remove selection"},
    {Action::CancelVisual, "cancel_visual", "Cancel selection"},
    {Action::OpenSearch, "search_tracks", "Search"},
    {Action::NextMatch, "next_match", "Next match"},
    {Action::PreviousMatch, "previous_match", "Previous match"},
    {Action::Confirm, "confirm", "Confirm"},
    // The per-row reset belongs to the fixed footer control now, so the
    // Settings `r` shortcut is gone and cannot come back through config.
    {Action::ResetBinding, "reset_binding", "Reset binding", false, kNoContext},
    {Action::ResetAllBindings, "reset_all_bindings", "Reset all bindings",
     false},
});

// `Action::Count` is the last enumerator, so its numeric value is the number
// of real enumerators including None. Only None carries no metadata; the
// sentinel itself is not an enumerator before it. Adding an Action without
// metadata therefore cannot compile.
static_assert(kActionInfo.size() ==
                  static_cast<std::size_t>(Action::Count) - 1,
              "every Action except Action::None needs an ActionInfo entry");

// The context bits are positional, so reordering or extending allContexts()
// must be reflected here instead of silently shifting every mask.
static_assert(kGlobal == (1u << 0) && kLibrary == (1u << 1) &&
                  kTree == (1u << 2) && kTracks == (1u << 3) &&
                  kVisual == (1u << 4) && kImmersive == (1u << 5) &&
                  kSettings == (1u << 6),
              "the context bits must follow allContexts() order");

} // namespace

std::string_view actionLabel(Action action) {
  for (const auto &info : kActionInfo) {
    if (info.action == action) {
      return info.label;
    }
  }
  return "Unknown";
}

bool actionEditable(Action action) {
  for (const auto &info : kActionInfo) {
    if (info.action == action)
      return info.editable;
  }
  return false;
}

ContextSet actionConfigurableContexts(Action action) {
  for (const auto &info : kActionInfo) {
    if (info.action == action)
      return info.configurable;
  }
  return kNoContext;
}

std::string_view actionId(Action action) {
  for (const auto &info : kActionInfo) {
    if (info.action == action) {
      return info.id;
    }
  }
  return "unknown";
}

std::optional<Action> actionFromId(std::string_view id) {
  for (const auto &info : kActionInfo) {
    if (info.id == id)
      return info.action;
  }
  return std::nullopt;
}

const std::vector<Action> &allActions() {
  static const std::vector<Action> actions = [] {
    std::vector<Action> result;
    result.reserve(kActionInfo.size());
    for (const auto &info : kActionInfo) {
      result.push_back(info.action);
    }
    return result;
  }();
  return actions;
}

} // namespace termusic
