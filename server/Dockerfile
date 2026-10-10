# Build from the repository root:  docker build -f server/Dockerfile .
FROM node:22-slim
WORKDIR /app
ENV NODE_ENV=production

# Install only the server's dependencies (the workspace manifests are needed for the lockfile).
COPY package.json package-lock.json ./
COPY packages/engine/package.json packages/engine/
COPY server/package.json server/
COPY app/package.json app/
RUN npm ci --workspace @pocket-club/server --omit=dev && npm cache clean --force

COPY packages/engine/src packages/engine/src
COPY server/src server/src

USER node
WORKDIR /app/server
EXPOSE 3000
# Run node directly (not through npm) so SIGTERM reaches the server and running hands can finish.
CMD ["node", "--import", "tsx", "src/index.ts"]
