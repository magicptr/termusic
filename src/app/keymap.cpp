#include "app/keymap.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cctype>
#include <sstream>

#include <ftxui/component/event.hpp>

namespace termusic {
namespace {

std::vector<std::string> splitSequence(const std::string &sequence) {
  std::vector<std::string> tokens;
  std::istringstream stream(sequence);
  std::string token;
  while (stream >> token)
    tokens.push_back(token);
  return tokens;
}

std::string joinTokens(const std::vector<std::string> &tokens) {
  std::string joined;
  for (const std::string &token : tokens) {
    if (!joined.empty())
      joined += ' ';
    joined += token;
  }
  return joined;
}

/// A configured token is accepted when it is a single printable character or a
/// known modifier/named form. Anything else is a configuration error rather
/// than something to guess at.
bool isKnownToken(const std::string &token) {
  if (token.empty())
    return false;
  static const std::set<std::string> named = {
      "Enter", "Esc",   "Tab",  "Space",     "Up",       "Down",
      "Left",  "Right", "Home", "End",       "Backspace", "Delete",
      "PageUp", "PageDown",
  };
  if (named.count(token) != 0)
    return true;
  const auto plus = token.find('+');
  if (plus != std::string::npos) {
    const std::string modifier = token.substr(0, plus);
    const std::string rest = token.substr(plus + 1);
    if (modifier != "Ctrl" && modifier != "Alt")
      return false;
    return isKnownToken(rest);
  }
  // A bare token must be exactly ONE printable codepoint. Accepting any
  // printable string let nonsense like "Ctrl+Banana" through as a key name.
  const unsigned char first = static_cast<unsigned char>(token[0]);
  if (first < 0x20)
    return false;
  if (first < 0x80)
    return token.size() == 1;
  const std::size_t expected = first >= 0xF0   ? 4
                               : first >= 0xE0 ? 3
                               : first >= 0xC0 ? 2
                                               : 0;
  if (expected == 0 || token.size() != expected)
    return false;
  for (std::size_t index = 1; index < token.size(); ++index) {
    if ((static_cast<unsigned char>(token[index]) & 0xC0) != 0x80)
      return false;
  }
  return true;
}

bool parseSequence(const std::string &sequence, std::vector<std::string> *out) {
  const std::vector<std::string> tokens = splitSequence(sequence);
  if (tokens.empty())
    return false;
  for (const std::string &token : tokens) {
    if (!isKnownToken(token))
      return false;
  }
  *out = tokens;
  return true;
}

} // namespace

std::string_view contextName(KeyContext context) {
  switch (context) {
  case KeyContext::Global:  return "global";
  case KeyContext::Library: return "library";
  case KeyContext::Tree:    return "tree";
  case KeyContext::Tracks:  return "tracks";
  case KeyContext::Visual:  return "visual";
  case KeyContext::Immersive: return "immersive";
  case KeyContext::Settings: return "settings";
  }
  return "global";
}

const std::vector<KeyContext> &chainFor(KeyContext context) {
  // Static chains: dispatch and the settings shadow report both read these.
  static const std::vector<KeyContext> global{KeyContext::Global};
  static const std::vector<KeyContext> library{KeyContext::Library,
                                               KeyContext::Global};
  static const std::vector<KeyContext> tree{KeyContext::Tree, KeyContext::Library,
                                            KeyContext::Global};
  static const std::vector<KeyContext> tracks{KeyContext::Tracks,
                                              KeyContext::Library,
                                              KeyContext::Global};
  // Visual deliberately does not inherit the normal panes.
  static const std::vector<KeyContext> visual{KeyContext::Visual,
                                              KeyContext::Global};
  static const std::vector<KeyContext> immersive{KeyContext::Immersive,
                                                 KeyContext::Global};
  static const std::vector<KeyContext> settings{KeyContext::Settings,
                                                KeyContext::Global};
  switch (context) {
  case KeyContext::Global:    return global;
  case KeyContext::Library:   return library;
  case KeyContext::Tree:      return tree;
  case KeyContext::Tracks:    return tracks;
  case KeyContext::Visual:    return visual;
  case KeyContext::Immersive: return immersive;
  case KeyContext::Settings:  return settings;
  }
  return global;
}

const std::vector<KeyContext> &allContexts() {
  static const std::vector<KeyContext> contexts = {
      KeyContext::Global,  KeyContext::Library, KeyContext::Tree,
      KeyContext::Tracks,  KeyContext::Visual,  KeyContext::Immersive,
      KeyContext::Settings};
  return contexts;
}

std::optional<KeyContext> contextFromName(std::string_view name) {
  if (name == "global")    return KeyContext::Global;
  if (name == "library")   return KeyContext::Library;
  if (name == "tree")      return KeyContext::Tree;
  if (name == "tracks")    return KeyContext::Tracks;
  if (name == "visual")    return KeyContext::Visual;
  if (name == "immersive") return KeyContext::Immersive;
  if (name == "settings")  return KeyContext::Settings;
  return std::nullopt;
}

bool actionConfigurableIn(Action action, KeyContext context) {
  // The mask's bits are context POSITIONS in allContexts(), so this loop is the
  // one place that translates between the two.
  const ContextSet allowed = actionConfigurableContexts(action);
  const std::vector<KeyContext> &contexts = allContexts();
  for (std::size_t index = 0; index < contexts.size(); ++index) {
    if (contexts[index] == context)
      return (allowed & (1u << index)) != 0u;
  }
  return false;
}

std::string normalizeKey(const ftxui::Event &event, bool text_entry) {
  if (event == ftxui::Event::Return) return "Enter";
  if (event == ftxui::Event::Escape) return "Esc";
  if (event == ftxui::Event::Tab)    return "Tab";
  if (event == ftxui::Event::TabReverse) return "Tab";
  if (event == ftxui::Event::ArrowUp)    return "Up";
  if (event == ftxui::Event::ArrowDown)  return "Down";
  if (event == ftxui::Event::ArrowLeft)  return "Left";
  if (event == ftxui::Event::ArrowRight) return "Right";
  if (event == ftxui::Event::Home)    return "Home";
  if (event == ftxui::Event::End)     return "End";
  if (event == ftxui::Event::Delete)  return "Delete";
  if (event == ftxui::Event::PageUp)   return "PageUp";
  if (event == ftxui::Event::PageDown) return "PageDown";
  if (event == ftxui::Event::CtrlD)   return "Ctrl+d";
  if (event == ftxui::Event::CtrlU)   return "Ctrl+u";
  if (event == ftxui::Event::CtrlW)   return "Ctrl+w";
  if (event == ftxui::Event::CtrlL)   return "Ctrl+l";
  if (event == ftxui::Event::Backspace) {
    // Byte-identical to Ctrl+h. Text entry keeps it; everywhere else it is the
    // configured Ctrl+h chord.
    return text_entry ? std::string() : std::string("Ctrl+h");
  }
  if (event.is_character()) {
    const std::string character = event.character();
    if (character == " ")
      return "Space";
    // Some terminals deliver the Enter key as CR or LF rather than as
    // Event::Return (tmux among them). Both mean Enter, and dropping them as
    // "control bytes" is what silently swallowed every Enter press there.
    if (character == "\r" || character == "\n")
      return "Enter";
    if (character.empty() ||
        static_cast<unsigned char>(character[0]) < static_cast<unsigned char>(0x20))
      return std::string();
    return character;
  }
  return std::string();
}

void Keymap::loadDefaults() {
  ++revision_;
  bindings_.clear();
  order_.clear();
  prefixes_.clear();
  const auto bind = [this](KeyContext context, Action action,
                           std::vector<std::string> sequences) {
    for (std::string &sequence : sequences) {
      // A default that is declared twice would advertise itself twice in the
      // editor and shadow the first declaration silently.
      const auto existing = bindings_[context].find(sequence);
      if (existing != bindings_[context].end()) {
        std::cerr << "duplicate default binding: " << sequence << std::endl;
        std::abort();
      }
      bindings_[context][sequence] = action;
      order_[context].push_back(sequence);
    }
  };

  // Round 50 canonical defaults: the shortcut set is deliberately small.
  // h/l are LOCAL horizontal movement, j/k are local vertical movement, and
  // immersive has its own playback meanings. Removed this round, and kept out
  // of the keymap so no config can bring them back: the configurable Quit
  // sequence, the Help keys, the global `[`/`]`, `+`/`-`, `R`, `u`, `H`, `L`,
  // the arrow aliases for Tree/Tracks movement, Tree's Ctrl+d/Ctrl+u, and the
  // Settings `r` reset.
  bind(KeyContext::Global, Action::Quit, {"q"});
  bind(KeyContext::Global, Action::Cancel, {"Esc"});
  bind(KeyContext::Global, Action::TogglePlay, {"Space"});
  bind(KeyContext::Global, Action::ToggleShuffle, {"s"});
  bind(KeyContext::Global, Action::PageLibrary, {"1"});
  bind(KeyContext::Global, Action::PageSettings, {"2"});
  bind(KeyContext::Global, Action::ToggleImmersive, {"i"});

  // Tree is Vim-style: j/k move, h/l change pane, PageDown/PageUp are the only
  // half-page keys. The arrow aliases are gone on purpose -- one spelling per
  // action is what keeps the table readable.
  bind(KeyContext::Tree, Action::MoveDown, {"j"});
  bind(KeyContext::Tree, Action::MoveUp, {"k"});
  bind(KeyContext::Tree, Action::MoveLeft, {"h"});
  bind(KeyContext::Tree, Action::MoveRight, {"l"});
  bind(KeyContext::Tree, Action::MoveToFirst, {"g g"});
  bind(KeyContext::Tree, Action::MoveToLast, {"G"});
  bind(KeyContext::Tree, Action::HalfPageDown, {"PageDown"});
  bind(KeyContext::Tree, Action::HalfPageUp, {"PageUp"});
  // Enter is the only Tree activation key: groups expand, collections open.
  bind(KeyContext::Tree, Action::Activate, {"Enter"});
  bind(KeyContext::Tree, Action::CreatePlaylist, {"a"});
  bind(KeyContext::Tree, Action::RenamePlaylist, {"r"});
  bind(KeyContext::Tree, Action::DeletePlaylist, {"d d"});
  bind(KeyContext::Tree, Action::PasteRegister, {"p"});
  // `/` searches the pane that owns the keyboard, so the tree is searchable
  // with the same key and the same box as the track list.
  bind(KeyContext::Tree, Action::OpenSearch, {"/"});

  // Tracks drop the arrow aliases for the same reason, but keep Ctrl+d/Ctrl+u:
  // unlike Tree, the list is long enough that both half-page spellings earn
  // their place.
  bind(KeyContext::Tracks, Action::MoveDown, {"j"});
  bind(KeyContext::Tracks, Action::MoveUp, {"k"});
  bind(KeyContext::Tracks, Action::MoveLeft, {"h"});
  bind(KeyContext::Tracks, Action::MoveToFirst, {"g g"});
  bind(KeyContext::Tracks, Action::MoveToLast, {"G"});
  bind(KeyContext::Tracks, Action::HalfPageDown, {"Ctrl+d", "PageDown"});
  bind(KeyContext::Tracks, Action::HalfPageUp, {"Ctrl+u", "PageUp"});
  bind(KeyContext::Tracks, Action::Activate, {"Enter"});
  bind(KeyContext::Tracks, Action::EnterVisual, {"v"});
  bind(KeyContext::Tracks, Action::YankCurrent, {"y y"});
  bind(KeyContext::Tracks, Action::DeleteCurrent, {"d d"});
  bind(KeyContext::Tracks, Action::CreatePlaylist, {"a"});
  bind(KeyContext::Tracks, Action::OpenSearch, {"/"});
  bind(KeyContext::Tracks, Action::NextMatch, {"n"});
  bind(KeyContext::Tracks, Action::PreviousMatch, {"N"});
  bind(KeyContext::Tracks, Action::MoveQueueItemUp, {"K"});
  bind(KeyContext::Tracks, Action::MoveQueueItemDown, {"J"});

  bind(KeyContext::Visual, Action::MoveDown, {"j"});
  bind(KeyContext::Visual, Action::MoveUp, {"k"});
  bind(KeyContext::Visual, Action::MoveToFirst, {"g g"});
  bind(KeyContext::Visual, Action::MoveToLast, {"G"});
  bind(KeyContext::Visual, Action::HalfPageDown, {"Ctrl+d", "PageDown"});
  bind(KeyContext::Visual, Action::HalfPageUp, {"Ctrl+u", "PageUp"});
  bind(KeyContext::Visual, Action::YankSelection, {"y"});
  bind(KeyContext::Visual, Action::DeleteSelection, {"d"});
  bind(KeyContext::Visual, Action::CancelVisual, {"Esc"});

  // Immersive reuses h/j/k/l for playback: no cursor or pane navigation
  // happens underneath while it is open.
  bind(KeyContext::Immersive, Action::CancelVisual, {"Esc"});
  bind(KeyContext::Immersive, Action::ToggleImmersive, {"i"});
  bind(KeyContext::Immersive, Action::VolumeDown, {"j"});
  bind(KeyContext::Immersive, Action::VolumeUp, {"k"});
  bind(KeyContext::Immersive, Action::Previous, {"h"});
  bind(KeyContext::Immersive, Action::Next, {"l"});
  bind(KeyContext::Immersive, Action::SeekBackward, {","});
  bind(KeyContext::Immersive, Action::SeekForward, {"."});
  bind(KeyContext::Immersive, Action::ToggleRepeat, {"r"});

  bind(KeyContext::Settings, Action::MoveDown, {"j"});
  bind(KeyContext::Settings, Action::MoveUp, {"k"});
  bind(KeyContext::Settings, Action::MoveLeft, {"h"});
  bind(KeyContext::Settings, Action::MoveRight, {"l"});
  bind(KeyContext::Settings, Action::Activate, {"Enter"});
  // No `r` reset: the fixed footer control is the recovery path, and the
  // per-row reset stays reachable through it.
  bind(KeyContext::Settings, Action::ResetAllBindings, {"R"});

  for (const auto &entry : bindings_)
    rebuildPrefixes(entry.first);
  // Snapshot the shipped state so resetBinding can restore it exactly.
  defaults_ = bindings_;
  custom_.clear();
  reset();
}

bool Keymap::applyOverride(KeyContext context, std::string_view action_name,
                           const std::vector<std::string> &sequences,
                           std::string *problem) {
  const auto fail = [&](std::string message) {
    if (problem != nullptr)
      *problem = std::move(message);
    return false;
  };
  const std::optional<Action> resolved = actionFromId(action_name);
  if (!resolved)
    return fail("unknown action");
  const Action action = *resolved;

  // Configurability is metadata, not a convention: an entry the application
  // has withdrawn (the reserved Quit, the removed Help/transport/section
  // shortcuts, Settings' `r`) is refused here, so an old configuration file
  // cannot resurrect a shortcut that no longer exists.
  if (!actionConfigurableIn(action, context)) {
    return fail(std::string(actionId(action)) + " is not configurable in [" +
                std::string(contextName(context)) + "]");
  }

  std::vector<KeySequence> parsed;
  parsed.reserve(sequences.size());
  for (const std::string &sequence : sequences) {
    std::vector<std::string> tokens;
    if (!parseSequence(sequence, &tokens))
      return fail("invalid key: \"" + sequence + "\"");
    // The reserved quit key is not a binding anyone can take: `q` leaves the
    // program from every state that is not text entry, so a binding containing
    // it could never fire and would only make the editor lie.
    for (const std::string &token : tokens) {
      if (token == kReservedQuitKey)
        return fail("\"" + sequence + "\" contains the reserved quit key \"" +
                    std::string(kReservedQuitKey) + "\"");
    }
    parsed.push_back(joinTokens(tokens));
  }

  std::map<KeySequence, Action> &table = bindings_[context];
  // Validate the whole override before touching anything.
  const auto strictPrefix = [](const KeySequence &prefix,
                               const KeySequence &whole) {
    return whole.size() > prefix.size() &&
           whole.compare(0, prefix.size(), prefix) == 0 &&
           whole[prefix.size()] == ' ';
  };
  for (const KeySequence &sequence : parsed) {
    for (const auto &[existing, owner] : table) {
      if (owner == action)
        continue; // replacing this action's own binding is the point
      if (existing == sequence)
        return fail("\"" + sequence + "\" is already bound to " +
                    std::string(actionId(owner)));
      if (strictPrefix(sequence, existing) || strictPrefix(existing, sequence))
        return fail("\"" + sequence + "\" is both an action and a sequence "
                    "prefix of " + std::string(actionId(owner)));
    }
    // Two sequences in the same override must not collide with each other.
    for (const KeySequence &other : parsed) {
      if (&other == &sequence)
        continue;
      if (other == sequence)
        return fail("\"" + sequence + "\" is bound twice in one override");
      if (strictPrefix(sequence, other) || strictPrefix(other, sequence))
        return fail("\"" + sequence + "\" is both an action and a sequence "
                    "prefix of \"" + other + "\"");
    }
  }

  // Accepted: the configured list is authoritative for this action, so the
  // previous bindings are dropped rather than merged.
  for (auto it = table.begin(); it != table.end();) {
    if (it->second == action)
      it = table.erase(it);
    else
      ++it;
  }
  std::vector<KeySequence> &ordered = order_[context];
  ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                               [&](const KeySequence &sequence) {
                                 return table.find(sequence) == table.end();
                               }),
                ordered.end());
  for (const KeySequence &joined : parsed) {
    table[joined] = action;
    ordered.push_back(joined);
  }
  rebuildPrefixes(context);
  custom_.insert({context, action});
  reset();
  ++revision_;
  return true;
}

void Keymap::rebuildPrefixes(KeyContext context) {
  std::set<std::string> &prefixes = prefixes_[context];
  prefixes.clear();
  const auto table = bindings_.find(context);
  if (table == bindings_.end())
    return;
  for (const auto &[sequence, action] : table->second) {
    (void)action;
    const std::vector<std::string> tokens = splitSequence(sequence);
    std::string prefix;
    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
      if (!prefix.empty())
        prefix += ' ';
      prefix += tokens[index];
      prefixes.insert(prefix);
    }
  }
}

bool Keymap::isCustom(KeyContext context, Action action) const {
  return custom_.count({context, action}) != 0;
}

Keymap::Origin Keymap::originOf(KeyContext context, Action action) const {
  return isCustom(context, action) ? Origin::Custom : Origin::Default;
}

Keymap::Effect Keymap::effectOf(KeyContext context, Action action) const {
  const std::vector<KeySequence> bindings = bindingsFor(context, action);
  if (bindings.empty())
    return Effect::Active;
  return shadowedIn(context, bindings.front()).empty() ? Effect::Active
                                                       : Effect::Shadowed;
}

bool Keymap::resetBinding(KeyContext context, Action action) {
  if (!isCustom(context, action))
    return false;
  std::map<KeySequence, Action> &table = bindings_[context];
  for (auto it = table.begin(); it != table.end();) {
    if (it->second == action)
      it = table.erase(it);
    else
      ++it;
  }
  std::vector<KeySequence> &ordered = order_[context];
  ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                               [&](const KeySequence &sequence) {
                                 return table.find(sequence) == table.end();
                               }),
                ordered.end());
  const auto defaults = defaults_.find(context);
  if (defaults != defaults_.end()) {
    for (const auto &[sequence, bound] : defaults->second) {
      if (bound != action)
        continue;
      table[sequence] = action;
      ordered.push_back(sequence);
    }
  }
  custom_.erase({context, action});
  rebuildPrefixes(context);
  reset();
  ++revision_;
  return true;
}

std::vector<std::pair<KeyContext, Action>> Keymap::shadowedIn(
    KeyContext context, const KeySequence &sequence) const {
  std::vector<std::pair<KeyContext, Action>> result;
  if (sequence.empty())
    return result;
  const auto mine = bindings_.find(context);
  const Action own = mine != bindings_.end() &&
                             mine->second.find(sequence) != mine->second.end()
                         ? mine->second.at(sequence)
                         : Action::None;
  // A binding is shadowed in context C when C's own lookup chain reaches
  // another context before it reaches `context`, and that nearer context
  // binds the same sequence to a different action.
  for (const KeyContext where : allContexts()) {
    const std::vector<KeyContext> &chain = chainFor(where);
    const auto position = std::find(chain.begin(), chain.end(), context);
    if (position == chain.end())
      continue;
    for (auto step = chain.begin(); step != position; ++step) {
      const auto table = bindings_.find(*step);
      if (table != bindings_.end()) {
        const auto found = table->second.find(sequence);
        if (found != table->second.end()) {
          if (found->second != own)
            result.emplace_back(where, found->second);
          break;
        }
        // The resolver gives a nearer prefix priority over a broader exact
        // binding. Reflect that in the editor's shadow report too.
        if (prefixesFor(*step).count(sequence) != 0) {
          const std::string prefix = sequence + ' ';
          const auto continuation = std::find_if(
              table->second.begin(), table->second.end(),
              [&](const auto &entry) { return entry.first.starts_with(prefix); });
          if (continuation != table->second.end())
            result.emplace_back(where, continuation->second);
          break;
        }
      }
    }
  }
  return result;
}

const std::set<std::string> &Keymap::prefixesFor(KeyContext context) const {
  static const std::set<std::string> empty;
  const auto found = prefixes_.find(context);
  return found == prefixes_.end() ? empty : found->second;
}

Keymap::Resolution Keymap::feed(KeyContext context, const std::string &token) {
  return feed(std::vector<KeyContext>{context, KeyContext::Global}, token);
}

Keymap::Resolution Keymap::feed(const std::vector<KeyContext> &chain,
                                const std::string &token) {
  if (token.empty() || chain.empty())
    return {Result::None, Action::None};

  // A partial sequence belongs to the interaction context where it started.
  // Mouse/page focus changes must not let a Tree prefix complete as a
  // Settings or Track-list command.
  if (!pending_.empty() && pending_context_ != chain.front())
    reset();

  KeySequence candidate = pending_;
  if (!candidate.empty())
    candidate += ' ';
  candidate += token;

  // Context priority applies to prefixes as well as completed bindings. A
  // nearer "g g" must get the chance to complete before a broader context's
  // single-key "g" binding fires.
  for (const KeyContext context : chain) {
    const auto table = bindings_.find(context);
    if (table != bindings_.end()) {
      const auto found = table->second.find(candidate);
      if (found != table->second.end()) {
        reset();
        return {Result::Matched, found->second};
      }
    }
    if (prefixesFor(context).count(candidate) != 0) {
      pending_ = candidate;
      pending_context_ = chain.front();
      return {Result::Pending, Action::None};
    }
  }

  // Not a continuation. Drop the pending sequence and reconsider the token on
  // its own, so a dead end never swallows the key.
  if (!pending_.empty()) {
    reset();
    if (candidate != token)
      return feed(chain, token);
  }
  return {Result::None, Action::None};
}

void Keymap::reset() {
  pending_.clear();
  pending_context_ = KeyContext::Global;
}

std::vector<KeySequence> Keymap::bindingsFor(KeyContext context,
                                             Action action) const {
  std::vector<KeySequence> result;
  const auto table = bindings_.find(context);
  if (table == bindings_.end())
    return result;
  const auto ordered = order_.find(context);
  if (ordered == order_.end())
    return result;
  for (const KeySequence &sequence : ordered->second) {
    const auto found = table->second.find(sequence);
    if (found != table->second.end() && found->second == action)
      result.push_back(sequence);
  }
  return result;
}

std::string Keymap::primaryBinding(KeyContext context, Action action) const {
  const std::vector<KeySequence> all = bindingsFor(context, action);
  return all.empty() ? std::string() : all.front();
}

std::string displayKey(const std::string &token) {
  if (token == "Up") return "\u2191";
  if (token == "Down") return "\u2193";
  if (token == "Left") return "\u2190";
  if (token == "Right") return "\u2192";
  if (token == "Space") return "Space";
  if (token == "Enter") return "Enter";
  if (token == "Esc") return "Esc";
  return token;
}

std::string Keymap::primaryBinding(const std::vector<KeyContext> &chain,
                                   Action action) const {
  for (const KeyContext context : chain) {
    const std::string binding = primaryBinding(context, action);
    if (!binding.empty())
      return binding;
  }
  return std::string();
}

std::string Keymap::hint(KeyContext context, Action action) const {
  const std::string binding = primaryBinding(context, action);
  if (binding.empty())
    return std::string();
  std::string rendered;
  for (const std::string &token : splitSequence(binding)) {
    if (!rendered.empty())
      rendered += ' ';
    rendered += displayKey(token);
  }
  return rendered;
}

std::string Keymap::hint(const std::vector<KeyContext> &chain,
                         Action action) const {
  const std::string binding = primaryBinding(chain, action);
  if (binding.empty())
    return std::string();
  std::string rendered;
  for (const std::string &token : splitSequence(binding)) {
    if (!rendered.empty())
      rendered += ' ';
    rendered += displayKey(token);
  }
  return rendered;
}

std::vector<std::string> Keymap::validate() const {
  std::vector<std::string> problems;
  for (const auto &[context, table] : bindings_) {
    // Same-context duplicate: two actions owning one exact sequence.
    std::map<KeySequence, Action> seen;
    for (const auto &[sequence, action] : table) {
      const auto found = seen.find(sequence);
      if (found != seen.end() && found->second != action) {
        problems.push_back("Binding conflict in [" +
                           std::string(contextName(context)) + "]: \"" +
                           sequence + "\" is bound to both " +
                           std::string(actionId(found->second)) + " and " +
                           std::string(actionId(action)));
      }
      seen[sequence] = action;
    }
    // Prefix ambiguity: an exact action that is also the start of a sequence.
    for (const auto &[sequence, action] : table) {
      if (table.find(sequence) != table.end() &&
          prefixesFor(context).count(sequence) != 0) {
        problems.push_back("Binding conflict in [" +
                           std::string(contextName(context)) + "]: \"" +
                           sequence + "\" is both an action and a sequence prefix");
      }
    }
  }
  return problems;
}

std::vector<std::pair<Action, std::string>> Keymap::listing(
    KeyContext context) const {
  std::vector<std::pair<Action, std::string>> rows;
  const auto table = bindings_.find(context);
  if (table == bindings_.end())
    return rows;
  std::set<Action> seen;
  for (const auto &[sequence, action] : table->second) {
    if (seen.insert(action).second)
      rows.emplace_back(action, sequence);
  }
  std::sort(rows.begin(), rows.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; });
  return rows;
}

} // namespace termusic
