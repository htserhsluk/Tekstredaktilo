# Tekstredaktilo

A CLI-based collaborative text editor written entirely in C. This project demonstrates advanced OS principles by allowing multiple users to connect to a central server and edit a shared document simultaneously in real-time from their terminals.

At its core, the editor tackles the complex problem of concurrent text modification using a custom hybrid OT/CRDT (Operational Transformation / Conflict-free Replicated Data Type) engine. This ensures that even when multiple clients type at the exact same millisecond, the document state remains perfectly synchronized across all peers without data corruption.


## OS Concepts Demonstrated

| Concept | Mechanism used |
|---|---|
| Role-Based Authorization | Login system with admin / editor / guest roles |
| File Locking | `fcntl` read/write locks on document storage |
| Concurrency Control | `pthreads` per client, `mutex` + `semaphore` |
| Data Consistency | OT transforms + vector clocks prevent lost updates |
| Socket Programming | TCP client-server on port 9090 |
| Inter-Process Communication | Named pipe (FIFO) between server and logger process |

---

## Prerequisites

You need `gcc`, `make`, and the `ncurses` library installed.

On Ubuntu / Debian:
```bash
sudo apt update
sudo apt install gcc make libncurses5-dev libncursesw5-dev
```

---

## Build

Clone the repo and build all three binaries with one command:

```bash
git clone https://github.com/htserhsluk/Tekstredaktilo.git
cd Tekstredaktilo
make
```

This produces three executables in the project root: `server`, `client`, `logger`.

---

## Running the Project

You need **3 terminals** open inside the `Tekstredaktilo` folder.

### Terminal 1 — Start the logger first

The logger must be started before the server, otherwise startup messages are missed.

```bash
./logger
```

Expected output:
```
[logger] Waiting for server on FIFO /tmp/collab_editor_log ...
```

Leave this running.

### Terminal 2 — Start the server

```bash
./server
```

Expected output:
```
Server ready. Connect clients on port 9090
```

The logger terminal will also begin printing timestamped server events.

Leave this running.

### Terminal 3 — Connect a client

```bash
./client
```

To connect to a server on a different machine:
```bash
./client 192.168.x.x
```

---

## Logging In

When the client starts you will be prompted for credentials. Use one of the built-in accounts:

| Username | Password | Role | Permissions |
|---|---|---|---|
| `admin` | `admin123` | Admin | Edit + save document + kick users |
| `alice` | `alice123` | Editor | Insert and delete text |
| `bob` | `bob123` | Editor | Insert and delete text |
| `guest` | *(any)* | Guest | View only |

---

## Multi-Client Editing

Open a fourth terminal and connect as a second user:

```bash
./client
# log in as bob / bob123
```

Edits made by alice will appear in bob's terminal in real time, and vice versa. The OT engine automatically resolves any concurrent edits so both clients always end up with identical document state.

---
