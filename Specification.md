# CMon Bot — Requirements Specification

> A Discord bot that provides multi-node CI/CD orchestration by bridging Discord users to CMon agents via a distributed job queue, with SSH-based dynamic service discovery.

---

## Overview

CMon Bot is the Discord-facing control plane over one or more CMon server agents. Three distinct layers work together:

| Layer | Role |
|-------|------|
| **CMon agent** | HTTP server on each node. Executes commands, knows nothing about Discord |
| **Job Queue** | Distributed async queue (Celery-like, shell-command-capable, SSH-aware). Routes work to the right node without the bot caring about transport |
| **CMon Bot** | Discord interface. Knows about services, users, and intent. Submits jobs and reports results |

The job queue replaces direct bot→CMon HTTP calls for all command execution. It also handles multi-node fan-out natively, removing that complexity from the bot.

---

## Problem Statement

The previous design had the bot calling CMon agents directly over HTTP. This creates:
- **Blocking Discord interactions** — bot waits on command completion before responding
- **Manual multi-node logic** — bot code must fan out, gather, and reconcile results
- **Tight coupling** — bot must know each node's address and key at call time

Adding a job queue and SSH-based discovery solves all three:
- Jobs are submitted and acknowledged immediately; results posted when ready
- Multi-node fan-out is the queue's concern, not the bot's
- Nodes register themselves; the bot discovers services dynamically

---

## Architecture

```
Discord
  │  slash commands
  ▼
┌──────────────────────────────────────────────────────┐
│                    CMon Bot                           │
│                                                       │
│  Command Router → Permission Check → Job Submitter   │
│                                    ↕                  │
│  Result Listener ← Audit Log ← Job Queue Client      │
└────────────────────────┬─────────────────────────────┘
                         │  enqueue / subscribe
                         ▼
┌──────────────────────────────────────────────────────┐
│              Distributed Job Queue                    │
│         (Celery-like, SSH-transport, shell commands)  │
│                                                       │
│  ┌─────────────┐   ┌──────────────┐                  │
│  │  Job Broker │   │  Workers     │                   │
│  │  (queue +   │   │  (per-node   │                   │
│  │   results)  │   │   SSH exec)  │                   │
│  └─────────────┘   └──────────────┘                  │
└────────────────────────┬─────────────────────────────┘
                         │  SSH
          ┌──────────────┴──────────────┐
          ▼                             ▼
   ┌─────────────┐               ┌─────────────┐
   │  CMon       │               │  CMon       │
   │  Node A     │               │  Node B     │
   └─────────────┘               └─────────────┘
```

---

## Core Concepts

### Node
A registered server running a CMon agent. Nodes are not statically configured — they are discovered via an SSH handshake (see §F1).

### Service
A logical deployment unit (e.g. `api`, `frontend`, `worker`). Services are declared by each node during discovery. The bot resolves service names to nodes through the registry built from discovery responses.

### Command Manifest
A file in each service's repository (e.g. `cmon.toml`) declaring available commands:

```toml
[commands]
sync    = "nix run .#sync"
build   = "nix run .#build"
test    = "nix run .#test"
migrate = "nix run .#migrate"
```

When a user runs `/sync api`, the bot resolves the service, reads its manifest command, and submits that exact shell command to the job queue targeting the correct node. CMon itself remains generic — it runs whatever command arrives.

### Job
A unit of work submitted to the queue. Contains: target node, shell command, working directory, requesting user, job ID. The bot submits a job and immediately acknowledges the Discord interaction. Results are delivered asynchronously when the job completes.

### Environment
A named group of nodes/services (e.g. `production`, `staging`). Scopes bulk operations.

---

## F1 — Node Discovery via SSH Handshake

Nodes are not statically listed in bot config. Instead the bot maintains a seed list of SSH addresses. On startup (and on demand), it performs a discovery handshake:

**Handshake flow:**
```
Bot → SSH connect to seed address
Bot → send SYN: { bot_version, request: "services" }
Node → send ACK: {
    node_id:   "prod-1",
    node_name: "Production Node 1",
    cmon_addr: "http://localhost:8000",
    services: [
        { name: "api",      repo: "/srv/api",      manifest: "cmon.toml" },
        { name: "frontend", repo: "/srv/frontend",  manifest: "cmon.toml" },
        { name: "worker",   repo: "/srv/worker",    manifest: "cmon.toml" }
    ]
}
```

The bot stores this in its node registry. Service-to-node resolution is entirely derived from ACK responses — no manual service mapping in config.

**Requirements:**
- Bot must re-run discovery on `/discover` command or on a configurable schedule
- Nodes that fail handshake are marked unhealthy in the registry
- New services added to a node's ACK are available after next discovery without bot restart
- Manifests are read from node disk during or after discovery (via SSH or CMon file endpoint)

---

## F2 — Async Execution via Job Queue

All command execution goes through the job queue. The bot never blocks waiting for a command to complete.

**Flow for every command:**
```
1. User issues slash command
2. Bot validates permissions and input
3. Bot submits job to queue → receives job_id immediately
4. Bot replies to Discord: "Job queued [job_id] — will update when complete"
5. Queue worker executes command on target node via SSH
6. On completion, queue delivers result to bot's result listener
7. Bot edits original message (or posts follow-up) with result
```

**Requirements:**
- Every command execution goes through the queue — no synchronous CMon calls for execution
- Health checks and discovery (read-only, fast) may remain synchronous
- Bot must handle job timeout (configurable, default 5 minutes) and report timeout to Discord
- Multi-node jobs (e.g. `/sync all`) are submitted as N independent jobs; bot aggregates results as they arrive
- Job status must be queryable: `/status <job_id>`
- The job queue is an existing separate project — bot is a client only, no queue internals in this spec

---

## F3 — CI/CD Commands

| Command | Description | Confirm |
|---------|-------------|:-------:|
| `/sync <service> [branch]` | Run service's manifest `sync` command (e.g. `nix run .#sync`) | — |
| `/build <service> [branch]` | Run manifest `build` command | — |
| `/test <service>` | Run manifest `test` command | — |
| `/deploy <service> <branch>` | Run manifest `deploy` or CMon `/deploy_branch` | — |
| `/teardown <service> <branch>` | Tear down a deployed branch | ✓ |
| `/migrate <service>` | Run manifest `migrate` command | ✓ |
| `/run <service> <command>` | Run any named command from the service manifest | — |

**Branch rules:** all branch-accepting commands validate input before submission (configurable regex per service, default `^[a-zA-Z0-9/_.-]+$`). Invalid branch names are rejected immediately without queuing.

---

## F4 — System Operations

| Command | Description | Confirm |
|---------|-------------|:-------:|
| `/health [node\|all]` | Uptime and load. Synchronous (fast read). | — |
| `/logs <service> [n]` | Last N journal entries (default 50) | — |
| `/restart <service>` | Restart the service process on its node | ✓ |
| `/reboot <node>` | Full system reboot | ✓ |
| `/ps <node>` | Running process summary | — |
| `/disk <node>` | Disk usage summary | — |

Confirmation flow: bot posts ephemeral message with Confirm / Cancel buttons. 60-second timeout. Cancels automatically on timeout. Both confirmation and cancellation are logged to audit.

---

## F5 — Multi-Node Orchestration

| Command | Description |
|---------|-------------|
| `/sync all [env]` | Sync all services, optionally scoped to an environment |
| `/deploy all <branch> [env]` | Deploy a branch to every service that has it |
| `/health all` | Aggregate health across all nodes |
| `/status all` | Summary of all running and queued jobs |

**Requirements:**
- Multi-node commands submit independent jobs per node; fan-out handled by the job queue
- Results arrive asynchronously per node; bot updates a single Discord message as each arrives
- Partial failure clearly surfaced: which nodes succeeded, failed, or timed out
- Ordering is not guaranteed across nodes (dependency ordering is out of scope)

---

## F6 — Service & Node Management

| Command | Description | Role |
|---------|-------------|------|
| `/discover` | Re-run SSH handshake against all seeds | ops |
| `/discover <address>` | Add new seed and run discovery | ops |
| `/nodes` | List all discovered nodes and health | dev |
| `/services` | List all known services and their nodes | dev |
| `/services <node>` | List services on a specific node | dev |
| `/manifest <service>` | Show current command manifest for a service | dev |
| `/reload <service>` | Re-read manifest without full rediscovery | ops |

---

## F7 — Quality of Life Commands

| Command | Description |
|---------|-------------|
| `/status <job_id>` | Check status and output of a specific async job |
| `/status all` | Live table of all active jobs across all nodes |
| `/cancel <job_id>` | Cancel a queued job before it starts |
| `/history <service> [n]` | Last N operations on a service |
| `/history <user> [n]` | Last N operations by a Discord user |
| `/diff <service>` | `git log --oneline` between current and upstream — shows what `/sync` would pull |
| `/rollback <service>` | Re-run the last successful deploy job for a service |
| `/env <name>` | Show all nodes and services in a named environment |
| `/pin <job_id>` | Make a job result message permanent in the channel |
| `/help [command]` | List commands available to the caller, or detailed help for one command |

---

## F8 — Permissions

| Role | Permitted |
|------|-----------|
| `cmon-viewer` | `/health`, `/logs`, `/ps`, `/disk`, `/nodes`, `/services`, `/manifest`, `/status`, `/history`, `/diff`, `/help` |
| `cmon-dev` | Above + `/sync`, `/build`, `/test`, `/deploy`, `/teardown`, `/run`, `/rollback`, `/pin` |
| `cmon-ops` | Above + `/restart`, `/reboot`, `/migrate`, `/cancel`, `/discover`, `/reload`, `/env` |

- Unauthorized commands return an ephemeral error visible only to the requester
- Role IDs are configured in `config.toml`, not hardcoded

---

## F9 — Audit Log

Every action recorded:
- Discord user ID and display name
- Command and all arguments
- Target service / node / branch
- Job ID (for async commands)
- Timestamps: submitted and completed
- Outcome: success / failure / cancelled / timeout
- First 500 chars of output on failure

Stored as append-only records (SQLite or flat JSONL). Optionally mirrored to a designated Discord audit channel.

---

## F10 — Response Formatting

- Command output in code blocks, truncated at 1800 chars with "full output: `/status <job_id>`" note
- Multi-node operations: live-updating embed table showing per-node status as jobs complete
- Health responses: node name, uptime, load average, memory in a readable embed
- Job queued confirmation includes job ID and queue position if available
- Error responses include exit code, first N lines of stderr, and job ID for reference

---

## Configuration

```toml
[bot]
token_env        = "DISCORD_BOT_TOKEN"
guild_id         = "..."
audit_channel_id = "..."        # optional
job_timeout_secs = 300

[queue]
broker_url_env  = "JOB_QUEUE_URL"
result_ttl_secs = 3600

[roles]
viewer = ["DISCORD_ROLE_ID"]
dev    = ["DISCORD_ROLE_ID"]
ops    = ["DISCORD_ROLE_ID"]

[discovery]
schedule_minutes = 30
ssh_key_env      = "SSH_KEY_PATH"

[[discovery.seeds]]
address = "user@prod-1.internal"

[[discovery.seeds]]
address = "user@staging.internal"

[[environments]]
name  = "production"
nodes = ["prod-1"]

[[environments]]
name  = "staging"
nodes = ["staging"]
```

No node addresses, service names, or manifest paths are declared in config. All of that comes from discovery.

---

## CMon Agent Changes Required

CMon currently executes commands synchronously — it forks the child, waits for completion, and returns output in the HTTP response. The job queue model requires one change:

### Async Command Mode

- `POST /exec` with `{ command, cwd, async: true }` returns immediately with `{ job_id }`
- `GET /job/<job_id>` returns status and output when complete
- The job queue worker calls CMon in async mode and polls (or subscribes) for results
- All existing synchronous endpoints remain unchanged for health checks and reads

This keeps CMon's existing API intact while enabling the job queue to drive long-running operations without holding open HTTP connections.

---

## Out of Scope

- Web dashboard
- Multi-guild / multi-tenant support
- Automatic rollback on failure (manual `/rollback` only)
- Secret rotation
- Container orchestration (Kubernetes, Nomad)
- Build artifact storage
- Queue internals (separate project)

---

## Open Questions

| # | Question | Impact |
|---|----------|--------|
| 1 | Should the SSH discovery handshake be a small daemon on each node, or a hook in the CMon agent itself? | Daemon is decoupled; CMon hook reduces operational complexity |
| 2 | Does the job queue execute shell commands directly over SSH, or does it call CMon's HTTP API over an SSH tunnel? | Determines whether the async CMon change is needed at all |
| 3 | Should `/rollback` re-run the last deploy job verbatim, or invoke a manifest-defined `rollback` command? | Manifest approach is more flexible but requires service owners to define it |
| 4 | Should `/deploy all` respect a declared service dependency order, or always run in parallel? | Dependency ordering adds correctness but significantly increases complexity |
| 5 | Where does the job queue broker run — on the bot host, a dedicated node, or one of the CMon nodes? | Affects availability and deployment topology |
