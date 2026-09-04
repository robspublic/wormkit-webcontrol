import { useEffect } from "react";

// Keep the device screen awake while the app is open. Uses the Screen Wake Lock
// API (HTTPS-only), which is exactly right for a phone/laptop used as a game
// controller. Browsers release the lock when the tab is hidden, so we re-acquire
// it when the page becomes visible again. No-ops gracefully where unsupported.
export function useWakeLock(): void {
  useEffect(() => {
    // WakeLockSentinel isn't in all TS lib versions; keep it loose.
    let sentinel: { release: () => Promise<void> } | null = null;
    let released = false;

    const request = async () => {
      const nav = navigator as Navigator & {
        wakeLock?: { request: (type: "screen") => Promise<{ release: () => Promise<void> }> };
      };
      if (!nav.wakeLock) return;
      try {
        sentinel = await nav.wakeLock.request("screen");
      } catch {
        // Denied (e.g. not visible / low battery) — ignore; we retry on
        // visibility change.
      }
    };

    const onVisibility = () => {
      if (document.visibilityState === "visible" && !released) {
        void request();
      }
    };

    void request();
    document.addEventListener("visibilitychange", onVisibility);

    return () => {
      released = true;
      document.removeEventListener("visibilitychange", onVisibility);
      void sentinel?.release().catch(() => {});
    };
  }, []);
}
