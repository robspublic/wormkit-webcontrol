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
  team: number | null; // claimed team number, or null
  is_admin: boolean;
}

export interface TurnState {
  turn_team: string | null;
  pos_x: number | null;
  pos_y: number | null;
  weapon: number | null;
  round_active: boolean;
}

// Monitor snapshot (mirrors the backend GameSnapshot / TeamInfo / WormInfo).
export interface WormInfo {
  team: number;
  worm: number;
  active: boolean;
  pos_x: number;
  pos_y: number;
  weapon: number;
  facing: number;
}

export interface TeamInfo {
  team_number: number;
  owner: number;
  current_worm: number;
  is_turn_holder: boolean;
  is_local: boolean;
  worms: WormInfo[];
}

export interface GameSnapshot {
  round_active: boolean;
  before_round_start: boolean;
  num_teams: number;
  current_machine: number;
  turn_team: number | null;
  turn_time_ms: number | null;
  teams: TeamInfo[];
}

// team number (as string, since JSON object keys are strings) -> email
export type Mappings = Record<string, string>;

async function jsonOrThrow<T>(res: Response): Promise<T> {
  if (!res.ok) {
    let detail = "";
    try {
      const body = await res.json();
      detail = body.detail ?? JSON.stringify(body);
    } catch {
      detail = await res.text().catch(() => "");
    }
    throw new Error(`${res.status}: ${detail}`);
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

  async monitor(): Promise<GameSnapshot> {
    return jsonOrThrow(await fetch("/api/monitor"));
  },

  async claim(): Promise<{ team: number; email: string }> {
    return jsonOrThrow(await fetch("/api/claim", { method: "POST" }));
  },

  async listMappings(): Promise<Mappings> {
    return jsonOrThrow(await fetch("/api/admin/mappings"));
  },

  async clearMappings(): Promise<void> {
    await jsonOrThrow(await fetch("/api/admin/clear-mappings", { method: "POST" }));
  },

  async deleteMapping(teamNumber: number): Promise<void> {
    const res = await fetch(`/api/admin/mappings/${teamNumber}`, { method: "DELETE" });
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
