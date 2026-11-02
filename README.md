# HILShare template for C and C++

This repository holds a Tock app in C++ for a [HILShare](https://github.com/hilshare/action) board.
A library in C works the same way, through `board/main.cc`. Each push to `main` builds the app on a
hosted runner and runs it on a physical Nucleo-F411RE. The result is a check on your commit. You need
no board, no token and no secret.

## Use

For a new repository:

1. Select **Use this template** on GitHub. The first push starts a run.
2. Replace `text`, `crc32` and the check in `board/main.cc` with your own checks.
3. Add your own copyright line for your own code. `LICENSE` stays with this code.

For a repository that you already have:

1. Copy `board/` and `.github/workflows/board.yml` into your repository.
2. Add your sources and your include path to `board/Makefile`, above the `include` line. A path can
   start with `..`. `C_SRCS` holds C files, and `CXX_SRCS` holds C++ files.

   ```make
   C_SRCS := $(shell find ../src -name '*.c')
   CXX_SRCS := main.cc ../src/parser.cc
   override CPPFLAGS += -I../include
   ```

3. Add your checks to `board/main.cc`.
4. If your repository uses git submodules, add `submodules: recursive` to `actions/checkout` in
   `board.yml`.

This template does not run CMake. List the sources in `board/Makefile`, and copy the defines of your
CMake project to `CPPFLAGS`. The name `my-app` can stay. To change it, change `PACKAGE_NAME` in
`board/Makefile` and the `app` path in `board.yml`.

## Build

Install `arm-none-eabi-gcc`, `unzip` and elf2tab 0.13.0. Then, from `board/`:

```sh
./fetch-dependencies.sh
make
```

Use GCC 13, as on Ubuntu 24.04. libtock-c selects its precompiled newlib and libc++ from the major
version of the compiler, so another version can give other figures.
`./fetch-dependencies.sh` gets libtock-c at a pinned commit into `third-party/`. The first build
downloads newlib and libc++ for libtock-c, about 300 MB. The TAB is `board/build/my-app.tab`.

libtock-c builds a position-independent app, so the TBF holds no fixed addresses. `Makefile` sets
`KERNEL_MINOR_VERSION := 3`, the version that the Tock release-2.3.0 kernel reports. A TAB with another
value fails preflight, and the sentence names the correction.

## What a run can test

A run tests code that computes. The kernel gives an app a console, a cycle counter, the clock rate
and an alarm. The app runs unprivileged, in its own flash and RAM. It cannot use a peripheral, an
interrupt or a register of the core, and such an access faults. A driver, an RTOS or a HAL does not
run here.

| Limit | Value |
|---|---|
| Flash | 256 KB, with the TBF header |
| RAM | 64408 bytes for the stack, the heap and the static data, as `minimum_ram_size` in the TBF. Above this limit, the run ends at exit 2, and the sentence gives the limit |
| Stack | 16 KB, with `STACK_SIZE` in `board/Makefile` |
| Heap | 8 KB, with `APP_HEAP_SIZE` in `board/Makefile` |
| Time | 10 s, about 10⁹ cycles. A sleep counts |
| Console | A check takes about 2.5 ms, so one run holds about 3900 checks |
| Test data | Your data goes in flash, in a `const` array. A run has no input |

The C++ standard library has these costs in flash:

| Header | Flash |
|---|---|
| `<vector>`, `<string>` | About 36 KB |
| `<iostream>` | About 210 KB. It does not work here. Refer to the table of faults below |

Floating-point math runs in software, because Tock does not save the FPU state on a Cortex-M. The
results are correct, and the code takes more time than firmware that uses the FPU. `size_t` has 32
bits, so a value that you copy from a 64-bit host can differ.

## Write your checks

The tests of your library do not run on the board. Copy each test into one check:

| Call | The check passes if |
|---|---|
| `check(run, name, expected, task)` | The result equals `expected` |
| `check_close(run, name, expected, tolerance, task)` | `|result − expected| ≤ tolerance`, for `double` |
| `check_within_budget(run, name, expected, budget_microseconds, task)` | The result equals `expected`, and the time is not above the budget |

- `task` is a lambda that returns the result. It runs one time, and each call prints a line with the
  time and the stack.
- An answer can be a number or a `bool`. Another type needs an overload of `format_answer`.
- To test more than one value, return a `bool`:

  ```cpp
  check(run, "parse", true, [] { return parse("1") == 1 && parse("2") == 2; });
  ```

- To test many values in one line of the log, return a count of the failures.
- `keep` holds a value in the timed section, out of the reach of the optimizer.
- A name and a result cannot hold a space. Each one that does ends the run at exit 7.
- If an answer is incorrect, the line also holds `expected=`.
- To find an expected value, run the check with any value, then read `answer` from the log.

To test the SHA-256 of Mbed TLS:

1. Add one line to the end of `board/fetch-dependencies.sh`, with a pinned commit:

   ```sh
   # Mbed TLS v3.6.7.
   fetch mbedtls https://github.com/Mbed-TLS/mbedtls 068ff080b369adfac81509f9b57b2afabaf82dc5
   ```

2. Add these lines to `board/Makefile`, above the `include` line:

   ```make
   C_SRCS := third-party/mbedtls/library/sha256.c third-party/mbedtls/library/platform_util.c
   override CPPFLAGS += -I. -Ithird-party/mbedtls/include \
       -DMBEDTLS_CONFIG_FILE='"my_mbedtls_config.h"'
   ```

3. Write `board/my_mbedtls_config.h`. The name must differ from `mbedtls_config.h`, because Mbed TLS
   finds its own file of that name first.

   ```c
   #define MBEDTLS_SHA256_C
   ```

4. Add this line to `board/main.cc`, below the other `#include` line. The header of Mbed TLS has its
   own `extern "C"`.

   ```cpp
   #include <mbedtls/sha256.h>
   ```

5. Add one check to `main`, before `finish_run(run)`. The expected value is the first four bytes of
   the SHA-256 of `abc`.

   ```cpp
   check(run, "sha256_abc", uint32_t{0xBA7816BF}, [] {
     const unsigned char input[] = {'a', 'b', 'c'};
     keep(input);
     unsigned char digest[32];
     mbedtls_sha256(input, sizeof(input), digest, 0);
     keep(digest);
     return (uint32_t{digest[0]} << 24) | (uint32_t{digest[1]} << 16) | (uint32_t{digest[2]} << 8)
            | uint32_t{digest[3]};
   });
   ```

6. Push. The job log shows the check line.

## Build errors

| Text in the build log | Correction |
|---|---|
| `undefined reference` to a C function, from `main.cc` | Put the C header inside `extern "C" { }` |
| `undefined reference to _gettimeofday` | The board has no wall clock. `time()` and `std::chrono` do not link. Time a section with `check`, and give the library a fixed time |
| `undefined reference to __tls_get_addr` | The app has one thread, so `thread_local` has no use. Add `-Dthread_local=` to `CPPFLAGS` |
| `undefined reference to __atomic_fetch_add_8` | This core has no 64-bit atomics. The app has one thread, so a plain `uint64_t` is safe |
| `multiple definition of yield` | libtock-c defines `yield`, `command`, `subscribe`, `allow` and `memop`. Add `-Dyield=library_yield` to `CPPFLAGS` |
| `exception handling disabled` | The template uses `-fno-exceptions`. The library must build without exceptions |
| `cannot use 'typeid' with '-fno-rtti'` | The template uses `-fno-rtti`. The library must build without RTTI |
| `thread` is not a member of `std` | The app has one thread. `std::thread` does not exist here |

## Failures with no build error

| Failure | Reason and correction |
|---|---|
| A global object with a constructor stays zero | libtock-c does not run `.init_array`, so the linker removes each constructor. Use a function-local `static`, or call an init function from `main`. `std::cout` needs such a constructor, so use `printf` |
| A `.a` file from another build faults | An app reads its data through `r9`. Build the library from source, or with `-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative` |
| Assembly code faults | Your assembly must keep `r9`, and it must not use an absolute address |
| `fopen` fails | The board has no file system. Put your data in flash |

## Read a failure

| Exit | Meaning | What to do |
|---|---|---|
| 1 | The app reported `FAIL` | Read the check lines |
| 2 | The TAB fails preflight | The sentence names the correction |
| 5 | The kernel did not load the app, or the app did not start a run | The sentence gives the reason of the kernel. With no reason, look for a fault in code that runs before `main` |
| 6 | The run did not end | A failed `assert` prints its file and its line. After a hang, the last `running` line names the check |
| 7 | A line that HILShare cannot read | A name or a result holds a space |
| 8 | Three attempts gave no verdict | The app printed no `HILSHARE_START_EXECUTION` and did not exit. Look for a hang before the first check |
| 9 | The app faulted | Read `reason`, `addr` and `pc` on the fault line |

An `assert` ends the run, so the later checks do not run. The check lines above it stay in the log.

To find the source line of a fault, download the artifact `my-app-elf` from the workflow run, then run
this command with the value of `pc`. The app links at `0x80000000`, and it runs from `0x08040080`:

```sh
arm-none-eabi-addr2line -f -i -C -e cortex-m4.elf $(printf '0x%x' $((<pc> - 0x08040080 + 0x80000000)))
```

A stack overflow writes below the app RAM, so it gives exit 9. `reason` is `mpu_stacking` or
`mpu_data`. If the fault line has `addr`, its value is just below the start of the app RAM. That start
is `0x20004000` for a `minimum_ram_size` up to 31640 bytes, and `0x20008000` above that. The line has
no `pc`, because the stack holds no exception frame.

## Use your runs

An owner has 20 runs in a day, for all its repositories. Each push to `main` and each push to a pull
request uses one run. A new push to a pull request replaces its queued job, and a job that does not
start uses no run. To keep a commit from a run, add `[skip ci]` to its message. Do not use
`paths-ignore` with a required check, because GitHub leaves that check pending for a skipped workflow.

## The rules of a run

- A run has 10 s from the boot line to the exit line, and a sleep counts. The run ends after that
  time.
- `cycles` must not time a section that sleeps. The CPU sleeps in `WFI`, and the debug probe controls
  whether `CYCCNT` counts there. Time such a section with the ticks of the alarm driver, driver
  number `0x0`. Command 1 of that driver gives the tick rate.
- A probe reset halts the core for about 24 ms. The alarm counts through the halt, and `CYCCNT` does
  not. An app that compares the two must wait 100 ms after the reset.
- A timed section starts from a cold cache. To time it warm, run it one time before you time it.
- The kernel runs a timer at the end of each 100 ms timeslice, so a task above 100 ms includes kernel
  time. The `timeslices` field of the exit line gives the count.
- A pull request from a fork or from Dependabot gets no OIDC token. Its check is the build, and its
  board run comes after the merge. Refer to `.github/workflows/board.yml`.

The app prints the lines of the [HILShare contract](https://github.com/hilshare/action/blob/main/SPECS.md).
The app decides `PASS` or `FAIL`. HILShare copies that verdict to the exit code. The
[action](https://github.com/hilshare/action) gives the exit codes and the limits, and each run accepts
the [terms](https://github.com/hilshare/action/blob/main/TERMS.md).

## Output

A run on a Nucleo-F411RE with HILShare kernel `0.2.0`. `␞` is the byte `0x1E`. The kernel prints the
first line and the last line, and the app prints the other lines.

```
␞HILSHARE_KERNEL_BOOT version=0.2.0 clock_hz=100000000
HILSHARE_START_EXECUTION
HILSHARE_FIRMWARE=0.1.0
running crc32
HILSHARE_CHECK name=crc32 status=PASS answer=414fa339 cycles=2728 us=27.28 overhead_us=16.30 budget_us=31 stack_bytes=476
HILSHARE_CHECKS_PASSED=1/1
HILSHARE_RESULT=PASS
␞HILSHARE_KERNEL_APP_EXIT name=my-app code=0 flash=32768 ram=32768 stack=576 heap=0 grants=1196 syscalls=37 timeslices=0
```

## Budgets

A budget is optional. Each budget here is `ceil(1.1 × us)` of the figure on a Nucleo-F411RE with
HILShare kernel `0.2.0`. Measure each budget again after a change to the compiler, the kernel version,
the libtock-c commit or the flags in `board/Makefile`. A kernel change can turn a budget red with no
commit of yours, and the release notes of the
[action](https://github.com/hilshare/action/releases) give each kernel change.

In about one run in ten, `overhead_us` is up to 0.7 µs higher, and `us` is lower by the same amount.
Set a budget from the value that most runs give.

| Task | `us` | `budget_us` |
|---|---|---|
| `crc32` | 27.28 | 31 |

## Licence

Apache License 2.0. Refer to [LICENSE](LICENSE).
