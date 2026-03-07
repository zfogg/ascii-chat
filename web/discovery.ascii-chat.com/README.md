# 🔍 discovery.ascii-chat.com

Official ACDS (ASCII-Chat Discovery Service) public key distribution website

🌐 **[discovery.ascii-chat.com](https://discovery.ascii-chat.com)**

## What This Is

This repository hosts the static website for ACDS public key distribution. The website serves Ed25519 public keys over HTTPS, which clients use to verify the identity of the official ACDS server at `discovery-service.ascii-chat.com:27225`.

The site provides:

- SSH Ed25519 public key (`/key.pub`)
- GPG Ed25519 public key (`/key.gpg`)
- Documentation on ACDS trust model and usage

## How It Works

### Key Generation and Export

The ACDS server uses a libsodium Ed25519 identity key stored in the `acds_identity` file (64 bytes: 32-byte seed + 32-byte public key).

The `export-keys.sh` script extracts the public key and converts it to distributable formats:

1. **Extracts public key**: Uses `dd` to extract the last 32 bytes from `acds_identity`
2. **SSH format**: Python script builds SSH wire format and base64 encodes it → `public/key.pub`
3. **GPG format**: Copies pre-generated GPG key → `public/key.gpg`

### Website Build

The site is a React/Vite SPA that:

- Fetches public keys from `/key.pub` and `/key.gpg`
- Displays fingerprints and usage examples
- Provides ACDS documentation

**Tech stack:**

- React 18
- Vite 6 (build tool)
- Vercel Analytics

### Deployment

The website is deployed to Vercel and serves keys over HTTPS. Clients download keys via HTTPS (verified by system CA certificates), establishing trust for the raw TCP connection to the ACDS server.

**Trust chain:**

```
HTTPS CA → discovery.ascii-chat.com → ACDS public key → discovery-service.ascii-chat.com:27225
```

## Development

```bash
# Install dependencies
npm install

# Run dev server
npm run dev

# Build for production
npm run build

# Preview production build
npm run preview
```

## Repository Structure

```
├── public/
│   ├── key.pub              # SSH Ed25519 public key (served at /key.pub)
│   ├── key.gpg              # GPG Ed25519 public key (served at /key.gpg)
│   └── favicon*.png/ico     # Site icons
├── src/
│   ├── App.jsx              # Main React component with key display and docs
│   ├── App.css              # Styling
│   └── main.jsx             # React entry point
├── export-keys.sh           # Script to extract/convert keys from acds_identity
├── gpg-gen-key.batch        # GPG key generation batch file
├── acds_identity            # ACDS server's Ed25519 identity (private, not in git)
├── acds_identity.pub        # Public key (raw 32 bytes)
├── acds_identity.pub.gpg    # GPG public key export
├── index.html               # HTML entry point
├── package.json             # Node dependencies and scripts
└── vite.config.js           # Vite configuration
```

## Security Notes

- The `acds_identity` private key is NOT committed to git (in `.gitignore`)
- Only public keys are served via the website
- Keys are verified by HTTPS (system CA trust)
- Clients automatically trust keys from `discovery.ascii-chat.com` when using the official ACDS server

## Related Projects

- **[ascii-chat](https://github.com/zfogg/ascii-chat)** - Main project repository
- **[ascii-chat.com](https://github.com/zfogg/ascii-chat.com)** - Homepage and documentation site
- **[web.ascii-chat.com](https://github.com/zfogg/ascii-chat)** - Browser client app
- **ACDS server** - Runs at `discovery-service.ascii-chat.com:27225` (not open source)
