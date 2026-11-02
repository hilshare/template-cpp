// The timer, the stack paint and the check lines of a run. Your checks go in main.cc. Include
// this file from main.cc only.

#ifndef RUN_H
#define RUN_H

#include <libtock/tock.h>
#include <stdint.h>
#include <stdio.h>

#include <type_traits>

namespace {

// capsules_extra::cycle_count::DRIVER_NUM, and its commands.
constexpr uint32_t cycle_count_driver = 0x90008;
constexpr uint32_t cycle_count_start = 1;
constexpr uint32_t cycle_count_read = 2;
constexpr uint32_t cycle_count_reset = 3;
// The clock driver of the HILShare kernel. Command 1 returns the CPU clock in hertz.
constexpr uint32_t clock_rate_driver = 0xA0000;
constexpr uint32_t clock_rate_hertz = 1;

// crt0 puts the stack top at tock_app_memory_begins_at() + stack_size.
constexpr uintptr_t stack_size = HILSHARE_STACK_SIZE;
// Written over the free stack before a task. The deepest word that differs after the task
// is its peak.
constexpr uint32_t stack_pattern = 0xA5A5A5A5;
// Kept free below the frame of paint_stack, so the fill does not write a live frame.
constexpr uintptr_t stack_paint_margin = 256;
// The longest answer of a check line, with its terminator.
constexpr size_t answer_size = 65;
// The budget of a check with no limit on its time.
constexpr uint32_t no_budget = UINT32_MAX;

// The cycles and the peak stack of one task.
struct Measurement {
  uint32_t cycles;
  uintptr_t stack_bytes;
};

// The cycle counter, the CPU clock, and the round trip of one counter read.
struct Timer {
  uint32_t clock_hz;
  uint32_t overhead_cycles;
};

// One run: the timer and the tally of the checks.
struct Run {
  Timer timer;
  int passed;
  int total;
};

// Keeps `value`, and each memory write before this point, inside the timed section.
template <typename Value>
inline void keep(Value& value) {
  asm volatile("" : : "r"(&value) : "memory");
}

// CYCCNT is 32 bits. Unsigned subtraction holds across one wrap.
uint32_t read_cycles() {
  uint64_t count = 0;
  tock_command_return_u64_to_returncode(command(cycle_count_driver, cycle_count_read, 0, 0), &count);
  return static_cast<uint32_t>(count);
}

// Starts the cycle counter and measures the round trip. Returns the check name of the
// driver that is not available, or nullptr.
const char* start_timer(Timer& timer) {
  if (tock_command_return_u32_to_returncode(command(clock_rate_driver, clock_rate_hertz, 0, 0),
                                            &timer.clock_hz) != RETURNCODE_SUCCESS) {
    return "clock_rate";
  }
  [[maybe_unused]] syscall_return_t reset = command(cycle_count_driver, cycle_count_reset, 0, 0);
  if (tock_command_return_novalue_to_returncode(command(cycle_count_driver, cycle_count_start, 0, 0))
      != RETURNCODE_SUCCESS) {
    return "cycle_counter";
  }
  uint32_t before = read_cycles();
  uint32_t after = read_cycles();
  timer.overhead_cycles = after - before;
  return nullptr;
}

// Fills stack_pattern from the stack limit to stack_paint_margin below this frame, and
// returns the end of the fill.
__attribute__((noinline)) uintptr_t paint_stack() {
  volatile uint8_t frame_marker = 0;
  uintptr_t frame = reinterpret_cast<uintptr_t>(&frame_marker);
  uintptr_t start = reinterpret_cast<uintptr_t>(tock_app_memory_begins_at());
  uintptr_t end = (frame - stack_paint_margin) & ~uintptr_t{3};
  for (uintptr_t address = start; address < end; address += 4) {
    *reinterpret_cast<volatile uint32_t*>(address) = stack_pattern;
  }
  return end;
}

// From the stack top to the deepest word that does not hold stack_pattern. A task that
// stayed above the fill gives the depth of the fill end.
uintptr_t peak_stack_bytes(uintptr_t painted_end) {
  uintptr_t start = reinterpret_cast<uintptr_t>(tock_app_memory_begins_at());
  uintptr_t address = start;
  while (address < painted_end && *reinterpret_cast<volatile uint32_t*>(address) == stack_pattern) {
    address += 4;
  }
  return start + stack_size - address;
}

// Runs `task` one time. A task passes its inputs and its result through keep(), so its
// work stays between the two reads.
template <typename Task>
Measurement measure(const Timer& timer, Task task) {
  uintptr_t painted_end = paint_stack();
  uint32_t before = read_cycles();
  task();
  uint32_t after = read_cycles();
  uintptr_t stack_bytes = peak_stack_bytes(painted_end);
  uint32_t elapsed = after - before;
  uint32_t cycles = elapsed > timer.overhead_cycles ? elapsed - timer.overhead_cycles : 0;
  return Measurement{cycles, stack_bytes};
}

// cycles × 100 000 000 / clock_hz.
uint64_t hundredths_of_a_microsecond(const Timer& timer, uint32_t cycles) {
  return static_cast<uint64_t>(cycles) * 100000000u / timer.clock_hz;
}

// The text of an answer: hexadecimal for a uint32_t, and decimal for another number. Add an
// overload for each other type of answer. The text must hold no space.
template <typename Answer>
void format_answer(char (&printed)[answer_size], Answer answer) {
  if constexpr (std::is_same_v<Answer, bool>) {
    snprintf(printed, sizeof(printed), "%s", answer ? "true" : "false");
  } else if constexpr (std::is_same_v<Answer, uint32_t>) {
    snprintf(printed, sizeof(printed), "%08lx", static_cast<unsigned long>(answer));
  } else if constexpr (std::is_floating_point_v<Answer>) {
    snprintf(printed, sizeof(printed), "%f", static_cast<double>(answer));
  } else {
    snprintf(printed, sizeof(printed), "%ld", static_cast<long>(answer));
  }
}

// Prints the name first, so that the log names the check after a hang or an assert.
template <typename Answer, typename Task>
Answer run_task(Run& run, const char* name, Task task, Measurement& measurement) {
  printf("running %s\n", name);
  Answer answer{};
  measurement = measure(run.timer, [&] {
    answer = task();
    keep(answer);
  });
  return answer;
}

// Prints the check line. expected= is on the line only if the answer is incorrect.
template <typename Answer>
void report(Run& run, const char* name, bool answer_is_correct, Answer answer, Answer expected,
            const Measurement& measurement, uint32_t budget_microseconds) {
  char printed[answer_size];
  format_answer(printed, answer);
  uint64_t time = hundredths_of_a_microsecond(run.timer, measurement.cycles);
  uint64_t overhead = hundredths_of_a_microsecond(run.timer, run.timer.overhead_cycles);
  bool passed = answer_is_correct
      && (budget_microseconds == no_budget || time <= static_cast<uint64_t>(budget_microseconds) * 100);
  printf("HILSHARE_CHECK name=%s status=%s answer=%s", name, passed ? "PASS" : "FAIL", printed);
  if (!answer_is_correct) {
    format_answer(printed, expected);
    printf(" expected=%s", printed);
  }
  printf(" cycles=%lu us=%lu.%02lu overhead_us=%lu.%02lu", static_cast<unsigned long>(measurement.cycles),
         static_cast<unsigned long>(time / 100), static_cast<unsigned long>(time % 100),
         static_cast<unsigned long>(overhead / 100), static_cast<unsigned long>(overhead % 100));
  if (budget_microseconds != no_budget) {
    printf(" budget_us=%lu", static_cast<unsigned long>(budget_microseconds));
  }
  printf(" stack_bytes=%lu\n", static_cast<unsigned long>(measurement.stack_bytes));
  run.total++;
  run.passed += passed ? 1 : 0;
}

// Passes if the result of task equals expected and the time is not above budget_microseconds.
// name must hold no space.
template <typename Answer, typename Task>
void check_within_budget(Run& run, const char* name, Answer expected, uint32_t budget_microseconds,
                         Task task) {
  Measurement measurement{};
  Answer answer = run_task<Answer>(run, name, task, measurement);
  report(run, name, answer == expected, answer, expected, measurement, budget_microseconds);
}

// Passes if the result of task equals expected. name must hold no space.
template <typename Answer, typename Task>
void check(Run& run, const char* name, Answer expected, Task task) {
  check_within_budget(run, name, expected, no_budget, task);
}

// Passes if |result − expected| ≤ tolerance. A result that is not a number fails.
template <typename Task>
void check_close(Run& run, const char* name, double expected, double tolerance, Task task) {
  Measurement measurement{};
  double answer = run_task<double>(run, name, task, measurement);
  double difference = answer - expected;
  bool close = difference <= tolerance && -difference <= tolerance;
  report(run, name, close, answer, expected, measurement, no_budget);
}

// Prints the start markers and starts the timer. Returns false after it printed the failed
// check and the result.
bool start_run(Run& run) {
  // _isatty is 1 for fd 0 only, so newlib buffers stdout fully. A line buffer gives one console
  // write for each line. With no buffer, each part of a printf is a console write.
  static char line[256];
  setvbuf(stdout, line, _IOLBF, sizeof(line));
  printf("HILSHARE_START_EXECUTION\n");
  printf("HILSHARE_FIRMWARE=0.1.0\n");
  if (const char* missing = start_timer(run.timer)) {
    printf("HILSHARE_CHECK name=%s status=FAIL\n", missing);
    printf("HILSHARE_RESULT=FAIL\n");
    return false;
  }
  return true;
}

// Prints the tally and the result.
void finish_run(const Run& run) {
  printf("HILSHARE_CHECKS_PASSED=%d/%d\n", run.passed, run.total);
  printf("HILSHARE_RESULT=%s\n", run.passed == run.total ? "PASS" : "FAIL");
}

}  // namespace

#endif  // RUN_H
