#include "tvtime/schedule.h"

#include <algorithm>

namespace tvtime {

bool Schedule::addSlot(ProgramSlot slot) {
  if (slot.startMinute < 0 || slot.endMinute <= slot.startMinute ||
      slot.endMinute > 24 * 60) {
    return false;
  }

  const auto overlaps = std::any_of(
      slots_.begin(),
      slots_.end(),
      [&slot](const ProgramSlot& existing) {
        return existing.channel == slot.channel &&
               slot.startMinute < existing.endMinute &&
               slot.endMinute > existing.startMinute;
      });

  if (overlaps) {
    return false;
  }

  slots_.push_back(std::move(slot));
  std::sort(slots_.begin(), slots_.end(), [](const auto& left, const auto& right) {
    if (left.channel != right.channel) {
      return left.channel < right.channel;
    }

    return left.startMinute < right.startMinute;
  });

  return true;
}

std::vector<ProgramSlot> Schedule::slotsForChannel(
    const std::string& channel) const {
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

}  // namespace tvtime
