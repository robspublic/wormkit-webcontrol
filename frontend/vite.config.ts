import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// During dev, proxy API + WebSocket calls to the FastAPI backend so the browser
// talks to a single origin (and the X-Auth-Email header flow matches prod,
// where nginx/oauth-proxy fronts both the static app and the API).
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      "/api": {
        target: "http://localhost:8000",
        changeOrigin: true,
      },
      "/ws": {
        target: "ws://localhost:8000",
        ws: true,
      },
    },
  },
});
