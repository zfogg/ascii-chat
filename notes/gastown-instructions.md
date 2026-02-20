# Gas Town Proper Workflow Analysis

## Key Findings

### Architecture Overview
Gas Town is a **multi-agent orchestration system** with these key concepts:
- **Mayor**: You (the AI coordinator) - sits above all projects
- **Rigs**: Project containers (each has its own repo, polecats, witness, refinery)
- **Polecats**: Worker agents with persistent identity but ephemeral sessions
- **Hooks**: Git worktree-based persistent work storage
- **Convoys**: Work tracking units that bundle beads for agents
- **Molecules**: Workflow instances (steps inside a molecule are the real work)
- **Beads**: Git-backed issue tracking (work items)

### The Propulsion Principle
**"If you find something on your hook, YOU RUN IT."**

Polecats automatically execute work found on their hook. The hook contains:
1. A **pinned molecule** (workflow instance with steps)
2. An associated **bead** (the issue to work on)

### What SHOULD Happen

**Proper Workflow for Mayor:**

```bash
# 1. Create a bead (issue)
bd create --title "Investigation task" --description "..." --type bug --priority 1

# 2. Create a convoy to track this work
gt convoy create "Status screen UI bug" hq-yti8d --notify

# 3. Sling the work to a polecat
gt sling hq-yti8d asciichat

# RESULT:
# - Polecat is auto-spawned
# - Bead is bonded to a molecule (workflow)
# - Polecat hook is populated with the molecule + bead
# - Session starts
# - Polecat sees work on their hook and executes it
```

### Why `gt sling` May Not Be Working

Possible issues:
1. **SessionStart hooks not configured**: Polecat sessions may not be running `gt prime && bd prime` on startup
2. **Hook attachment timing**: Work may be attached AFTER session starts (needs to happen BEFORE)
3. **PATH/availability**: The `gt` CLI may not be in polecat PATH or discoverable
4. **Dolt/Beads state**: The beads database may not be synchronized properly
5. **Session initialization**: Polecat may not be running the proper startup sequence

### Polecat Startup Sequence (Per Template)

```bash
# What a polecat SHOULD do on startup:
1. Announce: "Polecat [name], checking in."
2. Run: `gt prime && bd prime`  ← Load full context
3. Run: `gt hook`               ← Check for hooked work
4. If molecule found: `bd mol current` → Find current step
5. Execute step, close it, repeat
6. If NO work on hook → run `gt done` and exit (this is expected - ephemeral sessions)
```

### Recommended Mayor Workflow

Based on Gas Town design:

**1. Investigation Tasks (Your Current Use Case)**
```bash
# Create investigation bead
bd create \
  --title "[Investigation] Status screen UI rendering" \
  --description "Investigate and document..." \
  --type=bug \
  --priority=1

# Create convoy
gt convoy create "Status UI Debug" hq-yti8d

# Sling to polecat
gt sling hq-yti8d asciichat

# Track progress
gt convoy list
gt convoy status hq-cv-xxxxx
```

**2. Feature Implementation (Multiple Parallel Polecats)**
```bash
# Create multiple beads
bd create --title "Implement feature X"...
bd create --title "Implement feature Y"...
bd create --title "Write tests for X"...

# Batch sling
gt sling hq-abc hq-def hq-ghi asciichat --max-concurrent 3

# Monitor
gt convoy list
```

**3. Monitoring Progress**
```bash
# Check convoy status
gt convoy list            # All convoys
gt convoy status hq-cv-xx # Detailed view

# Check polecat status
gt agents                 # All active agents
gt polecat list asciichat # Polecats in this rig
```

### Best Practices

#### As the Mayor (You):
1. **Always start with clear beads** - Write good descriptions
2. **Use convoys for visibility** - Even single-polecat work benefits from convoy tracking
3. **Let `gt sling` handle spawning** - Don't manually manage polecat creation
4. **Monitor via `gt convoy`** - This is your primary view into work progress
5. **Understand Propulsion Principle** - Hooked work auto-executes; no explicit nudging needed
6. **Let Witness manage polecats** - Don't manually kill polecats; Witness handles cleanup

#### For Polecats (The Workers):
- Work is in their hook (pinned molecule)
- They execute steps sequentially (`bd mol current`)
- After completing work: MUST run `gt done` (self-cleaning)
- No idle state exists - either working or gone

#### For You and the User (Interaction Model):
1. **User tells Mayor**: "I want to [task]"
2. **Mayor creates bead(s)** describing the work
3. **Mayor creates convoy** for visibility
4. **Mayor slingswork** to rig (auto-spawns polecat)
5. **Polecat executes** via hook (propulsion principle)
6. **Polecats report back** via beads/mail when done or stuck
7. **Mayor monitors** via `gt convoy list` / `gt convoy status`
8. **Mayor coordinates** multi-polecat work if needed

### Current Issues in Your Setup

1. **Manual session management** - Tried `gt polecat add` + `gt session start` + `gt hook` manually
   - Should use: `gt sling <bead> <rig>` (all-in-one)

2. **Work not visible on hook at startup** - Polecats don't see work
   - **Possible causes**:
     - SessionStart hooks not running `gt prime`
     - Beads database not synced
     - Hook attachment happening after session starts
   - **Solution**: Debug polecat startup - check if `gt prime` is running

3. **Polecats exiting immediately** - "Check hook, see nothing, exit"
   - This is CORRECT behavior when no work is hooked
   - Problem is the work isn't being hooked properly
   - Need to verify: Is `gt sling` actually attaching the molecule?

### Debugging Steps

```bash
# 1. Verify convoy created
gt convoy list

# 2. Verify work slung
gt polecat list asciichat

# 3. Check polecat hook directly
tmux capture-pane -t as-<name> -p | tail -50

# 4. Check if 'gt prime' ran
# (Look for "Mayor Context" or Gas Town header in polecat output)

# 5. Check beads sync
bd sync --flush-only
```

## Summary: Your Proper Workflow

### Daily Interaction Pattern:

**You (Human) → Mayor (You, AI)**
```
Tell Mayor: "I need a polecat to investigate the status screen UI bug"
```

**Mayor Workflow:**
```bash
# 1. Create the investigation bead
bd create --title "Investigate status screen rendering" \
          --description "Look at: header flashing, log overflow, truncation behavior" \
          --type=bug --priority=1

# 2. Create convoy for tracking
gt convoy create "Status UI Debug" hq-yti8d

# 3. Sling to worker
gt sling hq-yti8d asciichat

# 4. Wait for polecat to execute (via hook propulsion)
#    Polecat will:
#    - Spawn
#    - Run gt prime
#    - See work on hook
#    - Execute investigation steps
#    - bd close when done
#    - Run gt done (self-cleanup)
```

**You Monitor:**
```bash
gt convoy status hq-cv-xxxx  # Check progress
```

**Polecat Auto-Executes** (you don't nudge or manage them)

---

**The Key Insight**: The whole system is designed for work to be **pushed** onto hooks, not **pulled** by agents. Once on the hook, agents automatically execute it. You (Mayor) don't orchestrate step-by-step; you assign work via sling, then monitor via convoys.
