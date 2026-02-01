# Coturn STUN/TURN Server Deployment Summary

**Date**: January 2, 2026
**Server**: sidechain (135.181.27.224)
**Status**: ✅ DEPLOYED AND RUNNING

---

## What Was Deployed

### 1. Coturn STUN/TURN Server
- **Container**: `ascii-chat-coturn` (coturn/coturn:latest)
- **Status**: Running with host networking
- **Ports Listening**:
  - 3478 UDP/TCP (STUN/TURN)
  - 49152-65535 UDP (TURN relay port range)

### 2. Systemd Service
- **Service**: `coturn.service`
- **Status**: Enabled (starts on boot)
- **Location**: `/etc/systemd/system/coturn.service`

### 3. Firewall Configuration
- **3478/tcp**: STUN/TURN TCP ✅
- **3478/udp**: STUN/TURN UDP ✅
- **49152:65535/udp**: TURN relay port range ✅

---

## Security Credentials

**⚠️ SAVE THESE - They are NOT stored anywhere else!**

### TURN Server Credentials

```
Primary User:
  Username: ascii
  Password: 0aa9917b4dad1b01631e87a32b875e09

Secondary User:
  Username: webrtc
  Password: 8d9ed3956272058a6460c93084b83afb
```

**Configuration File**: `/opt/ascii-chat/deploy/acds/coturn/turnserver.conf`

---

## DNS Configuration

**⚠️ IMPORTANT**: Disable Cloudflare proxy (must be DNS-only)

```dns
# A Records (IPv4) - MUST BE GRAY CLOUD IN CLOUDFLARE
stun.ascii-chat.com    A    135.181.27.224    🌐 DNS only
turn.ascii-chat.com    A    135.181.27.224    🌐 DNS only

# AAAA Records (IPv6) - MUST BE GRAY CLOUD IN CLOUDFLARE
stun.ascii-chat.com    AAAA    2a01:4f9:c012:d912::1    🌐 DNS only
turn.ascii-chat.com    AAAA    2a01:4f9:c012:d912::1    🌐 DNS only
```

**Why no proxy?**
- STUN/TURN use UDP/TCP on port 3478
- Cloudflare proxy only works for HTTP/HTTPS
- Direct access to the server IP is required

---

## ascii-chat Integration

### Default STUN/TURN Configuration

The ascii-chat codebase now includes these servers by default:

**STUN Servers** (in order):
1. `stun:stun.ascii-chat.com:3478` (Primary - our server)
2. `stun:stun.l.google.com:19302` (Fallback - Google public STUN)

**TURN Server**:
- `turn:turn.ascii-chat.com:3478`
- Username: `ascii`
- Password: `0aa9917b4dad1b01631e87a32b875e09`

**Code Location**: `lib/options/presets.c:540-554`

**Environment Variables** (optional overrides):
```bash
export ASCII_CHAT_STUN_SERVERS="stun:custom.example.com:3478"
export ASCII_CHAT_TURN_SERVERS="turn:custom.example.com:3478"
export ASCII_CHAT_TURN_USERNAME="myuser"
export ASCII_CHAT_TURN_CREDENTIAL="mypassword"
```

---

## Management Commands

### Service Control
```bash
# Check status
sudo systemctl status coturn

# Restart
sudo systemctl restart coturn

# Stop
sudo systemctl stop coturn

# Start
sudo systemctl start coturn

# Disable auto-start
sudo systemctl disable coturn
```

### Logs
```bash
# Systemd logs
sudo journalctl -u coturn -f

# Docker logs
cd /opt/ascii-chat/deploy/acds/coturn
sudo docker-compose logs -f

# Coturn internal log
sudo docker exec ascii-chat-coturn tail -f /var/log/coturn.log
```

### Active Sessions
```bash
# List active TURN sessions
sudo docker exec ascii-chat-coturn turnadmin -l
```

### Container Management
```bash
cd /opt/ascii-chat/deploy/acds/coturn

# Check container status
sudo docker-compose ps

# Restart container
sudo docker-compose restart

# Stop and remove
sudo docker-compose down

# Start
sudo docker-compose up -d
```

---

## Testing

### Test STUN Server

```bash
# Using stunclient (install: apt-get install stuntman-client)
stunclient stun.ascii-chat.com 3478
```

### Test TURN Server

```bash
# Using turnutils_uclient (included in coturn)
docker exec -it ascii-chat-coturn turnutils_uclient \
  -u ascii -w 0aa9917b4dad1b01631e87a32b875e09 \
  -v turn.ascii-chat.com
```

### Test from Browser

Open browser console (F12) and run:

```javascript
const pc = new RTCPeerConnection({
  iceServers: [
    { urls: 'stun:stun.ascii-chat.com:3478' },
    { urls: 'stun:stun.l.google.com:19302' },
    {
      urls: 'turn:turn.ascii-chat.com:3478',
      username: 'ascii',
      credential: '0aa9917b4dad1b01631e87a32b875e09'
    }
  ]
});

pc.createDataChannel('test');
pc.createOffer().then(offer => pc.setLocalDescription(offer));

// Watch ICE candidates appear
pc.onicecandidate = (e) => {
  if (e.candidate) {
    console.log('ICE Candidate:', e.candidate.candidate);
    // Look for "typ relay" (TURN) and "typ srflx" (STUN)
  }
};
```

Expected output:
- `typ host` - Local IP
- `typ srflx` - Server reflexive (STUN discovered public IP)
- `typ relay` - Relayed candidate (TURN server)

---

## Monitoring

### Check Listening Ports
```bash
sudo ss -tulnp | grep -E ':(3478|5349)'
```

### Check Server Public IP
```bash
# The server should be listening on these IPs:
# - 135.181.27.224 (public IPv4)
# - 2a01:4f9:c012:d912::1 (public IPv6)
# - 127.0.0.1 (localhost)
# - Various Docker bridge networks (172.x.x.x)
```

### Resource Usage
```bash
# Container stats
docker stats ascii-chat-coturn

# Memory/CPU limits (from docker-compose.yml):
# - Memory: 512M max, 256M reserved
# - CPU: 1.0 cores max, 0.5 reserved
```

---

## Security Best Practices

1. ✅ **Credentials Changed**: Default passwords replaced with secure random values
2. ✅ **Firewall Configured**: Only necessary ports opened
3. ✅ **No CLI Access**: Telnet CLI disabled in coturn config
4. ✅ **Loopback Prevention**: No relay to localhost
5. ✅ **Resource Limits**: Docker container has memory/CPU limits

**Recommended Next Steps**:
- [ ] Monitor coturn logs for unusual activity
- [ ] Set up log rotation for Docker logs
- [ ] Consider adding TLS certificates for TURNS (port 5349)
- [ ] Implement rate limiting at firewall level if abuse occurs
- [ ] Consider rotating TURN credentials periodically

---

## Files Created

```
/opt/ascii-chat/deploy/acds/coturn/
├── docker-compose.yml          # Docker Compose configuration
├── turnserver.conf             # Coturn server configuration
├── deploy.sh                   # Automated deployment script
├── README.md                   # Full documentation
└── DEPLOYMENT_SUMMARY.md       # This file

/etc/systemd/system/
└── coturn.service              # Systemd service unit

Repository changes:
├── lib/options/presets.c       # Updated STUN/TURN defaults
└── deploy/acds/coturn/         # All deployment files
```

---

## Troubleshooting

### Container won't start
```bash
sudo journalctl -u coturn -n 50
cd /opt/ascii-chat/deploy/acds/coturn
sudo docker-compose logs
```

### Ports not listening
```bash
# Check if coturn process is running
sudo docker exec ascii-chat-coturn pidof turnserver

# Check firewall
sudo ufw status
```

### TURN authentication failing
```bash
# Verify credentials in config
sudo grep '^user=' /opt/ascii-chat/deploy/acds/coturn/turnserver.conf

# Check coturn logs for auth errors
sudo docker logs ascii-chat-coturn | grep -i auth
```

### DNS not resolving
```bash
# Test DNS resolution
dig stun.ascii-chat.com
dig turn.ascii-chat.com

# Verify Cloudflare proxy is DISABLED (gray cloud)
```

---

## Deployment History

| Date | Action | Status |
|------|--------|--------|
| 2026-01-02 07:49 | Initial deployment | ✅ Success |
| 2026-01-02 07:57 | Credentials updated | ✅ Success |
| 2026-01-02 08:00 | ascii-chat integration | ✅ Success |

---

## Support

For issues or questions:
- Repository: https://github.com/zfogg/ascii-chat
- Documentation: `/opt/ascii-chat/deploy/acds/coturn/README.md`
- Logs: `sudo journalctl -u coturn -f`
