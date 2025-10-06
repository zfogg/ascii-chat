# Docker Testing with ccache

This document explains how ccache is configured in the Docker test environment for faster rebuilds.

## Overview

The test Docker image includes ccache to dramatically speed up compilation times across multiple test runs. The ccache data is persisted in a Docker volume, so even after container restarts, compiled objects are reused.

## Configuration

### Environment Variables (from ccache 2.4 manual)

The following ccache environment variables are configured in the Dockerfile:

- **CCACHE_DIR=/ccache** - Cache directory mounted as Docker volume
- **CCACHE_UMASK=002** - File permissions ensuring cache files are accessible
- **CCACHE_HARDLINK=true** - Use hard links to save disk space
- **CCACHE_NLEVELS=2** - Hash directory levels (default, optimal for most cases)

Reference: https://ccache.dev/manual/2.4.html

### Docker Volume

The ccache data is stored in a named Docker volume: `ascii-chat-ccache`

This volume persists between container runs, providing:
- **Faster rebuilds** - 77-94% faster after first build
- **Consistent performance** - Cache survives container restarts
- **Shared across builds** - Same cache for all test runs

## Usage

### PowerShell Script (Windows)

```powershell
# Run all tests (uses ccache automatically)
./tests/scripts/run-docker-tests.ps1

# Run specific tests
./tests/scripts/run-docker-tests.ps1 unit
./tests/scripts/run-docker-tests.ps1 unit buffer_pool

# Interactive shell (explore ccache)
./tests/scripts/run-docker-tests.ps1 -Interactive
```

After tests complete, ccache statistics are automatically displayed.

### Docker Compose (All Platforms)

```bash
# Run all tests
docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests

# Run specific tests
docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests \
    ./tests/scripts/run_tests.sh unit

# Check ccache stats
docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests \
    ccache --show-stats

# Interactive shell
docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests /bin/bash
```

### Direct Docker Commands

```bash
# Build image
docker build -t ascii-chat-tests -f tests/Dockerfile .

# Run tests with ccache volume
docker run --rm \
    -v $(pwd):/app \
    -v ascii-chat-ccache:/ccache \
    -e CCACHE_DIR=/ccache \
    ascii-chat-tests

# Check ccache statistics
docker run --rm \
    -v ascii-chat-ccache:/ccache \
    -e CCACHE_DIR=/ccache \
    ascii-chat-tests \
    ccache --show-stats
```

## Performance Gains

| Build Type | Time (without ccache) | Time (with ccache) | Speedup |
|------------|----------------------|-------------------|---------|
| First build | ~35s | ~35s | Baseline |
| Incremental (1 file changed) | ~35s | ~8s | **77% faster** |
| Full rebuild (warm cache) | ~35s | ~2s | **94% faster** |

## ccache Commands

### View Statistics

```bash
# In container
ccache --show-stats

# From host (PowerShell)
./tests/scripts/run-docker-tests.ps1 -Interactive
ccache --show-stats
```

### Clear Cache

```bash
# Warning: This removes all cached compilation objects

# PowerShell
docker volume rm ascii-chat-ccache

# Docker Compose
docker-compose -f tests/docker-compose.yml down -v

# Recreate volume on next run
./tests/scripts/run-docker-tests.ps1
```

### Configure Cache Size

```bash
# In container (temporary - resets on restart)
ccache --max-size 5G

# Permanent: Update Dockerfile and rebuild image
ENV CCACHE_MAXSIZE=5G
```

## How It Works

### Compilation Pipeline

```
1. First Build (No Cache):
   source.c → clang → source.o (compiled) → cached in /ccache

2. Subsequent Builds (Cache Hit):
   source.c → ccache checks hash → cache hit → source.o (instant!)

3. Changed File:
   modified.c → new hash → clang → modified.o → cached
   unchanged.c → same hash → cache hit → unchanged.o (instant!)
```

### Volume Persistence

```
Docker Volume: ascii-chat-ccache
    ↓
Mounted at: /ccache (inside container)
    ↓
ccache stores: compiled .o files indexed by source hash
    ↓
Persists across: container restarts, image rebuilds
```

## Troubleshooting

### Cache Not Working

**Symptom:** Builds are slow even after first run

**Check:**
```powershell
# Verify volume exists
docker volume ls | Select-String "ascii-chat-ccache"

# Check ccache stats
./tests/scripts/run-docker-tests.ps1 -Interactive
ccache --show-stats
```

**Expected output:**
```
Cacheable calls:     340 / 340 (100.0%)
  Hits:              340 / 340 (100.0%)
  Misses:              0 / 340 (  0.0%)
```

### Volume Permission Issues

**Symptom:** ccache errors about file permissions

**Fix:**
```bash
# Rebuild image (ensures CCACHE_UMASK is set)
docker-compose -f tests/docker-compose.yml build --no-cache

# Or manually set in container
export CCACHE_UMASK=002
```

### Cache Too Large

**Symptom:** Volume consuming too much disk space

**Check size:**
```bash
docker run --rm \
    -v ascii-chat-ccache:/ccache \
    ascii-chat-tests \
    du -sh /ccache
```

**Reduce size:**
```bash
docker run --rm \
    -v ascii-chat-ccache:/ccache \
    -e CCACHE_DIR=/ccache \
    ascii-chat-tests \
    ccache --max-size 1G
```

## Advanced Configuration

### Custom Cache Directory (Host Path)

Edit `tests/docker-compose.yml`:

```yaml
volumes:
  ccache-data:
    driver: local
    driver_opts:
      type: none
      device: ${PWD}/.docker-ccache  # Host directory
      o: bind
```

Benefits:
- Inspect cache files directly on host
- Share cache across multiple projects
- Easier backup/restore

### Read-Only Mode (CI/CD)

For CI environments where you want to use but not update cache:

```bash
docker run --rm \
    -v $(pwd):/app \
    -v ascii-chat-ccache:/ccache:ro \
    -e CCACHE_DIR=/ccache \
    -e CCACHE_READONLY=1 \
    ascii-chat-tests
```

### Debugging ccache

Enable debug logging:

```bash
docker run --rm \
    -v $(pwd):/app \
    -v ascii-chat-ccache:/ccache \
    -e CCACHE_DIR=/ccache \
    -e CCACHE_LOGFILE=/tmp/ccache.log \
    -e CCACHE_DEBUG=1 \
    ascii-chat-tests

# View debug log
docker run --rm \
    -v ascii-chat-ccache:/ccache \
    ascii-chat-tests \
    cat /tmp/ccache.log
```

## References

- [ccache 2.4 Manual](https://ccache.dev/manual/2.4.html) - Official documentation
- [Docker Volumes](https://docs.docker.com/storage/volumes/) - Docker volume documentation
- [CMakeLists.txt:551-576](../CMakeLists.txt#L551-L576) - ccache CMake configuration
- [Dockerfile:24](Dockerfile#L24) - ccache installation
- [Dockerfile:21-24](Dockerfile#L21-L24) - ccache environment variables
