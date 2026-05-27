import { writable, derived } from 'svelte/store';

/**
 * @typedef {{ label?: string }} BoardPosition
 * @typedef {{
 *   address: number | string,
 *   track?: string,
 *   label?: string,
 *   length?: number | string,
 *   positions?: BoardPosition[],
 *   defaultPosition?: number
 * }} BoardModule
 * @typedef {BoardModule & {
 *   track: string,
 *   length: number,
 *   selectedPosition: number
 * }} BoardRow
 * @typedef {{ track: string, rows: BoardRow[] }} BoardTrackGroup
 * @typedef {{
 *   enabled: boolean,
 *   stationQuery: string,
 *   stationId: string,
 *   platform: string,
 *   refreshIntervalSeconds: number,
 *   mappings: Record<string, string>,
 *   departureWindowEnabled: boolean,
 *   departureWindowMinutes: number
 * }} MirrorConfig
 * @typedef {{
 *   modulePositions?: Record<string, number>,
 *   mode?: string,
 *   randomIntervalSeconds?: number
 * }} BoardConfig
 * @typedef {{ mode?: string, randomIntervalSeconds?: number }} SaveBoardOptions
 */

/** @type {import('svelte/store').Writable<BoardModule[]>} */
export const boardDefs = writable(/** @type {BoardModule[]} */ ([]));
/** @type {import('svelte/store').Writable<Record<string, number>>} */
export const boardPositions = writable(/** @type {Record<string, number>} */ ({}));
/** @type {import('svelte/store').Writable<Array<Array<number | string>>>} */
export const boardLayout = writable(/** @type {Array<Array<number | string>>} */ ([]));
/** @type {import('svelte/store').Writable<boolean>} */
export const boardLoaded = writable(false);

/** @type {import('svelte/store').Writable<MirrorConfig>} */
export const mirrorConfig = writable({
  enabled: false,
  stationQuery: '',
  stationId: '',
  platform: '',
  refreshIntervalSeconds: 30,
  mappings: {},
  departureWindowEnabled: false,
  departureWindowMinutes: 10,
});

/** @type {import('svelte/store').Readable<BoardRow[]>} */
export const boardRows = derived(
  /** @type {[import('svelte/store').Writable<BoardModule[]>, import('svelte/store').Writable<Record<string, number>>]} */ ([
    boardDefs,
    boardPositions,
  ]),
  ([$boardDefs, $boardPositions]) => {
    return $boardDefs.map((mod) => ({
      address: mod.address,
      track: mod.track ?? 'Ungrouped',
      label: mod.label,
      length: Math.max(1, Number(mod.length ?? 1) || 1),
      positions: mod.positions,
      defaultPosition: mod.defaultPosition ?? 0,
      selectedPosition: $boardPositions[String(mod.address)] ?? mod.defaultPosition ?? 0,
    }));
  }
);

/** @type {import('svelte/store').Readable<BoardTrackGroup[]>} */
export const boardTrackGroups = derived(boardRows, ($boardRows) => {
  /** @type {Record<string, BoardRow[]>} */
  const grouped = $boardRows.reduce((acc, row) => {
    const key = String(row.track ?? 'Ungrouped');
    if (!acc[key]) acc[key] = [];
    acc[key].push(row);
    return acc;
  }, /** @type {Record<string, BoardRow[]>} */ ({}));

  return Object.keys(grouped)
    .sort((a, b) => a.localeCompare(b))
    .map((track) => ({ track, rows: grouped[track] }));
});

export async function loadBoardData() {
  const [defsResp, configResp, layoutResp] = await Promise.all([
    fetch('/ui/config/board_modules.json'),
    fetch('/config'),
    fetch('/ui/config/modules.json'),
  ]);

  const defs = await defsResp.json();
  /** @type {BoardConfig} */
  const config = await configResp.json();
  const layout = await layoutResp.json();

  boardDefs.set(defs.modules || []);
  boardPositions.set(config.modulePositions || {});
  boardLayout.set(layout.modules || []);
  boardLoaded.set(true);

  return config;
}

/**
 * @param {Record<string, number>} positions
 * @param {SaveBoardOptions} [options]
 */
export async function saveBoardPositions(positions, options = {}) {
  const payloadObj = {
    modulePositions: positions,
    mode: options.mode,
    randomIntervalSeconds: options.randomIntervalSeconds,
  };
  const payload = encodeURIComponent(JSON.stringify(payloadObj));
  const response = await fetch('/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: `payload=${payload}`,
  });
  return response.json();
}

export async function loadMirrorConfig() {
  const response = await fetch('/mirror-config');
  /** @type {MirrorConfig} */
  const data = await response.json();
  mirrorConfig.set(data);
  return data;
}

/**
 * @param {MirrorConfig} config
 */
export async function saveMirrorConfig(config) {
  const payload = encodeURIComponent(JSON.stringify(config));
  const response = await fetch('/mirror-config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: `payload=${payload}`,
  });
  return response.json();
}
