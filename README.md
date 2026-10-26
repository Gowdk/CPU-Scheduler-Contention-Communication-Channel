# CPU Scheduler Covert Timing Channel

**A Linux proof of concept that transmits messages between two user-space processes through CPU scheduler contention—without using a conventional IPC channel to carry the message.**

`C` · `Linux` · `Operating Systems` · `Systems Security` · `Covert Channels` · `Signal Processing`

**[Technical write-up and interactive explanation](https://gowdark.pages.dev/projects/cpu-scheduler-covert-channel/)**

## Overview

Can two processes communicate when they do not exchange data through sockets, pipes, files, or shared memory?

This project explores that question by using a shared **logical CPU** as an indirect communication medium. The producer encodes bits by either consuming CPU time or sleeping. A separate consumer, pinned to the same CPU, measures how much work it can complete in fixed time windows and uses the resulting contention pattern to reconstruct a message.

The challenge is not simply making the CPU busy: it is **recovering the correct message despite scheduling jitter, background activity, and the absence of direct synchronization**.

This is a controlled, same-host demonstration of a *covert timing channel*, not a demonstrated privilege escalation, sandbox escape, or undetectable exfiltration technique.

## How it works

```text
   PRODUCER (producer.c)                  CONSUMER (consumer.c)
   Message -> bytes -> bits                Work completed per time window
               |                                       ^
       1: perform CPU work                    Less work -> infer 1
       0: sleep until deadline                More work -> infer 0
               |                                       |
               +---------- SHARED LOGICAL CPU --------+
                               |
                       Scheduler contention

          No socket, pipe, or shared-memory payload exchange
```

Both processes use `sched_setaffinity()` to run on the same logical CPU. Each bit occupies a fixed-duration time slot:

- **Bit `1` — busy:** the producer runs a CPU-intensive workload, reducing the consumer's available processor time.
- **Bit `0` — idle:** the producer sleeps, leaving more CPU time available to the consumer.
- **Receiver:** the consumer counts batches of computational work during each sampling window. Lower counts indicate contention; higher counts indicate less contention.

### Frame format

```text
+-------------+------------------+------------------+--------+-------------+-------+
| Preamble    | Busy calibration | Idle calibration | Length | Payload     | CRC-8 |
| 32 bits     | 8 bits           | 8 bits           | 8 bits | 8 x N bits  | 8 bits|
+-------------+------------------+------------------+--------+-------------+-------+
```

The length field describes a payload of up to **255 bytes**. A six-slot idle interval separates repeated frames. The CRC-8 covers the length byte and payload, allowing the receiver to reject frames corrupted by measurement errors.

### Reliability techniques

| Challenge | Implementation |
| --- | --- |
| Competing on the same resource | Linux CPU affinity pins both processes to the same logical CPU. |
| Timing drift | Monotonic-clock deadlines anchor transmission to absolute slot boundaries. |
| Jitter near bit transitions | Each slot is divided into subslots; the receiver discards the first and last and takes the median of the remainder. |
| Finding a frame without a handshake | A known 32-bit preamble is detected with normalized cross-correlation. |
| CPU performance varying between runs | Eight busy and eight idle calibration bits establish a per-frame decoding threshold. |
| Accepting noisy or incomplete frames | Signal-separation checks, length-aware decoding, CRC-8 validation, and receiver state reset on failed candidates. |

The receiver manages frame detection, calibration, length decoding, payload reconstruction, and CRC verification through a `FrameState` structure.

## Build and run

### Requirements

- Linux with support for `sched_setaffinity()` and POSIX monotonic clocks
- A C compiler (GCC or Clang) and `make`
- Two terminals on the **same machine**, with permission to use the selected logical CPU

```bash
git clone https://github.com/Gowdk/CPU-Scheduler-Contention-Communication-Channel.git
cd CPU-Scheduler-Contention-Communication-Channel/"Finished Channel"
make
```

**Terminal 1 — start the consumer:**

```bash
./consumer -c 2 -s 100
```

**Terminal 2 — transmit a message:**

```bash
./producer -c 2 -s 100 -r 3 "Hello"
```

Start the consumer first so it is already listening. Both processes must use the **same** `-c` CPU index and `-s` slot duration. CPU `2` is the program default, but it may not be available in every environment; select a CPU permitted by your system's affinity mask (for example, inspect `taskset -pc $$`).

Upon successful reception, the consumer prints the decoded message along with the detected correlation, calibration statistics, failed-candidate count, end-to-end latency, and a post-lock payload goodput estimate.

### Command-line options

| Program | Option | Meaning | Default |
| --- | --- | --- | --- |
| Both | `-c` | Logical CPU index | `2` |
| Both | `-s` | Bit duration in milliseconds | `100` |
| Producer | `-r` | Number of times to send the frame | `5` |
| Consumer | `-n` | Receiver niceness (0–19) | `10` |
| Consumer | `-b` | Number of subslots per bit | `8` |

The current consumer requires a slot duration of at least **100 ms**. Use the default subslot count unless you are specifically experimenting with the sampling implementation.

To clean build artifacts:

```bash
make clean
```

## Performance and limitations

At the default **100 ms per bit**, the nominal signaling rate is **10 bits/second** before framing overhead. Each frame contains **64 fixed bits** in addition to its payload, plus an inter-frame idle gap, so useful-message throughput is lower. The consumer reports latency and a payload goodput estimate for a successfully decoded frame.

The experiment deliberately makes several tradeoffs:

- **Controlled environment:** both processes are on the same Linux host and explicitly pinned to one logical CPU. Cross-core or cross-host communication is not demonstrated.
- **Timing sensitivity:** competing workloads, scheduling policy, affinity restrictions, and system load can affect decoding reliability. Successful reception is not guaranteed.
- **Low bandwidth:** longer slots help separate busy and idle states but make transmission slower.
- **Error detection, not security:** CRC-8 detects many accidental corruptions; it is not cryptographic authentication or confidentiality.
- **Cooperative setup:** the two programs agree in advance on the CPU, slot duration, preamble, and framing rules. This project does not establish that a real-world security boundary has been bypassed.

## Code organization

```text
Finished Channel/
├── Makefile       # Builds the two executables
├── producer.c     # CPU-contention signaling and frame transmission
├── producer.h
├── consumer.c     # Sampling, synchronization, decoding, and metrics
├── consumer.h     # Receiver state and declarations
├── shared.c       # CPU affinity, monotonic clock, and CRC utilities
└── shared.h       # Shared protocol constants and interfaces
```

## Why I built it

I built this project after studying operating-system scheduling and implementing a [multilevel feedback queue scheduler](/https://github.com/Gowdk/MLFQSimulation). I wanted to investigate an operating-system security question through **low-level C programming and experimentation**: can ordinary resource contention reveal information that the processes never directly exchange?

The most interesting engineering problems were measuring contention accurately, synchronizing without IPC, distinguishing an intentional signal from noise, and maintaining enough state to reject invalid frames without losing track of the protocol.

This work connects **operating-system internals, side-channel security, statistical signal detection, and protocol design**. It also motivates defensive questions about resource isolation, scheduler observability, and how to measure the practical capacity of unintended communication channels.

## Further work

- Publish a reproducible benchmark of message success rates, false-positive rates, and payload throughput across slot sizes and background workloads.
- Explore whether CPU affinity and priority constraints can be relaxed without losing reliable synchronization.
- Investigate detection or mitigation using scheduler-level tracing and resource-usage analysis.
- Add automated validation for framing, CRC handling, command-line arguments, and noisy/failed transmissions.

---

**Detailed design notes:** [CPU Scheduler Covert Timing Channel — project write-up](https://gowdark.pages.dev/projects/cpu-scheduler-covert-channel/)

**Scope:** Educational systems-security research in a controlled environment.
