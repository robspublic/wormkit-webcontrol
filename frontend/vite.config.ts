import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// During dev, proxy API + WebSocket calls to the FastAPI backend so the browser
// talks to a single origin (and the X-Auth-Email header flow matches prod,
// where nginx/oauth-proxy fronts both the static app and the API).
//
// The backend port defaults to 8000 but can be overridden with the
// WKWC_BACKEND_PORT env var (scripts/run-dev.sh sets this so the proxy always
// matches the port the backend is actually launched on).
const backendPort = process.env.WKWC_BACKEND_PORT ?? "8000";

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      "/api": {
        target: `http://localhost:${backendPort}`,
        changeOrigin: true,
      },
      "/ws": {
        target: `ws://localhost:${backendPort}`,
        ws: true,
      },
    },
  },
});
