# Shared package - builds on base with dependencies already installed
FROM zfogg/ascii-chat-web-base:latest

# Build shared package only
RUN cd web && bun run --filter './packages/shared' build
