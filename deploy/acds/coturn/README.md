# ASCII-Chat Coturn STUN/TURN Server

This directory contains the deployment configuration for the ascii-chat STUN/TURN server using [coturn](https://github.com/coturn/coturn).

## What is STUN/TURN?

- **STUN** (Session Traversal Utilities for NAT): Helps WebRTC peers discover their public IP address and port when behind NAT
- **TURN** (Traversal Using Relays around NAT): Relays media streams when direct peer-to-peer connection fails (e.g., symmetric NAT, firewall restrictions)

## Architecture

```
┌─────────────┐                 ┌──────────────┐                 ┌─────────────┐
│  Client A   │────STUN────────▶│    Coturn    │◀────STUN────────│  Client B   │
│  (behind    │                 │  STUN/TURN   │                 │  (behind    │
│   NAT)      │                 │    Server    │                 │   NAT)      │
└─────────────┘                 └──────────────┘                 └─────────────┘
       │                               │                                │
       │         (if direct fails)     │                                │
       └──────────────TURN Relay───────┴─────────TURN Relay─────────────┘
```

## Files

- `docker-compose.yml` - Docker Compose configuration for coturn container
- `turnserver.conf` - Coturn server configuration
- `coturn.service` - Systemd service unit file
- `deploy.sh` - Automated deployment script
- `README.md` - This file

## Deployment

### Prerequisites

- Docker and Docker Compose installed
- Root/sudo access to the server
- DNS records for `stun.ascii-chat.com` and `turn.ascii-chat.com` pointing to server IP
- Firewall ports opened (see Firewall Configuration below)

### Quick Deploy

1. **On your local machine**, clone the repo and push to server:
   ```bash
   git clone https://github.com/zfogg/ascii-chat.git
   ssh sidechain "sudo rm -rf /opt/ascii-chat && sudo mkdir -p /opt/ascii-chat"
   scp -r ascii-chat/* sidechain:/tmp/ascii-chat/
   ssh sidechain "sudo mv /tmp/ascii-chat/* /opt/ascii-chat/"
   ```

2. **On the server**, run the deployment script:
   ```bash
   ssh sidechain
   cd /opt/ascii-chat/deploy/acds/coturn
   sudo chmod +x deploy.sh
   sudo ./deploy.sh
   ```

### Manual Deploy

```bash
# 1. Copy files to /opt/ascii-chat
sudo mkdir -p /opt/ascii-chat/deploy/acds/coturn
sudo cp docker-compose.yml turnserver.conf /opt/ascii-chat/deploy/acds/coturn/

# 2. Install systemd service
sudo cp coturn.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable coturn
sudo systemctl start coturn

# 3. Check status
sudo systemctl status coturn
docker-compose ps
```

## Configuration

### Authentication

**Default credentials** (⚠️ CHANGE IN PRODUCTION):
- Username: `ascii` / Password: `changeme123`
- Username: `webrtc` / Password: `changeme456`

Edit `turnserver.conf` and modify the `user=` lines:
```conf
user=your-username:your-secure-password
```

Then restart:
```bash
sudo systemctl restart coturn
```

### TLS/DTLS Support (Optional)

For TURNS (TURN over TLS), you need SSL certificates:

1. Obtain certificates (e.g., Let's Encrypt):
   ```bash
   sudo certbot certonly --standalone -d turn.ascii-chat.com
   ```

2. Update `turnserver.conf`:
   ```conf
   tls-listening-port=5349
   cert=/etc/letsencrypt/live/turn.ascii-chat.com/fullchain.pem
   pkey=/etc/letsencrypt/live/turn.ascii-chat.com/privkey.pem
   ```

3. Mount certificates in `docker-compose.yml`:
   ```yaml
   volumes:
     - /etc/letsencrypt:/etc/letsencrypt:ro
   ```

4. Restart:
   ```bash
   sudo systemctl restart coturn
   ```

## Firewall Configuration

Open these ports on your server firewall:

```bash
# STUN/TURN
sudo ufw allow 3478/tcp
sudo ufw allow 3478/udp

# STUN/TURN over TLS (if using TLS)
sudo ufw allow 5349/tcp
sudo ufw allow 5349/udp

# TURN relay port range
sudo ufw allow 49152:65535/udp
```

## DNS Configuration

Create these DNS records:

```
stun.ascii-chat.com.  A      <server-ip>
turn.ascii-chat.com.  A      <server-ip>

# If using IPv6:
stun.ascii-chat.com.  AAAA   <server-ipv6>
turn.ascii-chat.com.  AAAA   <server-ipv6>
```

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
  -u ascii -w changeme123 \
  -v turn.ascii-chat.com
```

### Test from Web Browser

Open browser console and run:
```javascript
const pc = new RTCPeerConnection({
  iceServers: [
    { urls: 'stun:stun.ascii-chat.com:3478' },
    {
      urls: 'turn:turn.ascii-chat.com:3478',
      username: 'ascii',
      credential: 'changeme123'
    }
  ]
});

pc.createDataChannel('test');
pc.createOffer().then(offer => pc.setLocalDescription(offer));

// Wait for ICE candidates
pc.onicecandidate = (e) => {
  if (e.candidate) {
    console.log('ICE Candidate:', e.candidate.candidate);
  }
};
```

You should see candidates with `typ relay` (TURN) and `typ srflx` (STUN).

## Management

### Start/Stop Service

```bash
# Start
sudo systemctl start coturn

# Stop
sudo systemctl stop coturn

# Restart
sudo systemctl restart coturn

# Status
sudo systemctl status coturn
```

### View Logs

```bash
# Systemd logs
sudo journalctl -u coturn -f

# Docker logs
cd /opt/ascii-chat/deploy/acds/coturn
docker-compose logs -f

# Coturn log file
docker exec -it ascii-chat-coturn tail -f /var/log/coturn.log
```

### Update Configuration

```bash
# Edit config
sudo vim /opt/ascii-chat/deploy/acds/coturn/turnserver.conf

# Restart to apply
sudo systemctl restart coturn
```

## Monitoring

### Check Active Sessions

```bash
docker exec -it ascii-chat-coturn turnadmin -l
```

### Check Container Health

```bash
cd /opt/ascii-chat/deploy/acds/coturn
docker-compose ps
```

### Check Listening Ports

```bash
sudo ss -tulnp | grep -E ':(3478|5349)'
```

## Troubleshooting

### Container won't start

```bash
# Check logs
sudo journalctl -u coturn -n 50

# Check Docker logs
cd /opt/ascii-chat/deploy/acds/coturn
docker-compose logs
```

### Firewall blocking

```bash
# Test if port is reachable
nc -zvu <server-ip> 3478
telnet <server-ip> 3478
```

### TURN not working

1. Verify authentication credentials are correct
2. Check firewall allows UDP port range 49152-65535
3. Verify external IP detection:
   ```bash
   docker exec -it ascii-chat-coturn ip addr show
   ```

### Permission denied errors

Make sure the systemd service has correct permissions:
```bash
sudo chown root:root /etc/systemd/system/coturn.service
sudo chmod 644 /etc/systemd/system/coturn.service
sudo systemctl daemon-reload
```

## Security Considerations

1. **Change default credentials** immediately after deployment
2. **Use TLS** for production (TURNS on port 5349)
3. **Limit relay bandwidth** by setting `max-bps` in turnserver.conf
4. **Enable authentication** - never run without credentials
5. **Monitor logs** for abuse/unusual traffic
6. **Consider rate limiting** at firewall level

## Integration with ASCII-Chat

To use these STUN/TURN servers in ascii-chat WebRTC code:

```c
// In your WebRTC configuration
const char* ice_servers[] = {
    "stun:stun.ascii-chat.com:3478",
    "turn:ascii:changeme123@turn.ascii-chat.com:3478"
};
```

Or in JavaScript for testing:
```javascript
const config = {
  iceServers: [
    { urls: 'stun:stun.ascii-chat.com:3478' },
    {
      urls: 'turn:turn.ascii-chat.com:3478',
      username: 'ascii',
      credential: 'changeme123'
    }
  ]
};
```

## Resources

- [Coturn Documentation](https://github.com/coturn/coturn/wiki/turnserver)
- [WebRTC ICE/STUN/TURN Overview](https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API/Connectivity)
- [Testing STUN/TURN Servers](https://webrtc.github.io/samples/src/content/peerconnection/trickle-ice/)
