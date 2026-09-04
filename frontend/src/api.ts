// Typed client for the wkWebControl backend.
//
// In production nginx/oauth-proxy fronts both the static app and the API, so
// the browser never sends X-Auth-Email itself; the proxy injects it. In dev,
// Vite proxies /api and /ws to the FastAPI server.

export type ControlAction =
  | "move_left"
  | "move_right"
  | "aim_up"
  | "aim_down"
  | "select_weapon"
  | "fire";

export interface Me {
  email: string;
  teams: string[];
}

export interface TurnState {
  turn_team: string | null;
  pos_x: number | null;
  pos_y: number | null;
  weapon: number | null;
  round_active: boolean;
}

export type Mappings = Record<string, string>;

async function jsonOrThrow<T>(res: Response): Promise<T> {
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`${res.status} ${res.statusText}: ${text}`);
  }
  return (await res.json()) as T;
}

export const api = {
  async me(): Promise<Me> {
    return jsonOrThrow(await fetch("/api/me"));
  },

  async turn(): Promise<TurnState> {
    return jsonOrThrow(await fetch("/api/turn"));
  },

  async listMappings(): Promise<Mappings> {
    return jsonOrThrow(await fetch("/api/admin/mappings"));
  },

  async setMapping(team: string, email: string): Promise<void> {
    await jsonOrThrow(
      await fetch(`/api/admin/mappings/${encodeURIComponent(team)}`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ email }),
      }),
    );
  },

  async deleteMapping(team: string): Promise<void> {
    const res = await fetch(`/api/admin/mappings/${encodeURIComponent(team)}`, {
      method: "DELETE",
    });
    if (!res.ok && res.status !== 204) {
      throw new Error(`${res.status} ${res.statusText}`);
    }
  },
};

/** Open the control WebSocket. Caller owns the socket lifecycle. */
export function openControlSocket(): WebSocket {
  const proto = window.location.protocol === "https:" ? "wss" : "ws";
  return new WebSocket(`${proto}://${window.location.host}/ws/control`);
}
