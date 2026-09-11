#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace termusic {

/// Every user intent the UI can trigger. Views never touch MPD or state
/// directly: they resolve a key or mouse gesture to one of these and hand it to
/// the Controller (requirements v2 §12.4).
enum class Action {
  None,

  // --- Global ---------------------------------------------------------------
  Quit,
  PageLibrary,
  PageSettings,
  Help,
  FocusNext,
  TogglePlay,
  Next,
  Previous,
  SeekForward,
  SeekBackward,
  VolumeUp,
  VolumeDown,
  ToggleRepeat,
  ToggleShuffle,
  Search,
  Cancel,
  Reconnect,
  UpdateDatabase,

  // --- List navigation ------------------------------------------------------
  MoveDown,
  MoveUp,
  MoveToFirst,
  MoveToLast,
  HalfPageDown,
  HalfPageUp,
  Activate,

  // --- Path / playlist navigation -------------------------------------------
  GoParent,
  EnterDirectory,
  PrevPlaylist,
  NextPlaylist,
  ToggleQueue,

  // --- Queue and playlist edits ---------------------------------------------
  AddToQueue,
  RemoveSelected,
  ClearQueue,
  MoveQueueItemUp,
  MoveQueueItemDown,
  LoadPlaylistToQueue,
  AddToPlaylist,
  NewPlaylist,
  RenamePlaylist,
  DeletePlaylist,

  // --- Modal -----------------------------------------------------------------
  Confirm,

  // --- Semantic keymap actions (Round 45A) -----------------------------------
  // Named for what they do, never for the key that happens to trigger them.
  SectionPrevious,
  SectionNext,
  OpenImmersive,
  ToggleImmersive,
  FocusTree,
  FocusTracks,
  MoveLeft,
  MoveRight,
  CreatePlaylist,
  PasteRegister,
  EnterVisual,
  YankCurrent,
  YankSelection,
  DeleteCurrent,
  DeleteSelection,
  CancelVisual,
  OpenSearch,
  NextMatch,
  PreviousMatch,

  // --- Settings keybinding editor -------------------------------------------
  ResetBinding,
  ResetAllBindings,

  /// Sentinel: always last, never a real action. It exists so the metadata
  /// table can be size-checked at compile time -- adding an Action without
  /// adding its ActionInfo is otherwise perfectly legal C++ that silently
  /// zero-fills the entry, leaving the action with an empty config id.
  Count,
};

/// Human readable label, used by the settings/keymap page.
std::string_view actionLabel(Action action);

/// Whether this action is offered as an editable row AT ALL, in any context.
/// False for system/recovery entries that must never go through the
/// Enter-to-record path (see `ResetAllBindings`).
bool actionEditable(Action action);

/// A set of keymap contexts, one bit per context position in `allContexts()`.
/// The `KeyContext` enum lives in app/keymap.hpp, which includes this header,
/// so the bits are POSITIONAL: bit N is `allContexts()[N]`. `kAllContexts` is
/// therefore independent of the enum's declaration order, and the tests pin
/// the order these masks are read against.
using ContextSet = unsigned;

/// Every context: the ordinary case, and the default for an action.
inline constexpr ContextSet kAllContexts = ~0u;

/// No context: the action exists and dispatches, but is not the user's to
/// rebind anywhere.
inline constexpr ContextSet kNoContext = 0u;

/// The contexts in which this action is a USER-CONFIGURABLE keybinding entry:
/// it gets an editable row in the keybinding editor, and a
/// `[keybindings.<context>]` override for it is accepted.
///
/// This is deliberately separate from whether the Action exists. `Quit`,
/// `Help`, `Reconnect` and the section commands all stay valid semantic
/// actions that code may dispatch; they are simply no longer the user's to
/// rebind, so an obsolete override for one of them cannot resurrect a shortcut
/// that was removed.
ContextSet actionConfigurableContexts(Action action);

/// Stable identifier used as the keymap's configuration key.
std::string_view actionId(Action action);

/// Reverse lookup: stable configuration id -> action.
std::optional<Action> actionFromId(std::string_view id);

/// Every bindable action, in declaration order.
const std::vector<Action> &allActions();

} // namespace termusic
