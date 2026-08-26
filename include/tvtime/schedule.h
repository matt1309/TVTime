#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tvtime {

struct ProgramSlot {
  std::string channel;
  std::string videoId;
  int startMinute = 0;
  int endMinute = 0;
};

class Schedule {
 public:
  bool addSlot(ProgramSlot slot);

  [[nodiscard]] std::vector<ProgramSlot> slotsForChannel(
      const std::string& channel) const;
  [[nodiscard]] std::optional<ProgramSlot> nowPlaying(
      const std::string& channel,
      int minuteOfDay) const;

 private:
  std::vector<ProgramSlot> slots_;
};

}  // namespace tvtime
