// CRC-32 of a fixed text, timed one time on the board. Replace text, crc32 and the check with
// your own checks. README.md gives the steps for a library.

#include "run.h"

namespace {

const char text[] = "The quick brown fox jumps over the lazy dog";
constexpr uint32_t crc32_expected = 0x414FA339;
constexpr uint32_t crc32_budget_microseconds = 31;

// The reflected CRC-32 of IEEE 802.3: polynomial 0xEDB88320, one bit at a time.
uint32_t crc32(const char* data, size_t length) {
  uint32_t remainder = 0xFFFFFFFF;
  for (size_t index = 0; index < length; index++) {
    remainder ^= static_cast<uint8_t>(data[index]);
    for (int bit = 0; bit < 8; bit++) {
      uint32_t mask = -(remainder & 1);
      remainder = (remainder >> 1) ^ (0xEDB88320 & mask);
    }
  }
  return ~remainder;
}

}  // namespace

int main() {
  Run run{};
  if (!start_run(run)) {
    return 0;
  }
  check_within_budget(run, "crc32", crc32_expected, crc32_budget_microseconds, [] {
    keep(text);
    return crc32(text, sizeof(text) - 1);
  });
  finish_run(run);
  return 0;
}
