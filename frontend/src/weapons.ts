// Weapon palette data for the Tier-2 visual selector.
//
// The tiles are rendered from a single sprite sheet (assets/weapon-panel.png,
// 180x380) that mirrors WA's in-game weapon panel: 5 columns x 13 rows =
// 65 slots, laid out as Util + F1..F12 (see reference/WA_WeaponPanel_layout.md).
//
// Each slot's weaponId is the WA 3.8.1 weapon id from the reference
// Constants::Weapon enum (Weapon_None=0, Weapon_Bazooka=1, ... sequential).
// Ids are assigned BY NAME (not grid position) because the panel layout and the
// enum ordering don't line up 1:1 (the enum has a few weapons the panel doesn't
// surface, e.g. AquaSheep). Keeping an explicit name->id table avoids off-by-one
// drift.
//
// NOTE (in-game unknown): whether SelectWeapon wants this enum id or a 0-based
// panel index is confirmed in-game. If selection is off by one, the DLL side is
// where to adjust (see ControlHooks); this table stays the source of truth for
// names/positions.

export interface WeaponSlot {
  name: string;
  weaponId: number; // WA Constants::Weapon enum id
  row: number; // 0-based grid row (0 = Util, 1 = F1, ... 12 = F12)
  col: number; // 0-based grid column (0..4)
  mouseOnly: boolean; // needs an in-game map click (target crosshair) to deploy;
  // the web interface can't relay a map coordinate, so these are shown but not
  // selectable — the player must use the PC's mouse for them.
}

// Weapon ids that require clicking a location on the map to deploy (a targeting
// crosshair opens after selection). Our web control only relays move/aim/fire,
// never a map coordinate, so these can't be driven from the browser. They stay
// visible (same coloring + ammo badge) but are marked and non-interactive.
//   Homing weapons: Homing Missile (2), Homing Pigeon (4)
//   Airstrike family: Air Strike (27), Napalm Strike (28), Mail Strike (29),
//     Mine Strike (30), Mole Squadron (31), French Sheep Strike (50),
//     Mike's Carpet Bomb (51)
//   Construction (mouse-placed): Girder (34), Girder Starter Pack (36)
//   Other target-click: Patsy's Magic Bullet (61)
export const MOUSE_ONLY_WEAPON_IDS: ReadonlySet<number> = new Set([
  2, 4, 27, 28, 29, 30, 31, 34, 36, 50, 51, 61,
]);

// Sprite-sheet geometry (weapon-panel.png, cropped to drop the row-label
// column). Layout: 1px outer border, 28px tiles, 1px lines between tiles.
//   width  = 1 + 5*28 + 4*1 + 1 = 146
//   height = 1 + 13*28 + 12*1 + 1 = 378
// So tiles are 28x28 on a 29px pitch, with the first tile at offset (1,1).
export const PANEL_COLS = 5;
export const PANEL_ROWS = 13;
export const PANEL_WIDTH = 146;
export const PANEL_HEIGHT = 378;
export const TILE_SIZE = 28; // px, actual weapon image
export const TILE_PITCH = 29; // px, tile + 1px separator line
export const TILE_ORIGIN = 1; // px, top/left border before the first tile

// The 13 rows of the panel, top to bottom, each a list of 5 [name, weaponId].
// weaponId values are the reference Constants::Weapon enum (Bazooka=1 ...).
const ROWS: Array<Array<[string, number]>> = [
  // Util row (utility weapons live at the end of the enum, ids 62-66)
  [
    ["Jet Pack", 62],
    ["Low Gravity", 63],
    ["Fast Walk", 64],
    ["Laser Sight", 65],
    ["Invisibility", 66],
  ],
  // F1
  [
    ["Bazooka", 1],
    ["Homing Missile", 2],
    ["Mortar", 3],
    ["Homing Pigeon", 4],
    ["Sheep Launcher", 5],
  ],
  // F2
  [
    ["Grenade", 6],
    ["Cluster Bomb", 7],
    ["Banana Bomb", 8],
    ["Battle Axe", 9],
    ["Earthquake", 10],
  ],
  // F3
  [
    ["Shotgun", 11],
    ["Handgun", 12],
    ["Uzi", 13],
    ["Minigun", 14],
    ["Longbow", 15],
  ],
  // F4
  [
    ["Fire Punch", 16],
    ["Dragon Ball", 17],
    ["Kamikaze", 18],
    ["Suicide Bomber", 19],
    ["Prod", 20],
  ],
  // F5
  [
    ["Dynamite", 21],
    ["Mine", 22],
    ["Sheep", 23],
    ["Super Sheep", 24],
    ["Mole Bomb", 26],
  ],
  // F6
  [
    ["Air Strike", 27],
    ["Napalm Strike", 28],
    ["Mail Strike", 29],
    ["Mine Strike", 30],
    ["Mole Squadron", 31],
  ],
  // F7
  [
    ["Blow Torch", 32],
    ["Pneumatic Drill", 33],
    ["Girder", 34],
    ["Baseball Bat", 35],
    ["Girder Starter Pack", 36],
  ],
  // F8
  [
    ["Ninja Rope", 37],
    ["Bungee", 38],
    ["Parachute", 39],
    ["Teleport", 40],
    ["Scales of Justice", 41],
  ],
  // F9
  [
    ["Super Banana Bomb", 42],
    ["Holy Hand Grenade", 43],
    ["Flame Thrower", 44],
    ["Salvation Army", 45],
    ["MB Bomb", 46],
  ],
  // F10
  [
    ["Petrol Bomb", 47],
    ["Skunk", 48],
    ["Priceless Ming Vase", 49],
    ["French Sheep Strike", 50],
    ["Mike's Carpet Bomb", 51],
  ],
  // F11
  [
    ["Mad Cows", 52],
    ["Old Woman", 53],
    ["Concrete Donkey", 54],
    ["Indian Nuclear Test", 55],
    ["Armageddon", 56],
  ],
  // F12
  [
    ["Skip Go", 57],
    ["Surrender", 58],
    ["Select Worm", 59],
    ["Freeze", 60],
    ["Patsy's Magic Bullet", 61],
  ],
];

export const WEAPON_SLOTS: WeaponSlot[] = ROWS.flatMap((cells, row) =>
  cells.map(([name, weaponId], col) => ({
    name,
    weaponId,
    row,
    col,
    mouseOnly: MOUSE_ONLY_WEAPON_IDS.has(weaponId),
  })),
);
