#include "ui/core/settings.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "util/text.hpp"

namespace termusic::ui::core {

using namespace ftxui;

namespace {

/// The label column, in cells. One width for every module, so the panes read as
/// one form rather than as five hand-built screens.
constexpr int kLabelWidth = 20;

/// How far a Select's candidate rows are indented under their parent. A curtain
/// that opens downward, not a popup: the rows are ordinary list rows and push
/// everything below them down.
constexpr int kCandidateIndent = 4;

/// Formats a Number item: `scale` 10 turns 15 into "1.5".
std::string formatNumber(const SettingItem &item, int value) {
  if (item.scale <= 1)
    return std::to_string(value) + item.unit;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.*f%s",
                item.scale == 10 ? 1 : 2,
                static_cast<double>(value) / static_cast<double>(item.scale),
                item.unit.c_str());
  return std::string(buffer);
}

int clampToRange(const SettingItem &item, int value) {
  return std::clamp(value, item.minimum, item.maximum);
}

/// Word wrap, honouring explicit newlines. Text items are prose (Help,
/// descriptions), so they wrap; a VALUE is never wrapped -- a truncated host
/// name is better than one silently split across two rows.
std::vector<std::string> wrapText(const std::string &text, int width) {
  std::vector<std::string> lines;
  if (width <= 8) {
    lines.push_back(text);
    return lines;
  }
  std::string line;
  std::size_t index = 0;
  while (index <= text.size()) {
    const std::size_t space = text.find(' ', index);
    const std::string word =
        text.substr(index, space == std::string::npos ? std::string::npos
                                                      : space - index);
    const int candidate =
        static_cast<int>(line.size() + (line.empty() ? 0U : 1U) + word.size());
    if (!line.empty() && candidate > width) {
      lines.push_back(line);
      line.clear();
    }
    if (!line.empty())
      line += ' ';
    line += word;
    if (space == std::string::npos)
      break;
    index = space + 1;
  }
  if (!line.empty())
    lines.push_back(line);
  if (lines.empty())
    lines.push_back(std::string());
  return lines;
}

} // namespace

bool SettingItem::selectable() const {
  return kind != SettingKind::Heading && kind != SettingKind::Disabled;
}

// --- Builders ---------------------------------------------------------------

SettingItem heading(std::string text) {
  SettingItem item;
  item.kind = SettingKind::Heading;
  item.label = std::move(text);
  return item;
}

SettingItem note(std::string text) {
  SettingItem item;
  item.kind = SettingKind::Text;
  item.label = std::move(text);
  return item;
}

SettingItem note(std::function<std::string()> text) {
  SettingItem item;
  item.kind = SettingKind::Text;
  item.get_text = std::move(text);
  return item;
}

SettingItem toggle(std::string label, std::function<bool()> get,
                   std::function<void(bool)> set, std::string help) {
  SettingItem item;
  item.kind = SettingKind::Toggle;
  item.label = std::move(label);
  item.help = std::move(help);
  item.get_bool = std::move(get);
  item.set_bool = std::move(set);
  return item;
}

SettingItem input(std::string label, std::function<std::string()> get,
                  std::function<void(const std::string &)> set,
                  std::string help, bool secret) {
  SettingItem item;
  item.kind = SettingKind::Input;
  item.label = std::move(label);
  item.help = std::move(help);
  item.get_text = std::move(get);
  item.set_text = std::move(set);
  item.secret = secret;
  return item;
}

SettingItem number(std::string label, std::function<int()> get,
                   std::function<void(int)> set, int minimum, int maximum,
                   int step, std::string unit, int scale, std::string help) {
  SettingItem item;
  item.kind = SettingKind::Number;
  item.label = std::move(label);
  item.help = std::move(help);
  item.get_int = std::move(get);
  item.set_int = std::move(set);
  item.minimum = minimum;
  item.maximum = maximum;
  item.step = std::max(1, step);
  item.unit = std::move(unit);
  item.scale = std::max(1, scale);
  return item;
}

SettingItem select(std::string label, std::vector<std::string> options,
                   std::function<int()> get, std::function<void(int)> set,
                   std::string help) {
  SettingItem item;
  item.kind = SettingKind::Select;
  item.label = std::move(label);
  item.help = std::move(help);
  item.options = std::move(options);
  item.get_index = std::move(get);
  item.set_index = std::move(set);
  return item;
}

SettingItem action(std::string label, std::function<void()> run,
                   std::string help) {
  SettingItem item;
  item.kind = SettingKind::Action;
  item.label = std::move(label);
  item.help = std::move(help);
  item.run = std::move(run);
  return item;
}

SettingItem disabled(std::string label, std::string help) {
  SettingItem item;
  item.kind = SettingKind::Disabled;
  item.label = std::move(label);
  item.help = std::move(help);
  return item;
}

// --- The list engine --------------------------------------------------------

void SettingList::set(std::vector<SettingItem> items) {
  items_ = std::move(items);
  editing_ = -1;
  buffer_.clear();
  open_select_ = -1;
  candidate_ = 0;
  selected_ = nextSelectable(-1, 1);
  scroll_ = 0;
}

int SettingList::nextSelectable(int from, int delta) const {
  const int count = static_cast<int>(items_.size());
  if (count == 0)
    return 0;
  int index = from;
  for (int step = 0; step < count; ++step) {
    index += delta;
    if (index < 0) {
      if (!wrap_navigation_)
        return std::clamp(from, 0, count - 1);
      index = count - 1;
    }
    if (index >= count) {
      if (!wrap_navigation_)
        return std::clamp(from, 0, count - 1);
      index = 0;
    }
    if (items_[static_cast<std::size_t>(index)].selectable())
      return index;
  }
  return std::clamp(from, 0, count - 1);
}

bool SettingList::leaveEditing() {
  if (open_select_ >= 0) {
    // Esc on an open curtain: close it and change nothing. The row goes back to
    // showing the ACTIVE value, because that is the only value there is.
    open_select_ = -1;
    candidate_ = 0;
    return true;
  }
  if (editing_ < 0)
    return false;
  editing_ = -1;
  buffer_.clear();
  return true;
}

std::string SettingList::selectedLabel() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(items_.size()))
    return {};
  return items_[static_cast<std::size_t>(selected_)].label;
}

void SettingList::moveCursor(int delta) {
  if (delta == 0)
    return;
  // While a curtain is open, j/k belong to the CANDIDATES: the settings cursor
  // cannot move, so the outer list is frozen until the curtain closes.
  if (open_select_ >= 0 && open_select_ < static_cast<int>(items_.size())) {
    const SettingItem &item = items_[static_cast<std::size_t>(open_select_)];
    const int count = static_cast<int>(item.options.size());
    if (count > 0)
      candidate_ = std::clamp(candidate_ + (delta > 0 ? 1 : -1), 0, count - 1);
    return;
  }
  // Moving off a field commits nothing: the same rule as leaving it with Esc.
  if (editing_ >= 0)
    leaveEditing();
  selected_ = nextSelectable(selected_, delta > 0 ? 1 : -1);
  scrollToCursor();
}

bool SettingList::activate() {
  if (selected_ < 0 || selected_ >= static_cast<int>(items_.size()))
    return false;
  const int index = selected_;
  const SettingItem &item = items_[static_cast<std::size_t>(index)];
  switch (item.kind) {
  case SettingKind::Heading:
    return false;
  case SettingKind::Disabled:
    // Offered, not available: there is nothing Enter could change, and the
    // cursor cannot even reach the row.
    return false;
  case SettingKind::Text:
    // Read-only, but selectable so a long block can be walked line by line.
    return true;
  case SettingKind::Toggle:
    if (item.get_bool && item.set_bool) {
      item.set_bool(!item.get_bool());
      return true;
    }
    return false;
  case SettingKind::Input:
    // Inputs are replacement editors. The lightweight settings field has no
    // caret or selection model, so retaining the old host made typing a remote
    // IP append it to "127.0.0.1". Start empty and let Enter commit the new
    // value; Esc still cancels without calling the setter.
    editing_ = index;
    buffer_.clear();
    return true;
  case SettingKind::Number:
    // A number opens EMPTY: it is replaced, not appended to. Committing an
    // empty field keeps the current value.
    editing_ = index;
    buffer_.clear();
    return true;
  case SettingKind::Select: {
    if (item.options.empty() || !item.get_index || !item.set_index)
      return false;
    if (open_select_ != index) {
      // Collapsed + Enter: drop the curtain, with the cursor already on the
      // value that is ACTUALLY active -- never on the first option.
      open_select_ = index;
      candidate_ = std::clamp(item.get_index(), 0,
                              static_cast<int>(item.options.size()) - 1);
      return true;
    }
    // Open + Enter: confirm. The setter is the only writer, so this is where
    // the value reaches the configuration; then the curtain folds away and the
    // rows below come back up.
    const int chosen = std::clamp(
        candidate_, 0, static_cast<int>(item.options.size()) - 1);
    open_select_ = -1;
    candidate_ = 0;
    item.set_index(chosen);
    return true;
  }
  case SettingKind::Action:
    if (item.run) {
      item.run();
      return true;
    }
    return false;
  }
  return false;
}

bool SettingList::inputKey(const Event &event) {
  if (editing_ < 0 || editing_ >= static_cast<int>(items_.size()))
    return false;
  const SettingItem &item = items_[static_cast<std::size_t>(editing_)];
  if (event == Event::Escape) {
    leaveEditing();
    return true;
  }
  if (event == Event::Return) {
    commitEdit();
    return true;
  }
  if (event == Event::Backspace) {
    if (!buffer_.empty())
      buffer_.pop_back();
    return true;
  }
  if (item.kind == SettingKind::Number) {
    // Digits, a sign and one separator only: a port is not prose.
    if (event.is_character()) {
      const std::string typed = event.character();
      if (!typed.empty() && typed.size() == 1U &&
          (std::isdigit(static_cast<unsigned char>(typed[0])) != 0 ||
           (typed == "-" && buffer_.empty()))) {
        buffer_ += typed;
      }
      return true;
    }
    return true;
  }
  if (event.is_character()) {
    // One event carries one whole codepoint, which may be several bytes.
    const std::string typed = event.character();
    if (!typed.empty() &&
        static_cast<int>(static_cast<unsigned char>(typed[0])) >= 0x20)
      buffer_ += typed;
    return true;
  }
  return true; // the field owns every other key while it is being typed into
}

void SettingList::commitEdit() {
  if (editing_ < 0 || editing_ >= static_cast<int>(items_.size())) {
    leaveEditing();
    return;
  }
  const SettingItem item = items_[static_cast<std::size_t>(editing_)];
  if (item.kind == SettingKind::Input) {
    if (item.set_text)
      item.set_text(buffer_);
  } else if (item.kind == SettingKind::Number && item.set_int) {
    // An empty or unparseable field keeps the current value: a half-typed
    // number is not a reset to zero.
    if (!buffer_.empty()) {
      try {
        item.set_int(clampToRange(item, std::stoi(buffer_) * item.scale));
      } catch (const std::exception &) {
      }
    }
  }
  editing_ = -1;
  buffer_.clear();
}

std::string SettingList::displayValue(const SettingItem &item,
                                      int index) const {
  if (editing_ == index)
    return buffer_;
  switch (item.kind) {
  case SettingKind::Toggle: {
    const bool value = item.get_bool ? item.get_bool() : false;
    return value ? "[x] on" : "[ ] off";
  }
  case SettingKind::Input: {
    const std::string value = item.get_text ? item.get_text() : std::string();
    if (!item.secret)
      return value;
    return std::string(value.size(), '*');
  }
  case SettingKind::Number:
    return formatNumber(item, item.get_int ? item.get_int() : 0);
  case SettingKind::Select: {
    if (item.options.empty())
      return std::string();
    int index_value = item.get_index ? item.get_index() : 0;
    index_value =
        std::clamp(index_value, 0, static_cast<int>(item.options.size()) - 1);
    return item.options[static_cast<std::size_t>(index_value)];
  }
  case SettingKind::Text:
    return item.get_text ? item.get_text() : item.label;
  case SettingKind::Disabled:
    // The one value a reserved row has: it is not available yet.
    return "[ ] unavailable";
  case SettingKind::Action:
  case SettingKind::Heading:
    return std::string();
  }
  return std::string();
}

void SettingList::scrollToCursor() {
  // One screen row per item, plus one for its help line: the cursor's own
  // footprint is what has to stay visible.
  int row = 0;
  for (int index = 0; index < selected_ && index < static_cast<int>(items_.size());
       ++index) {
    row += items_[static_cast<std::size_t>(index)].help.empty() ? 1 : 2;
  }
  if (row < scroll_)
    scroll_ = row;
}

Element SettingList::render(const Theme &theme, bool focused, int rows,
                            int width) const {
  // Flatten first: scrolling works on rendered lines, so a wrapped description
  // takes as many of them as it needs.
  struct Line {
    int item;
    enum class Part { Label, Help, Candidate } part;
    /// Candidate rows carry which option they are; -1 for every other part.
    int option = -1;
    std::string text; ///< the rendered line (already wrapped)
  };
  std::vector<Line> lines;
  for (std::size_t index = 0; index < items_.size(); ++index) {
    const SettingItem &item = items_[index];
    if (item.kind == SettingKind::Text) {
      for (std::string &piece : wrapText(displayValue(item, static_cast<int>(index)),
                                         width - 2)) {
        lines.push_back({static_cast<int>(index), Line::Part::Label, -1,
                         std::move(piece)});
      }
      continue;
    }
    lines.push_back({static_cast<int>(index), Line::Part::Label, -1,
                     std::string()});
    // An open curtain drops its candidates straight into the list, one row
    // each, so everything below is physically pushed down: a curtain, not an
    // overlay, and no reserved height when it is closed.
    if (open_select_ == static_cast<int>(index)) {
      for (std::size_t option = 0; option < item.options.size(); ++option) {
        lines.push_back({static_cast<int>(index), Line::Part::Candidate,
                         static_cast<int>(option), item.options[option]});
      }
    }
    if (!item.help.empty()) {
      // The help line is indented under the label column.
      for (std::string &piece : wrapText(item.help, width - kLabelWidth - 2)) {
        lines.push_back({static_cast<int>(index), Line::Part::Help, -1,
                         std::move(piece)});
      }
    }
  }
  // Two lines matter: the selected ITEM's first row (so the settings cursor is
  // never lost) and, while a curtain is open, the CANDIDATE under the cursor.
  int cursor_line = 0;
  int focus_line = 0;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const Line &line = lines[index];
    if (line.item != selected_)
      continue;
    if (line.part == Line::Part::Candidate) {
      if (line.option == candidate_)
        focus_line = static_cast<int>(index);
      continue;
    }
    if (line.part == Line::Part::Label) {
      cursor_line = static_cast<int>(index);
      if (open_select_ < 0)
        focus_line = cursor_line;
    }
  }
  // Keep both on screen: recomputing here (rather than in a mutating method)
  // keeps render() const and idempotent, and the viewport stays the ONE the
  // pane already has -- no second nested scrollbar.
  int first = std::min(scroll_, cursor_line);
  if (focus_line >= first + rows)
    first = focus_line - rows + 1;
  if (focus_line < first)
    first = focus_line;
  first = std::clamp(first, 0, std::max(0, static_cast<int>(lines.size()) - 1));

  Elements out;
  for (int index = first; index < static_cast<int>(lines.size()) &&
                          index < first + rows;
       ++index) {
    const Line &line = lines[static_cast<std::size_t>(index)];
    const SettingItem &item = items_[static_cast<std::size_t>(line.item)];
    const bool is_cursor =
        line.part == Line::Part::Candidate
            ? (open_select_ == line.item && line.option == candidate_)
            : (line.item == selected_ && line.part == Line::Part::Label &&
               !curtainOpen());
    switch (line.part) {
    case Line::Part::Candidate: {
      // Indentation is the ONLY structure: no ball, no bracket, no tick. The
      // value that is actually active is a little heavier than the rest, which
      // is enough to tell it apart while browsing.
      const bool active =
          item.get_index && line.option == item.get_index();
      Element row = hbox({
          text(std::string(kCandidateIndent, ' ') + line.text) |
              (active ? bold : nothing) |
              color(active ? theme.text : theme.muted_text),
          filler(),
      });
      if (is_cursor && focused)
        row = row | color(theme.selected_fg) | bgcolor(theme.selected_bg);
      else if (is_cursor)
        row = row | bold;
      out.push_back(std::move(row));
      break;
    }
    case Line::Part::Help:
      out.push_back(text(std::string(kLabelWidth, ' ') + line.text) |
                    color(theme.weak_text));
      break;
    case Line::Part::Label:
      if (item.kind == SettingKind::Heading) {
        out.push_back(hbox({
            text(" " + item.label) | bold | color(theme.accent_primary),
            filler(),
        }));
        break;
      }
      if (item.kind == SettingKind::Text) {
        out.push_back(text(line.text) | color(theme.text));
        break;
      }
      if (item.kind == SettingKind::Disabled) {
        // A reserved row keeps the geometry of the control it will become --
        // the label column and a value -- and is drawn in the weakest text
        // role: present, visibly not offered.
        out.push_back(hbox({
            text(util::padRight(item.label, kLabelWidth)) |
                color(theme.weak_text),
            text(displayValue(item, line.item)) | color(theme.weak_text),
            filler(),
        }));
        break;
      }
      {
        // One row geometry for every control, with the KIND carried by the
        // value's shape: `[x] on` is a switch, `< 6600 >` a stepper, a label in
        // the accent colour an action.
        const std::string value =
            item.kind == SettingKind::Action
                ? "[ " + item.label + " ]"
                : displayValue(item, line.item);
        Element row = hbox({
            text(util::padRight(item.kind == SettingKind::Action ? std::string()
                                                                : item.label,
                                kLabelWidth)) |
                color(theme.muted_text),
            text(value) |
                color(item.kind == SettingKind::Action ? theme.accent_secondary
                                                       : theme.text) |
                bold,
            filler(),
        });
        if (is_cursor && focused)
          row = row | color(theme.selected_fg) | bgcolor(theme.selected_bg);
        else if (is_cursor)
          row = row | bold;
        out.push_back(std::move(row));
      }
      break;
    }
  }
  if (out.empty())
    out.push_back(text(""));
  return vbox(std::move(out)) | flex;
}

// --- The reusable section ---------------------------------------------------

void SettingsListSection::build(const CoreContext &context) {
  context_ = &context;
  theme_ = context.theme();
  fill(context);
  filled_ = true;
}

Component SettingsListSection::component() {
  // A leaf renderer: the Application routes the pane's keys into the list
  // (moveCursor / activate / adjust / inputKey), so there is no widget tree to
  // keep in step with the items.
  return Renderer([this] {
    return context_ != nullptr
               ? render(*context_)
               : text("");
  });
}

Element SettingsListSection::render(const CoreContext &context) {
  poll(context);
  const int rows = std::max(3, context.metrics.main_height - 4);
  const int width = std::max(20, context.metrics.track_buffer_width - 4);
  return corePane(
      std::string(title()),
      list_.render(context.theme(), context.content_focused(), rows, width),
      context.theme());
}

void SettingsListSection::poll(const CoreContext &context) {
  theme_ = context.theme();
  context_ = &context;
  if (!filled_ || itemsChanged(context)) {
    fill(context);
    filled_ = true;
  }
}

} // namespace termusic::ui::core
