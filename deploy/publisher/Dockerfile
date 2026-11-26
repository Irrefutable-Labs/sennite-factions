# The publisher's server (Server/), built from a committed revision. Build context: Server/.
FROM node:22-alpine AS build
WORKDIR /src
COPY package.json package-lock.json ./
COPY vendor ./vendor
RUN npm ci --no-audit --no-fund
COPY tsconfig.json tsconfig.build.json ./
COPY src ./src
RUN npm run build && npm prune --omit=dev

FROM node:22-alpine
WORKDIR /app
COPY --from=build /src/node_modules ./node_modules
COPY --from=build /src/dist ./dist
COPY package.json ./
COPY world ./world
USER 10001:10001
CMD ["node", "dist/publisher/main.js"]
