import { defineConfig, loadEnv } from "vite";
import preact from "@preact/preset-vite";

// Dev-time only: proxies /api and /ws to the real device so `npm run
// dev` gives instant hot-reload against actual hardware, without
// needing the embed-into-firmware pipeline for iteration. Never
// hardcode a specific device IP/hostname here -- it goes stale the
// moment the device reconnects to a different network or DHCP lease.
// Set VITE_DEVICE_HOST in webui/.env.local (gitignored, copy from
// .env.example) to your device's current glide.local or IP.
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  const deviceHost = env.VITE_DEVICE_HOST || "glide.local";

  return {
    plugins: [preact()],
    // Relative asset paths: this app is served from the ESP32's own
    // root once embedded in firmware, not from a build-tool-assumed
    // absolute path.
    base: "./",
    build: {
      outDir: "dist",
      // Firmware embeds every built file as its own byte array (see
      // firmware's extra_scripts embed step) -- one bundle per type
      // keeps that simple and avoids dozens of tiny chunk files each
      // needing their own PROGMEM array.
      cssCodeSplit: false,
      rollupOptions: {
        output: {
          manualChunks: undefined,
        },
      },
    },
    server: {
      proxy: {
        "/api": {
          target: `http://${deviceHost}`,
          changeOrigin: true,
        },
        "/ws": {
          target: `ws://${deviceHost}`,
          ws: true,
        },
      },
    },
  };
});
