#include "tvtime/schedule.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

namespace tvtime {

namespace {

// Slots are persisted one per line as tab-separated fields. Tabs and
// newlines in free-form fields (channel/videoId) are escaped so the format
// stays trivial to parse without a full JSON library.
std::string escapeField(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '\t':
        escaped += "\\t";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      default:
        escaped += ch;
        break;
    }
  }

  return escaped;
}

std::string unescapeField(const std::string& value) {
  std::string result;
  result.reserve(value.size());

  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      switch (value[i + 1]) {
        case 't':
          result += '\t';
          ++i;
          continue;
        case 'n':
          result += '\n';
          ++i;
          continue;
        case 'r':
          result += '\r';
          ++i;
          continue;
        case '\\':
          result += '\\';
          ++i;
          continue;
        default:
          break;
      }
    }

    result += value[i];
  }

  return result;
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;

  for (const char ch : line) {
    if (ch == '\t') {
      fields.push_back(current);
      current.clear();
    } else {
      current += ch;
    }
  }
  fields.push_back(current);

  return fields;
}

bool addSlotLocked(std::vector<ProgramSlot>& slots, ProgramSlot slot) {
  const auto overlaps = std::any_of(
      slots.begin(),
      slots.end(),
      [&slot](const ProgramSlot& existing) {
        return existing.channel == slot.channel &&
               slot.startMinute < existing.endMinute &&
               slot.endMinute > existing.startMinute;
      });

  if (overlaps) {
    return false;
  }

  slots.push_back(std::move(slot));
  std::sort(slots.begin(), slots.end(), [](const auto& left, const auto& right) {
    if (left.channel != right.channel) {
      return left.channel < right.channel;
    }

    return left.startMinute < right.startMinute;
  });

  return true;
}

}  // namespace

Schedule::AddResult Schedule::addSlot(ProgramSlot slot) {
  if (slot.startMinute < 0 || slot.endMinute <= slot.startMinute ||
      slot.endMinute > 24 * 60) {
    return AddResult::kInvalidRange;
  }

  std::unique_lock lock(mutex_);
  if (!addSlotLocked(slots_, std::move(slot))) {
    return AddResult::kOverlap;
  }

  return AddResult::kAdded;
}

std::vector<ProgramSlot> Schedule::slots() const {
  std::shared_lock lock(mutex_);
  return slots_;
}

std::vector<ProgramSlot> Schedule::slotsForChannel(
    const std::string& channel) const {
  std::shared_lock lock(mutex_);
  std::vector<ProgramSlot> channelSlots;

  std::copy_if(
      slots_.begin(),
      slots_.end(),
      std::back_inserter(channelSlots),
      [&channel](const ProgramSlot& slot) { return slot.channel == channel; });

  return channelSlots;
}

std::optional<ProgramSlot> Schedule::nowPlaying(
    const std::string& channel,
    int minuteOfDay) const {
  std::shared_lock lock(mutex_);
  const auto match = std::find_if(
      slots_.begin(),
      slots_.end(),
      [&channel, minuteOfDay](const ProgramSlot& slot) {
        return slot.channel == channel && minuteOfDay >= slot.startMinute &&
               minuteOfDay < slot.endMinute;
      });

  if (match == slots_.end()) {
    return std::nullopt;
  }

  return *match;
}

bool Schedule::loadFromFile(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error) || error) {
    return true;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  std::vector<ProgramSlot> loaded;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }

    const auto fields = splitTabs(line);
    if (fields.size() != 4) {
      // Skip malformed lines rather than aborting the whole load so a single
      // corrupt/truncated line doesn't lose every previously persisted slot.
      continue;
    }

    ProgramSlot slot;
    slot.channel = unescapeField(fields[0]);
    slot.videoId = unescapeField(fields[1]);
    try {
      slot.startMinute = std::stoi(fields[2]);
      slot.endMinute = std::stoi(fields[3]);
    } catch (const std::exception&) {
      continue;
    }

    if (!addSlotLocked(loaded, slot)) {
      // Corrupt/overlapping persisted data; skip the bad slot rather than
      // failing the whole load.
      continue;
    }
  }

  std::unique_lock lock(mutex_);
  slots_ = std::move(loaded);
  return true;
}

bool Schedule::saveToFile(const std::filesystem::path& path) const {
  std::ostringstream buffer;
  {
    std::shared_lock lock(mutex_);
    for (const auto& slot : slots_) {
      buffer << escapeField(slot.channel) << '\t' << escapeField(slot.videoId) << '\t'
             << slot.startMinute << '\t' << slot.endMinute << '\n';
    }
  }

  const auto parent = path.parent_path();
  std::error_code error;
  if (!parent.empty() && !std::filesystem::exists(parent, error)) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return false;
    }
  }

  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }

  file << buffer.str();
  return file.good();
}

}  // namespace tvtime
