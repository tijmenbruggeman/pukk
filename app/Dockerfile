# Pukk web app. The build stage runs on the build machine's own platform (the
# output is plain JavaScript), the runtime stage only copies files, so a
# multi-arch image needs no emulation.

FROM --platform=$BUILDPLATFORM node:24-alpine AS build
WORKDIR /src
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY . .
RUN npm run build && mkdir -p /empty

FROM node:24-alpine
ENV NODE_ENV=production \
    PORT=8080 \
    PUKK_DATA_DIR=/data
WORKDIR /app
COPY --from=build /src/dist ./dist
COPY --from=build --chown=node:node /empty /data
USER node
VOLUME /data
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s CMD wget -qO- http://127.0.0.1:8080/healthz || exit 1
CMD ["node", "--disable-warning=ExperimentalWarning", "dist/server.js"]
