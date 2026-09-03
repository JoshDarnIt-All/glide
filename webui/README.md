# Glide web UI (M4)

Preact + TypeScript + Vite. Talks to the REST/WebSocket API in
`../docs/api.md`. See `../docs/m4_ui_plan.md` for the design/technical
decisions behind this.

## Setup

Requires Node.js/npm (separate from the PlatformIO/C++ toolchain used
for the firmware itself).

```
npm install
```

## Developing against real hardware

```
cp .env.example .env.local
# edit .env.local: set VITE_DEVICE_HOST to your device's glide.local
# or IP (shown on the serial monitor at boot, or GET /api/v1/wifi)
npm run dev
```

Opens a dev server with instant hot-reload; `/api` and `/ws` requests
are proxied to the real device (see `vite.config.ts`), so no firmware
rebuild is needed while iterating on the UI.

## Production build

```
npm run build
```

Outputs to `dist/`. You normally don't need to run this by hand --
`firmware/scripts/embed_webui.py` runs it automatically (via
PlatformIO's `extra_scripts`) every time you build or upload the
firmware, and embeds the gzipped result directly into the firmware
image. See that script and `docs/m4_ui_plan.md`'s "Technical
architecture" section for why (flash budget, why it's not on the
LittleFS partition, etc).
