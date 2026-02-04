# smash — Small Shell

A Unix-like command-line shell implemented in C++ demonstrating core operating systems concepts: process management, job control, signal handling, and inter-process communication.

## Overview

**smash** reads commands in an interactive loop, executes built-in commands in-process, and runs external commands via `fork` + `exec`. It implements background job control, I/O redirection, pipes, and signal handling — all standard Unix shell behaviors.

### What This Demonstrates

| Concept | Implementation |
|---------|----------------|
| **Process Management** | `fork()`, `execvp()`, `waitpid()` with `WUNTRACED`/`WNOHANG` |
| **Signal Handling** | Custom `SIGINT` handler routes Ctrl+C to foreground process |
| **Job Control** | Background jobs (`&`), job list tracking, `fg` command |
| **IPC** | Pipes via `pipe()` + `dup2()`, both stdout and stderr |
| **I/O Redirection** | File descriptor manipulation for `>` and `>>` |
| **OOP Design** | Singleton, Factory Method, Command Pattern |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  smash.cpp: main()                                              │
│  └─ Sets SIGINT handler, runs REPL loop                         │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│  SmallShell (Singleton)                                         │
│  ├─ executeCommand(): alias expansion, dispatch                 │
│  ├─ CreateCommand(): Factory method for Command objects         │
│  └─ JobsList m_joblist: background process tracking             │
└──────────────────────────┬──────────────────────────────────────┘
                           │
           ┌───────────────┼───────────────┐
           ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │BuiltInCmd  │  │ExternalCmd │  │SpecialCmd  │
    │(in-process)│  │(fork+exec) │  │(Pipe/Redir)│
    └────────────┘  └────────────┘  └────────────┘
```

### Key Code Locations

| What to Review | File | Key Function/Class |
|----------------|------|-------------------|
| Command dispatch & factory | `SmallShell.cpp` | `CreateCommand()`, `executeCommand()` |
| External command execution | `Commands.cpp:212` | `ExternalCommand::execute()` — fork/exec pattern |
| Pipe implementation | `Commands.cpp:423` | `PipeCommand::execute()` — dual fork + pipe |
| I/O redirection | `Commands.cpp:301` | `RedirectionCommand::execute()` — dup2 pattern |
| Job list & zombie cleanup | `JobList.cpp` | `removeFinishedJobs()` — waitpid with WNOHANG |
| Signal handling | `signals.cpp` | `ctrlCHandler()` — SIGKILL to foreground |

---

## Supported Commands

### Built-in Commands
*Created in `SmallShell::CreateCommand()` (SmallShell.cpp:96-114)*

| Command | Description |
|---------|-------------|
| `chprompt [name]` | Change shell prompt |
| `showpid` | Print shell PID |
| `pwd` | Print working directory |
| `cd <path>` | Change directory (`-` for previous) |
| `jobs` | List background jobs |
| `fg [job-id]` | Bring job to foreground |
| `kill -<sig> <job-id>` | Send signal to job |
| `quit [kill]` | Exit shell |
| `alias name='cmd'` | Create alias |
| `unalias <names>` | Remove aliases |
| `unsetenv <vars>` | Remove environment variables |
| `watchproc <pid>` | Monitor process CPU/memory |
| `du [path]` | Calculate disk usage |
| `whoami` | Show user and home directory |
| `netinfo <iface>` | Network interface info (bonus) |

### Special Syntax

| Syntax | Behavior |
|--------|----------|
| `command &` | Run in background |
| `cmd > file` | Redirect stdout (overwrite) |
| `cmd >> file` | Redirect stdout (append) |
| `cmd1 \| cmd2` | Pipe stdout |
| `cmd1 \|& cmd2` | Pipe stderr |

### External Commands
- Simple commands: executed via `execvp()` 
- Commands with `*` or `?`: executed via `/bin/bash -c "..."` for glob expansion

---

## Building

### Platform Requirements

| Platform | Compiler | Status |
|----------|----------|--------|
| **Linux** | System g++ | ✅ Works |
| **macOS** | Homebrew GCC-15 | ✅ Works |
| **macOS** | Apple Clang | ❌ Won't compile |
| **Windows** | — | Use WSL |


> ⚠️ **Platform Notes**:
> - **Linux**: Works with system g++
> - **macOS**: Requires Homebrew GCC (`brew install gcc`). Apple Clang will not work.
> - **Windows**: Not supported (use WSL)

---

Some features use Linux-specific APIs (`/proc` filesystem, network ioctls). On macOS, these commands will compile but may not function correctly at runtime: `watchproc`, `netinfo`, `unsetenv`.

### Build Instructions

**On Linux:**
```bash
make
```

**On macOS (requires Homebrew GCC):**
```bash
# Install GCC if you haven't
brew install gcc

# Build (Makefile auto-detects macOS and uses g++-15)
make
```

**Manual compilation (Linux):**
```bash
g++ -std=c++11 -D_XOPEN_SOURCE=500 -Wall -Wextra -pedantic \
    smash.cpp SmallShell.cpp Commands.cpp JobList.cpp signals.cpp \
    -o smash
```

**Manual compilation (macOS with Homebrew GCC):**
```bash
/opt/homebrew/bin/g++-15 -std=c++11 -D_XOPEN_SOURCE=500 -Wall -Wextra -pedantic \
    smash.cpp SmallShell.cpp Commands.cpp JobList.cpp signals.cpp \
    -o smash
```

### Run

```bash
./smash
```

---

## Example Session

```bash
smash> showpid
smash pid is 12345

smash> chprompt demo
demo> sleep 30 &
demo> sleep 60 &
demo> jobs
[1] sleep 30 &
[2] sleep 60 &

demo> fg 1
sleep 30 & 12346
^Csmash: got ctrl-C
smash: process 12346 was killed

demo> alias ll='ls -la'
demo> ll
total 48
drwxr-xr-x  2 user user  4096 Apr 18 10:00 .
...

demo> echo "test" > output.txt
demo> cat output.txt | head -1
test

demo> quit kill
smash: sending SIGKILL signal to 1 jobs:
12347: sleep 60 &
```

---

## Project Structure

```
smash/
├── smash.cpp           # Entry point, REPL loop, signal setup
├── SmallShell.cpp/h    # Shell singleton, command factory, job tracking
├── Commands.cpp/h      # Command hierarchy and implementations (~1200 lines)
├── JobList.cpp/h       # Background job management
├── signals.cpp/h       # SIGINT handler
├── Makefile            # Build configuration
├── .gitignore          # Build artifact exclusions
└── README.md           # This file
```

---

## Design Patterns

| Pattern | Location | Purpose |
|---------|----------|---------|
| **Singleton** | `SmallShell::getInstance()` | Single shell instance, global state access |
| **Factory Method** | `SmallShell::CreateCommand()` | Instantiate commands by parsed type |
| **Command** | `Command` class hierarchy | Encapsulate operations with uniform `execute()` interface |

---

## Technical Notes

- Child processes call `setpgrp()` after `fork()` to create new process groups — prevents terminal SIGINT from killing the shell along with children
- Zombie processes cleaned up via `waitpid(..., WNOHANG)` in `removeFinishedJobs()` before each command
- Job IDs assigned as `max(existing_ids) + 1`, tracked in a boolean array for O(1) lookup

---
