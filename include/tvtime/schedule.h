#pragma once

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace tvtime {

struct ProgramSlot {
  std::string channel;
  std::string videoId;
  int startMinute = 0;
  int endMinute = 0;
};

// Thread-safe, optionally-persisted store of scheduled program slots. This
// backs the `/api/schedule` endpoints so schedules built through the API
// survive a server restart instead of only living in browser localStorage.
class Schedule {
 public:
  enum class AddResult {
    kAdded,
    kInvalidRange,
    kOverlap,
  };

  AddResult addSlot(ProgramSlot slot);

  [[nodiscard]] std::vector<ProgramSlot> slots() const;
  [[nodiscard]] std::vector<ProgramSlot> slotsForChannel(
      const std::string& channel) const;
  [[nodiscard]] std::optional<ProgramSlot> nowPlaying(
      const std::string& channel,
      int minuteOfDay) const;

  // Loads previously persisted slots from `path`, replacing any in-memory
  // slots. Returns true if the file did not exist (nothing to load) or was
  // loaded successfully; returns false if the file exists but could not be
  // parsed.
  bool loadFromFile(const std::filesystem::path& path);

  // Persists the current slots to `path`. Returns false on write failure.
  [[nodiscard]] bool saveToFile(const std::filesystem::path& path) const;

 private:
  mutable std::shared_mutex mutex_;
  std::vector<ProgramSlot> slots_;
};

}  // namespace tvtime
