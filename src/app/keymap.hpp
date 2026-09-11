#pragma once

// Round 45A: the configurable keymap layer.
//
//   FTXUI Event -> normalizeKey() -> Keymap -> semantic Action -> dispatcher
//
// Features own actions. The keymap owns keys. UI hints query the keymap, and
// configuration overrides it.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "app/actions.hpp"

namespace ftxui {
class Event;
} // namespace ftxui

namespace termusic {

/// Logical UI contexts. A binding is looked up in the active context first and
/// then in `Global`, so a key can mean different things in different places.
enum class KeyContext {
  Global,
  Library,  // pane-focus chords
  Tree,
  Tracks,
  Visual,
  Immersive,
  Settings,
};

std::string_view contextName(KeyContext context);

/// The lookup order used at runtime for a context: the context itself first,
/// then broader ones. This is the single source for both dispatch and the
/// editor's shadow analysis, so they can never disagree.
const std::vector<KeyContext> &chainFor(KeyContext context);

/// Every context, in display order.
const std::vector<KeyContext> &allContexts();
std::optional<KeyContext> contextFromName(std::string_view name);

/// The one key that always quits and can never be rebound. `Application`
/// checks it before the keymap is consulted, and `Keymap::applyOverride`
/// refuses to give it to any action, so no configuration can take the
/// emergency exit away. Exposed here so the guard, the loader and the tests all
/// name the same token.
inline constexpr std::string_view kReservedQuitKey = "q";

/// Whether (context, action) is a user-configurable entry: it gets an editable
/// row in the keybinding editor, and a `[keybindings.<context>]` override for
/// it is accepted. An Action stays dispatchable when this is false -- this is
/// about configurability, never about the Action existing.
bool actionConfigurableIn(Action action, KeyContext context);

/// Canonical physical key token: "j", "H", "Ctrl+h", "Enter", "Esc", "Tab".
///
/// This is the ONLY place that knows how a terminal event maps to a token. It
/// deliberately does not decide actions.
///
/// Terminal constraint (measured, Round 44): Ctrl+h and Backspace are both
/// delivered as 0x7f / `Event::Backspace`. They are byte-identical, so
/// `text_entry` is what separates them: while text entry owns the keyboard the
/// key is left to the input field, otherwise it normalizes to "Ctrl+h".
std::string normalizeKey(const ftxui::Event &event, bool text_entry);

/// A binding is a space separated sequence of tokens: "j", "g n", "y y".
using KeySequence = std::string;

class Keymap {
public:
  enum class Result { None, Pending, Matched };

  struct Resolution {
    Result result = Result::None;
    Action action = Action::None;
  };

  /// Installs the shipped Vim-inspired keymap.
  void loadDefaults();

  /// Applies one configured binding. Replaces every binding the action had in
  /// that context, so a user list is authoritative rather than additive.
  ///
  /// Rejected -- leaving the defaults fully intact -- for an unknown action, a
  /// malformed sequence, an exact collision with a different action, or a
  /// prefix ambiguity. Nothing is mutated unless the whole override is valid,
  /// so one bad entry can never poison unrelated settings.
  bool applyOverride(KeyContext context, std::string_view action_name,
                     const std::vector<std::string> &sequences,
                     std::string *problem = nullptr);

  /// Feeds one canonical token. Sequences are driven entirely by the
  /// registered bindings: a token only becomes a pending prefix when some
  /// binding actually continues with it.
  Resolution feed(KeyContext context, const std::string &token);

  /// Resolves against a fallback chain: the active context first, then any
  /// broader ones (Library, then Global). This is how the pane-focus chords
  /// stay reachable from both the Tree and the Track list.
  Resolution feed(const std::vector<KeyContext> &chain,
                  const std::string &token);

  /// Drops any pending sequence.
  void reset();

  bool pending() const { return !pending_.empty(); }

  /// Bumped by every mutation, so a view that mirrors the keymap can rebuild
  /// only when something actually changed.
  std::size_t revision() const { return revision_; }

  /// Every binding configured for an action in a context.
  std::vector<KeySequence> bindingsFor(KeyContext context, Action action) const;

  /// The binding shown in hints: the first one configured.
  std::string primaryBinding(KeyContext context, Action action) const;

  /// First non-empty binding across a fallback chain.
  std::string primaryBinding(const std::vector<KeyContext> &chain,
                             Action action) const;

  /// Short human-readable hint for an action, or empty when unbound.
  std::string hint(KeyContext context, Action action) const;

  /// Short human-readable hint for an action across a fallback chain, so a
  /// status line can show the key that actually fires in the active context.
  std::string hint(const std::vector<KeyContext> &chain, Action action) const;

  /// Same-context duplicates and same-context prefix ambiguities.
  std::vector<std::string> validate() const;

  /// Bindings for every action in a context, for a settings/help listing.
  std::vector<std::pair<Action, std::string>> listing(KeyContext context) const;

  // --- Editor inspection (read-only) ---------------------------------------

  /// Where a binding came from. Origin and effect are independent: a binding
  /// can be Custom *and* Shadowed at the same time, so one enum cannot carry
  /// both facts.
  enum class Origin { Default, Custom };
  /// Whether the binding is the one that actually wins in its own context.
  enum class Effect { Active, Shadowed };

  Origin originOf(KeyContext context, Action action) const;
  Effect effectOf(KeyContext context, Action action) const;

  /// True when this action's bindings in this context came from user
  /// configuration rather than the built-in defaults.
  bool isCustom(KeyContext context, Action action) const;

  /// Removes the user override for one action in one context and restores the
  /// built-in default bindings. Returns false when nothing was customised.
  bool resetBinding(KeyContext context, Action action);

  /// Contexts whose chain reaches this sequence at a *higher* priority than
  /// `context` does, resolving it to a different action. Shadowing is normal
  /// context behaviour, not an error.
  std::vector<std::pair<KeyContext, Action>> shadowedIn(
      KeyContext context, const KeySequence &sequence) const;

private:
  std::size_t revision_ = 0;
  void rebuildPrefixes(KeyContext context);
  const std::set<std::string> &prefixesFor(KeyContext context) const;

  // context -> sequence -> action (lookup)
  std::map<KeyContext, std::map<KeySequence, Action>> bindings_;
  // context -> sequences in configuration order, so hints show the binding the
  // user actually wrote first rather than whatever the map happens to sort to.
  std::map<KeyContext, std::vector<KeySequence>> order_;
  // context -> valid prefixes, derived from bindings_
  std::map<KeyContext, std::set<KeySequence>> prefixes_;

  // Pristine snapshot taken at the end of loadDefaults(), so a reset restores
  // the shipped binding rather than approximating it.
  std::map<KeyContext, std::map<KeySequence, Action>> defaults_;
  std::set<std::pair<KeyContext, Action>> custom_;

  KeySequence pending_;
  KeyContext pending_context_ = KeyContext::Global;
};

/// Renders a canonical token for display: "Ctrl+h" -> "Ctrl+h", "Esc" -> "Esc".
std::string displayKey(const std::string &token);

} // namespace termusic
