# F1 Telemetry Replay Engine 🏎️
### A Real-Time, Multi-Process Simulation of the 2021 Abu Dhabi Grand Prix

> **EGC 301P — Operating Systems Lab Mini Project**  
> Built in C & Python · Demonstrates core OS concepts through motorsport simulation

---

## Overview

The **F1 Telemetry Replay Engine** is a high-performance, multithreaded simulation of the 2021 Formula 1 Abu Dhabi Grand Prix. It fetches real historical race data from F1's servers and replays it in real-time across a custom-built client-server architecture — entirely in your terminal.

19 independent car client processes stream live speed and throttle telemetry over TCP sockets to a central Race Control server, which ingests all data simultaneously, maintains a live leaderboard, and enforces role-based access for Admin, Engineer, and Guest users.

---

## Features

- **Real F1 Data** — Python pipeline fetches genuine speed & throttle telemetry for all 19 drivers from the 2021 Abu Dhabi Grand Prix using the `fastf1` library
- **Concurrent Car Clients** — 19 independent C processes simulate cars streaming telemetry over TCP sockets with a physics-based lap counter
- **Multithreaded Race Control Server** — POSIX threads ingest all 19 data streams simultaneously without blocking
- **Live Leaderboard** — A forked analytics process prints a sorted leaderboard on every new lap, for all 58 laps of the race
- **Role-Based Access Control** — Admin, Engineer, and Guest roles with distinct command permissions
- **Shared Memory + Mutex Synchronization** — Race state stored in `mmap` shared memory, protected by `pthread_mutex` locks to prevent race conditions
- **Race Archive Logger** — A background thread logs all car telemetry to `race_archive.dat` with POSIX file locking (`fcntl`)
- **Failsafe Auto-Shutdown** — A watchdog timer thread shuts the server down after 3 minutes if the race doesn't complete

---

## OS Concepts Demonstrated

| Concept | Implementation |
|---|---|
| **Role-Based Authorization** | Admin / Engineer / Guest roles at server startup; commands are gated by role |
| **File Locking** | `fcntl` advisory write locks on `race_archive.dat` in the logger thread |
| **Concurrency Control** | `pthreads` for client handler, logger, admin console, and watchdog threads |
| **Synchronization** | `pthread_mutex` on shared memory; named POSIX semaphore (`sem_open`) for max-car slots |
| **Data Consistency** | All reads/writes to shared grid protected by mutex to prevent dirty reads and race conditions |
| **Socket Programming** | TCP client-server model; 19 car clients connect to the server over `SOCK_STREAM` |
| **IPC — Shared Memory** | `mmap(MAP_SHARED | MAP_ANONYMOUS)` shares race state between parent server and analytics child |
| **IPC — Signals** | `fork()` creates analytics subprocess; `kill(SIGTERM)` used for coordinated shutdown |

---

## 📁 Project Structure

```
f1_telemetry_engine/
├── src/
│   ├── server.c          # Race Control — multithreaded TCP server
│   └── car_client.c      # Car Simulator — TCP client with physics engine
├── data/                 # Generated CSV telemetry files (one per driver)
├── get_telemetry.py      # Python data pipeline (fetches real F1 data)
├── CMakeLists.txt        # CMake build configuration
├── race_archive.dat      # Auto-generated race log (created at runtime)
└── README.md
```

---

## Prerequisites

**System:** Linux (required — uses POSIX APIs: `mmap`, `pthread`, `sem_open`, `fcntl`, `fork`)

**Tools:**
- GCC / Clang with C11 support
- CMake ≥ 3.20
- Python 3.8+
- `fastf1` Python library

> **macOS note:** Remove `rt` from `target_link_libraries` in `CMakeLists.txt` (already handled). Named semaphores behave slightly differently on macOS.

---

## Setup & Usage

### Step 1 — Fetch the Telemetry Data

Install the Python dependency and run the data pipeline:

```bash
pip install fastf1
python3 get_telemetry.py
```

This downloads real speed & throttle data for all 19 drivers from the 2021 Abu Dhabi GP and saves them as CSV files in the `data/` directory.

---

### Step 2 — Build the C Programs

```bash
mkdir build && cd build
cmake ..
make
```

This produces two executables: `pitwall_server` and `car_client`.

---

### Step 3 — Start the Race Control Server

```bash
./pitwall_server
```

You'll be prompted to select a role:

```
Welcome to the Abu Dhabi 2021 Pit Wall
1. Admin (Race Director)
2. Engineer (Pit Wall)
3. Guest (Spectator)
Enter Role [1-3]:
```

---

### Step 4 — Launch the Car Clients

Open a new terminal and run one client per driver. The car ID must be between 1–19, the name should match the driver abbreviation, and the path points to their CSV file:

```bash
./car_client 1 VER data/VER.csv
./car_client 2 HAM data/HAM.csv
./car_client 3 BOT data/BOT.csv
# ... and so on for all 19 drivers
```

> **Tip:** Use the included launch script to start all 19 drivers at once. Save the following as `launch_cars.sh` in the **project root** and run `bash launch_cars.sh`:
> 
```bash
names=("Verstappen" "Hamilton" "Sainz" "Tsunoda" "Gasly" "Bottas" "Norris" "Alonso" "Ocon" "Leclerc" "Vettel" "Ricciardo" "Stroll" "Schumacher" "Perez" "Latifi" "Giovinazzi" "Russell" "Raikkonen")
files=("33.csv" "44.csv" "55.csv" "22.csv" "10.csv" "77.csv" "4.csv" "14.csv" "31.csv" "16.csv" "5.csv" "3.csv" "18.csv" "47.csv" "11.csv" "6.csv" "99.csv" "63.csv" "7.csv")

for i in {1..19}; do
    idx=$((i-1))
    name=${names[$idx]}
    filepath="./data/${files[$idx]}"
    # Pointing to the build folder where the executable lives
    ./build/car_client $i "$name" "$filepath" &
done
```

---

## Admin Console Commands

Once the server is running, type commands into the server terminal:

| Command | Permission | Description |
|---|---|---|
| `green` | Admin only | Start telemetry ingestion (green flag) |
| `red` | Admin only | Pause telemetry ingestion (red flag) |
| `shut` | Admin only | Manually shut down the server |
| `status` | Admin, Engineer | Show server/track status and your role |
| `fastest` | Admin, Engineer | Show the current fastest car on track |
| `watch` | All roles | Print a live snapshot of all active cars |

---

## Sample Output

```
--- [LIVE LEADERBOARD] - LAP 14/58 ---
P1: Car 1 (VER) - 312.4 km/h
P2: Car 44 (HAM) - 308.1 km/h
P3: Car 77 (BOT) - 301.7 km/h
...
--------------------------------------

[CHECKERED FLAG] VERSTAPPEN WINS THE WORLD CHAMPIONSHIP! AUTO-SHUTTING DOWN SERVER...
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                   pitwall_server                    │
│                                                     │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────┐  │
│  │ Logger      │  │ Admin        │  │ Watchdog  │  │
│  │ Thread      │  │ Console      │  │ Timer     │  │
│  │ (fcntl lock)│  │ Thread       │  │ Thread    │  │
│  └─────────────┘  └──────────────┘  └───────────┘  │
│                                                     │
│  ┌─────────────────────────────────────────────┐    │
│  │     Shared Memory (mmap) — Race Grid        │    │
│  │         Protected by pthread_mutex          │    │
│  └─────────────────────────────────────────────┘    │
│                          │ fork()                   │
│              ┌───────────▼──────────┐               │
│              │  Analytics Process  │               │
│              │  (Live Leaderboard) │               │
│              └─────────────────────┘               │
└───────────────────────┬─────────────────────────────┘
                        │ TCP (port 8080)
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
  ┌──────────┐    ┌──────────┐    ┌──────────┐
  │car_client│    │car_client│    │car_client│
  │  (VER)   │    │  (HAM)   │    │  (BOT)   │  ...×19
  └──────────┘    └──────────┘    └──────────┘
```

---

## License

This project was built as part of the **EGC 301P Operating Systems Lab** course. It is intended for educational purposes.

---

*Engineered for the 2021 Abu Dhabi Grand Prix — where champions are made.*
