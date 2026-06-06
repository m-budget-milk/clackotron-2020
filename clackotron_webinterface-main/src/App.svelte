<script>
  import { onMount } from "svelte";
  import {
    boardRows,
    boardTrackGroups,
    boardLayout,
    boardLoaded,
    loadBoardData,
    saveBoardPositions,
    saveModuleLayout,
    saveBoardModules,
    mirrorConfig,
    loadMirrorConfig,
    saveMirrorConfig,
  } from "./store/state";
  import { wiTitle, wiStrings, wiVersion } from "./store/webinterface";

  $: {
    document.title = $wiTitle;
  }

  let activePage = "config";
  let zeroAddress = "";
  let stepAddress = "";
  let stepResult = null;
  let typeAddress = "";
  let typeResult = null;
  let oldAddrChange = "";
  let newAddrChange = "";
  let addrResult = null;
  let saveStatus = null;
  let moduleLayoutEditor = [];
  let moduleLayoutStatus = null;
  let boardModulesCsvStatus = null;
  let restartStatus = null;
  let selectedPositions = {};
  let boardMode = "manual";
  let randomIntervalSeconds = 60;

  let mirrorStationSearch = "";
  let mirrorSearchResults = [];
  let mirrorSearching = false;
  let mirrorSaveStatus = null;

  $: if ($boardLoaded && Object.keys(selectedPositions).length === 0) {
    selectedPositions = Object.fromEntries(
      $boardRows.map((r) => [String(r.address), r.selectedPosition])
    );
  }

  $: boardRowsByAddress = new Map($boardRows.map((r) => [String(r.address), r]));
  $: knownModuleAddresses = $boardRows
    .map((r) => Number(r.address))
    .filter((addr) => Number.isInteger(addr) && addr >= 1 && addr <= 255)
    .sort((a, b) => a - b);

  const getLayoutCellAddress = (cell) => {
    if (cell && typeof cell === "object") return cell.address;
    return cell;
  };

  const getLayoutCellLength = (cell) => {
    if (cell && typeof cell === "object") return cell.length;
    return undefined;
  };

  $: boardVisualization = ($boardLayout || []).map((row) => {
    const cells = [];
    let pendingFillers = 0;

    for (const raw of row) {
      const token = String(getLayoutCellAddress(raw) ?? "");
      const isFiller = token.toUpperCase() === "X";

      if (pendingFillers > 0 && isFiller) {
        pendingFillers--;
        continue;
      }

      if (pendingFillers > 0 && !isFiller) {
        pendingFillers = 0;
      }

      if (isFiller) {
        cells.push({ addr: "X", text: "", missing: false, filler: true, colspan: 1 });
        continue;
      }

      const moduleRow = boardRowsByAddress.get(token);
      if (!moduleRow) {
        cells.push({ addr: token, text: "-", missing: true, filler: false, colspan: 1 });
        continue;
      }

      const pos = selectedPositions[token] ?? moduleRow.selectedPosition;
      const text = moduleRow.positions?.[pos]?.label ?? "";
      const span = Math.max(1, Number(moduleRow.length ?? 1) || 1);

      cells.push({ addr: token, text, missing: false, filler: false, colspan: span });
      pendingFillers = span - 1;
    }

    return cells;
  });

  const loadWebinterfaceConfig = async () => {
    return fetch("/ui/config/webinterface.json")
      .then((response) => response.json())
      .then((data) => {
        $wiTitle = data.name;
        $wiStrings = data.strings;
        $wiVersion = data.version;
      });
  };

  const saveBoard = async () => {
    saveStatus = null;
    try {
      const result = await saveBoardPositions(selectedPositions, {
        mode: boardMode,
        randomIntervalSeconds,
      });
      saveStatus = result.success ? "ok" : "error";
    } catch (e) {
      saveStatus = "error";
    }
  };

  const syncModuleLayoutEditor = () => {
    moduleLayoutEditor = ($boardLayout || []).map((row) =>
      row.map((cell) => {
        const address = String(getLayoutCellAddress(cell) ?? "");
        const fallbackLength = boardRowsByAddress.get(address)?.length ?? 1;
        return {
          address,
          length: Math.max(1, Number(getLayoutCellLength(cell) ?? fallbackLength) || 1),
        };
      })
    );
  };

  const setModuleLayoutCellAddress = (rowIndex, cellIndex, value) => {
    moduleLayoutEditor = moduleLayoutEditor.map((row, r) =>
      r === rowIndex ? row.map((cell, c) => (c === cellIndex ? { ...cell, address: value } : cell)) : row
    );
  };

  const setModuleLayoutCellLength = (rowIndex, cellIndex, value) => {
    const length = Math.max(1, Number.parseInt(value, 10) || 1);
    moduleLayoutEditor = moduleLayoutEditor.map((row, r) =>
      r === rowIndex ? row.map((cell, c) => (c === cellIndex ? { ...cell, length } : cell)) : row
    );
  };

  const addModuleLayoutRow = () => {
    moduleLayoutEditor = [...moduleLayoutEditor, [{ address: "", length: 1 }]];
  };

  const removeModuleLayoutRow = (rowIndex) => {
    moduleLayoutEditor = moduleLayoutEditor.filter((_, r) => r !== rowIndex);
  };

  const addModuleLayoutCell = (rowIndex) => {
    moduleLayoutEditor = moduleLayoutEditor.map((row, r) =>
      r === rowIndex ? [...row, { address: "", length: 1 }] : row
    );
  };

  const removeModuleLayoutCell = (rowIndex, cellIndex) => {
    moduleLayoutEditor = moduleLayoutEditor
      .map((row, r) => (r === rowIndex ? row.filter((_, c) => c !== cellIndex) : row))
      .filter((row) => row.length > 0);
  };

  const normalizeModuleLayout = () => {
    return moduleLayoutEditor
      .map((row) =>
        row
          .map((cell) => ({
            address: String(cell.address ?? "").trim(),
            length: Math.max(1, Number.parseInt(cell.length, 10) || 1),
          }))
          .filter((cell) => cell.address.length > 0)
          .map((cell) =>
            cell.address.toUpperCase() === "X"
              ? "X"
              : { address: Number.parseInt(cell.address, 10), length: cell.length }
          )
      )
      .filter((row) => row.length > 0);
  };

  const saveModulesJson = async () => {
    const layout = normalizeModuleLayout();
    const validAddresses = new Set(knownModuleAddresses);

    if (layout.length === 0) {
      alert("Please add at least one module address.");
      return;
    }

    for (const row of layout) {
      for (const cell of row) {
        if (cell === "X") continue;
        const address = Number(cell.address);
        const length = Number(cell.length);
        if (!Number.isInteger(address) || address < 1 || address > 255) {
          alert("Module cells must be addresses from 1-255 or X placeholders.");
          return;
        }
        if (!Number.isInteger(length) || length < 1 || length > 16) {
          alert("Module lengths must be from 1-16.");
          return;
        }
        if (validAddresses.size > 0 && !validAddresses.has(address)) {
          alert(`Address ${address} is not defined in board_modules.json.`);
          return;
        }
      }
    }

    moduleLayoutStatus = null;
    try {
      const result = await saveModuleLayout(layout);
      moduleLayoutStatus = result.success ? "ok" : "error";
      if (result.success) {
        await loadBoardData();
        syncModuleLayoutEditor();
      }
    } catch (e) {
      moduleLayoutStatus = "error";
    }
  };

  const csvEscape = (value) => {
    const text = String(value ?? "");
    return /[;"\n\r]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
  };

  const downloadBoardModulesCsv = () => {
    const maxPositions = Math.max(1, ...$boardRows.map((row) => (row.positions || []).length));
    const positionHeaders = Array.from({ length: maxPositions }, (_, i) => `pos${i}`);
    const header = ["address", "track", "label", "defaultPosition", ...positionHeaders];
    const rows = $boardRows.map((row) => [
      row.address,
      row.track,
      row.label ?? "",
      row.defaultPosition ?? 0,
      ...positionHeaders.map((_, i) => row.positions?.[i]?.label ?? ""),
    ]);
    const csv = [header, ...rows].map((row) => row.map(csvEscape).join(";")).join("\n") + "\n";
    const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "board_modules.csv";
    link.click();
    URL.revokeObjectURL(url);
  };

  const parseCsv = (text) => {
    const delimiter = (text.match(/;/g) || []).length >= (text.match(/,/g) || []).length ? ";" : ",";
    const rows = [];
    let row = [];
    let cell = "";
    let quoted = false;

    for (let i = 0; i < text.length; i++) {
      const char = text[i];
      const next = text[i + 1];

      if (quoted) {
        if (char === '"' && next === '"') {
          cell += '"';
          i++;
        } else if (char === '"') {
          quoted = false;
        } else {
          cell += char;
        }
        continue;
      }

      if (char === '"') {
        quoted = true;
      } else if (char === delimiter) {
        row.push(cell);
        cell = "";
      } else if (char === "\n") {
        row.push(cell);
        rows.push(row);
        row = [];
        cell = "";
      } else if (char !== "\r") {
        cell += char;
      }
    }

    row.push(cell);
    if (row.some((value) => value.trim().length > 0)) rows.push(row);
    return rows;
  };

  const importBoardModulesCsv = async (event) => {
    const file = event.target.files?.[0];
    event.target.value = "";
    if (!file) return;

    boardModulesCsvStatus = null;
    try {
      const rows = parseCsv(await file.text()).filter((row) => row.some((cell) => cell.trim()));
      if (rows.length < 2) {
        alert("CSV must contain a header row and at least one module row.");
        return;
      }

      const headers = rows[0].map((header) => header.trim().toLowerCase());
      const indexOf = (...names) => names.map((name) => headers.indexOf(name)).find((idx) => idx >= 0) ?? -1;
      const addressIdx = indexOf("address", "addr");
      const trackIdx = indexOf("track");
      const labelIdx = indexOf("label", "name");
      const defaultIdx = indexOf("defaultposition", "default_position", "default");
      const positionsIdx = indexOf("positions", "positionlabels", "position_labels");
      const positionIndexes = headers
        .map((header, index) => ({ header, index }))
        .filter(({ header }) => /^pos(ition)?\d+$/.test(header))
        .sort((a, b) => Number(a.header.match(/\d+/)?.[0] ?? 0) - Number(b.header.match(/\d+/)?.[0] ?? 0))
        .map(({ index }) => index);

      if (addressIdx < 0 || (positionsIdx < 0 && positionIndexes.length === 0)) {
        alert("CSV needs an address column and pos0/pos1/... columns.");
        return;
      }

      const modules = rows.slice(1).map((row) => {
        const address = Number.parseInt(row[addressIdx], 10);
        const defaultPosition = defaultIdx >= 0 ? Number.parseInt(row[defaultIdx], 10) || 0 : 0;
        const positions =
          positionIndexes.length > 0
            ? positionIndexes.map((idx) => ({ label: String(row[idx] ?? "") }))
            : String(row[positionsIdx] ?? "")
                .split("|")
                .map((label) => ({ label }));

        return {
          address,
          track: trackIdx >= 0 ? String(row[trackIdx] ?? "") : "Ungrouped",
          label: labelIdx >= 0 ? String(row[labelIdx] ?? "") : "",
          defaultPosition,
          positions,
        };
      });

      for (const mod of modules) {
        if (!Number.isInteger(mod.address) || mod.address < 1 || mod.address > 255) {
          alert("Every CSV row needs an address from 1-255.");
          return;
        }
        if (!Array.isArray(mod.positions) || mod.positions.length === 0) {
          alert(`Module ${mod.address} needs at least one position label.`);
          return;
        }
        if (mod.defaultPosition < 0 || mod.defaultPosition >= mod.positions.length) {
          alert(`Default position for module ${mod.address} is outside its position list.`);
          return;
        }
      }

      const result = await saveBoardModules(modules);
      boardModulesCsvStatus = result.success ? "ok" : "error";
      if (result.success) {
        await loadBoardData();
        syncModuleLayoutEditor();
      }
    } catch (e) {
      console.error("Failed to import board modules CSV:", e);
      boardModulesCsvStatus = "error";
    }
  };

  const restartBoard = async () => {
    const shouldRestart = window.confirm("Restart the board now?");
    if (!shouldRestart) return;

    restartStatus = null;
    try {
      const response = await fetch("/restart", { method: "POST" });
      if (!response.ok) {
        restartStatus = "error";
        return;
      }
      restartStatus = "ok";
    } catch (e) {
      restartStatus = "error";
    }
  };

  const zeroAll = async () => {
    try {
      const response = await fetch("/zero");
      if (!response.ok) {
        alert("Failed to zero modules");
        return;
      }
      await response.text();
      alert("All modules zeroed successfully!");
    } catch (error) {
      console.error("Error zeroing modules:", error);
      alert("Error zeroing modules");
    }
  };

  const zeroModule = async () => {
    const addr = Number.parseInt(zeroAddress, 10);
    if (Number.isNaN(addr) || addr < 0 || addr > 255) {
      alert("Please enter a valid module address (0-255)");
      return;
    }
    try {
      const response = await fetch(`/zero?addr=${addr}`);
      if (!response.ok) {
        alert("Failed to zero module");
        return;
      }
      await response.text();
      alert(`Zeroed module ${addr}`);
    } catch (error) {
      console.error("Error zeroing module:", error);
      alert("Error zeroing module");
    }
  };

  const stepModule = async () => {
    const addr = Number.parseInt(stepAddress, 10);
    if (Number.isNaN(addr) || addr < 0 || addr > 255) {
      alert("Please enter a valid module address (0-255)");
      return;
    }
    stepResult = null;
    try {
      const response = await fetch(`/step?addr=${addr}`);
      if (!response.ok) {
        alert("Failed to step module");
        return;
      }
      await response.text();
      stepResult = { addr };
    } catch (error) {
      console.error("Error stepping module:", error);
      alert("Error stepping module");
    }
  };

  const getModuleType = async () => {
    const addr = Number.parseInt(typeAddress, 10);
    if (Number.isNaN(addr) || addr < 0 || addr > 255) {
      alert("Please enter a valid module address (0-255)");
      return;
    }

    typeResult = null;

    try {
      const response = await fetch(`/type?addr=${addr}`);
      if (!response.ok) {
        const errText = await response.text();
        console.error("Failed to read module type:", errText);
        alert("Failed to read module type");
        return;
      }

      const data = await response.json();
      if (!data.success) {
        alert("Failed to read module type");
        return;
      }

      typeResult = data;
    } catch (error) {
      console.error("Error reading module type:", error);
      alert("Error reading module type");
    }
  };

  const changeModuleAddr = async () => {
    const oldAddr = Number.parseInt(oldAddrChange, 10);
    const newAddr = Number.parseInt(newAddrChange, 10);
    if (Number.isNaN(oldAddr) || oldAddr < 1 || oldAddr > 255) {
      alert("Please enter a valid old address (1-255)");
      return;
    }
    if (Number.isNaN(newAddr) || newAddr < 1 || newAddr > 255) {
      alert("Please enter a valid new address (1-255)");
      return;
    }

    addrResult = null;

    try {
      const response = await fetch(`/addr?oldAddr=${oldAddr}&newAddr=${newAddr}`);
      if (!response.ok) {
        const errText = await response.text();
        console.error("Failed to change module address:", errText);
        alert("Failed to change module address");
        return;
      }

      const data = await response.json();
      if (!data.success) {
        alert("Failed to change module address");
        return;
      }

      addrResult = data;
      alert(`Successfully changed module address from ${oldAddr} to ${newAddr}`);
      oldAddrChange = "";
      newAddrChange = "";
    } catch (error) {
      console.error("Error changing module address:", error);
      alert("Error changing module address");
    }
  };

  const MIRROR_FIELDS = [
    { value: "none", label: "- no mapping -" },
    { value: "destination", label: "Destination" },
    { value: "departure_hour", label: "Departure Hour" },
    { value: "departure_minute", label: "Departure Minute" },
    { value: "category", label: "Category / Service Type" },
    { value: "number", label: "Train Number" },
    { value: "delay", label: "Delay" },
    { value: "platform", label: "Platform" },
    { value: "next_stations", label: "Next Stations" },
  ];

  const searchStations = async () => {
    if (mirrorStationSearch.length < 2) return;
    mirrorSearching = true;
    mirrorSearchResults = [];
    try {
      const resp = await fetch(`/api/station-search?query=${encodeURIComponent(mirrorStationSearch)}`);
      const data = await resp.json();
      mirrorSearchResults = data.stations || [];
    } catch (e) {
      mirrorSearchResults = [];
    } finally {
      mirrorSearching = false;
    }
  };

  const selectStation = (station) => {
    $mirrorConfig = { ...$mirrorConfig, stationQuery: station.name, stationId: station.id };
    mirrorSearchResults = [];
    mirrorStationSearch = "";
  };

  const setMirrorMapping = (addr, field) => {
    const mappings = { ...$mirrorConfig.mappings };
    if (field === "none") {
      delete mappings[String(addr)];
    } else {
      mappings[String(addr)] = field;
    }
    $mirrorConfig = { ...$mirrorConfig, mappings };
  };

  const saveMirror = async () => {
    mirrorSaveStatus = null;
    try {
      const result = await saveMirrorConfig($mirrorConfig);
      mirrorSaveStatus = result.success ? "ok" : "error";
    } catch (e) {
      mirrorSaveStatus = "error";
    }
  };

  const inferFieldFromLabel = (label) => {
    const l = String(label || "").toLowerCase();
    if (l.includes("destination")) return "destination";
    if (l.includes("departure hour") || l.includes("hour")) return "departure_hour";
    if (l.includes("departure minute") || l.includes("minute")) return "departure_minute";
    if (l.includes("service type") || l.includes("category")) return "category";
    if (l.includes("train") && l.includes("number")) return "number";
    if (l.includes("delay")) return "delay";
    if (l.includes("platform") || l.includes("track")) return "platform";
    if (l.includes("next station") || l.includes("next stations") || l.includes("calling at") || l.includes("via")) return "next_stations";
    return "none";
  };

  const buildDefaultMappings = (rows) => {
    const mappings = {};
    for (const row of rows) {
      const field = inferFieldFromLabel(row.label);
      if (field !== "none") {
        mappings[String(row.address)] = field;
      }
    }
    return mappings;
  };

  onMount(async () => {
    await loadWebinterfaceConfig();
    const boardConfig = await loadBoardData();
    syncModuleLayoutEditor();
    boardMode = String(boardConfig?.mode || "manual").toLowerCase() === "random" ? "random" : "manual";
    randomIntervalSeconds = Number(boardConfig?.randomIntervalSeconds || 60);
    if (!Number.isFinite(randomIntervalSeconds) || randomIntervalSeconds < 1) randomIntervalSeconds = 1;
    if (randomIntervalSeconds > 86400) randomIntervalSeconds = 86400;

    const mirror = await loadMirrorConfig();

    const hasMappings = mirror?.mappings && Object.keys(mirror.mappings).length > 0;
    if (!hasMappings && Array.isArray($boardRows) && $boardRows.length > 0) {
      const autoMappings = buildDefaultMappings($boardRows);
      if (Object.keys(autoMappings).length > 0) {
        $mirrorConfig = { ...$mirrorConfig, mappings: autoMappings };
      }
    }
  });
</script>

<main>
  <header>
    <h1 id="title">{$wiTitle}</h1>
  </header>

  <div class="content">
    <div class="page-tabs">
      <button class:active={activePage === "config"} on:click={() => (activePage = "config")}>
        Configuration
      </button>
      <button class:active={activePage === "mirror"} on:click={() => (activePage = "mirror")}>
        Mirror
      </button>
      <button class:active={activePage === "debug"} on:click={() => (activePage = "debug")}>
        Debug
      </button>
    </div>

    {#if activePage === "config"}
      {#if !$boardLoaded}
        <div class="board-loading">Loading board configuration...</div>
      {:else}
        <h2 class="section-title">Board Editor</h2>
        <h3 class="track-heading">Board Visualization</h3>
        <div class="board-visual-wrap">
          <div class="board-visual">
            {#each boardVisualization as row}
              <div class="board-visual-row">
                {#each row as cell}
                  <div
                    class="board-visual-cell"
                    class:missing={cell.missing}
                    class:filler={cell.filler}
                    style={`width: ${86 * cell.colspan + 6 * (cell.colspan - 1)}px;`}
                  >
                    <div class="visual-label">{cell.text || " "}</div>
                    <div class="visual-addr">{cell.addr}</div>
                  </div>
                {/each}
              </div>
            {/each}
          </div>
        </div>

        {#each $boardTrackGroups as group}
          <h3 class="track-heading">Track {group.track}</h3>
          <div class="track-section">
            <table class="board-table">
              <thead>
                <tr>
                  <th>Addr</th>
                  <th>Label</th>
                  <th>Selection</th>
                </tr>
              </thead>
              <tbody>
                {#each group.rows as row}
                  <tr>
                    <td>{row.address}</td>
                    <td>{row.label}</td>
                    <td>
                      <select
                        value={selectedPositions[String(row.address)] ?? row.selectedPosition}
                        on:change={(e) => {
                          selectedPositions = {
                            ...selectedPositions,
                            [String(row.address)]: +e.target.value,
                          };
                        }}
                      >
                        {#each row.positions as pos, i}
                          <option value={i}>{i}{pos.label ? ": " + pos.label : ""}</option>
                        {/each}
                      </select>
                    </td>
                  </tr>
                {/each}
              </tbody>
            </table>
          </div>
        {/each}

        <h3 class="track-heading">Display Mode</h3>
        <div class="mirror-row">
          <div class="mirror-label">Mode</div>
          <select bind:value={boardMode}>
            <option value="manual">Manual</option>
            <option value="random">Random</option>
          </select>
        </div>

        {#if boardMode === "random"}
          <div class="mirror-row">
            <div class="mirror-label">Cycle interval (seconds)</div>
            <input type="number" min="1" max="86400" bind:value={randomIntervalSeconds} />
          </div>
        {/if}

        <div class="save-line">
          <button on:click={saveBoard}>Save</button>
          {#if saveStatus === "ok"}
            <span class="save-status ok">Saved!</span>
          {:else if saveStatus === "error"}
            <span class="save-status error">Save failed</span>
          {/if}
        </div>
      {/if}

    {:else if activePage === "mirror"}
      <h2 class="section-title">Stationboard Mirror</h2>
      <div class="block mirror-block">
        <div class="mirror-row">
          <div class="mirror-label">Enable mirror mode</div>
          <input type="checkbox" bind:checked={$mirrorConfig.enabled} />
        </div>

        <div class="mirror-row">
          <div class="mirror-label">Station search</div>
          <div class="mirror-search-wrap">
            <input
              type="text"
              placeholder="Search station name..."
              bind:value={mirrorStationSearch}
              on:keydown={(e) => e.key === "Enter" && searchStations()}
            />
            <button on:click={searchStations} disabled={mirrorSearching}>
              {mirrorSearching ? "..." : "Search"}
            </button>
          </div>
          {#if mirrorSearchResults.length > 0}
            <ul class="station-results">
              {#each mirrorSearchResults as s}
                <li>
                  <button class="station-result-btn" on:click={() => selectStation(s)}>
                    {s.name} <span class="station-id">({s.id})</span>
                  </button>
                </li>
              {/each}
            </ul>
          {/if}
        </div>

        <div class="mirror-row">
          <div class="mirror-label">Station name</div>
          <input type="text" bind:value={$mirrorConfig.stationQuery} placeholder="e.g. Andermatt" />
        </div>

        <div class="mirror-row">
          <div class="mirror-label">Station ID</div>
          <input type="text" bind:value={$mirrorConfig.stationId} placeholder="e.g. 8509070" />
        </div>

        <div class="mirror-row">
          <div class="mirror-label">Platform / Track</div>
          <input type="text" bind:value={$mirrorConfig.platform} placeholder="e.g. 4" />
        </div>

        <div class="mirror-row">
          <div class="mirror-label">Refresh interval (s)</div>
          <input type="number" min="10" max="3600" bind:value={$mirrorConfig.refreshIntervalSeconds} />
        </div>

        <div class="mirror-row">
          <div class="mirror-label">Only show within N minutes</div>
          <input type="checkbox" bind:checked={$mirrorConfig.departureWindowEnabled} />
        </div>

        {#if $mirrorConfig.departureWindowEnabled}
        <div class="mirror-row">
          <div class="mirror-label">Window (minutes)</div>
          <input type="number" min="1" max="1440" bind:value={$mirrorConfig.departureWindowMinutes} />
        </div>
        {/if}

        <h3 class="track-heading">Module Field Mapping</h3>
        <table class="board-table">
          <thead>
            <tr>
              <th>Addr</th>
              <th>Module Label</th>
              <th>API Field</th>
            </tr>
          </thead>
          <tbody>
            {#each $boardRows as row}
              <tr>
                <td>{row.address}</td>
                <td>{row.label || "-"}</td>
                <td>
                  <select
                    value={$mirrorConfig.mappings[String(row.address)] ?? "none"}
                    on:change={(e) => setMirrorMapping(row.address, e.target.value)}
                  >
                    {#each MIRROR_FIELDS as f}
                      <option value={f.value}>{f.label}</option>
                    {/each}
                  </select>
                </td>
              </tr>
            {/each}
          </tbody>
        </table>

        <div class="save-line">
          <button on:click={saveMirror}>Save Mirror Config</button>
          {#if mirrorSaveStatus === "ok"}
            <span class="save-status ok">Saved!</span>
          {:else if mirrorSaveStatus === "error"}
            <span class="save-status error">Save failed</span>
          {/if}
        </div>
      </div>

    {:else}
      <h2 class="section-title">Debug Functions</h2>
      <div class="block debug-block">
        <p class="debug-intro">Use these actions for diagnostics and maintenance.</p>

        <div class="debug-option">
          <h3>Setup modules.json</h3>
          <p>Configure the physical module layout used by the board preview and module address list.</p>

          <div class="module-layout-editor">
            {#each moduleLayoutEditor as row, rowIndex}
              <div class="module-layout-row">
                <span class="module-layout-row-label">Row {rowIndex + 1}</span>
                <div class="module-layout-cells">
                  {#each row as cell, cellIndex}
                    <div class="module-layout-cell">
                      <input
                        type="text"
                        value={cell.address}
                        placeholder="Addr or X"
                        on:input={(e) => setModuleLayoutCellAddress(rowIndex, cellIndex, e.target.value)}
                      />
                      {#if String(cell.address ?? "").trim().toUpperCase() !== "X"}
                        <input
                          class="module-layout-length"
                          type="number"
                          min="1"
                          max="16"
                          value={cell.length}
                          aria-label="Module length"
                          on:input={(e) => setModuleLayoutCellLength(rowIndex, cellIndex, e.target.value)}
                        />
                      {/if}
                      <button
                        class="small-button"
                        type="button"
                        on:click={() => removeModuleLayoutCell(rowIndex, cellIndex)}
                        aria-label="Remove module cell"
                      >
                        -
                      </button>
                    </div>
                  {/each}
                  <button class="small-button" type="button" on:click={() => addModuleLayoutCell(rowIndex)}>+</button>
                </div>
                <button class="small-button" type="button" on:click={() => removeModuleLayoutRow(rowIndex)}>
                  Remove Row
                </button>
              </div>
            {/each}
          </div>

          <div class="debug-controls">
            <button type="button" on:click={addModuleLayoutRow}>Add Row</button>
            <button type="button" on:click={saveModulesJson}>Save modules.json</button>
          </div>
          {#if moduleLayoutStatus === "ok"}
            <p class="debug-result">modules.json saved. Restart the device to reload the module address list.</p>
          {:else if moduleLayoutStatus === "error"}
            <p class="debug-result error">Failed to save modules.json</p>
          {/if}
        </div>

        <div class="debug-option">
          <h3>Setup board_modules.json</h3>
          <p>Edit module labels and flap positions with a CSV file. Position columns are named pos0, pos1, pos2, ...</p>
          <div class="debug-controls">
            <button type="button" on:click={downloadBoardModulesCsv}>Download CSV</button>
            <label class="file-button">
              Upload CSV
              <input type="file" accept=".csv,text/csv" on:change={importBoardModulesCsv} />
            </label>
          </div>
          {#if boardModulesCsvStatus === "ok"}
            <p class="debug-result">board_modules.json saved. Restart the board after changing active modules.</p>
          {:else if boardModulesCsvStatus === "error"}
            <p class="debug-result error">Failed to import board_modules.csv</p>
          {/if}
        </div>

        <div class="debug-option">
          <h3>Restart Board</h3>
          <p>Restarts the controller so updated configuration files are loaded from LittleFS.</p>
          <div class="debug-controls">
            <button type="button" class="restart-button" on:click={restartBoard}>Restart Board</button>
          </div>
          {#if restartStatus === "ok"}
            <p class="debug-result">Restarting board. Reload this page after the device reconnects.</p>
          {:else if restartStatus === "error"}
            <p class="debug-result error">Failed to restart board</p>
          {/if}
        </div>

        <div class="debug-option">
          <h3>Zero All Modules</h3>
          <p>
            Sends the ZERO command to all configured modules and moves each one to its hardware zero position.
          </p>
          <button on:click={zeroAll} class="zero-button">Run Zero All</button>
        </div>

        <div class="debug-option">
          <h3>Zero One Module</h3>
          <p>Sends the ZERO command to a single module address.</p>
          <div class="debug-controls">
            <input type="number" min="0" max="255" bind:value={zeroAddress} placeholder="Module address" />
            <button on:click={zeroModule} class="zero-button">Run Zero</button>
          </div>
        </div>

        <div class="debug-option">
          <h3>Step One Module</h3>
          <p>Sends the STEP command to one module address and advances the flap by one blade.</p>
          <div class="debug-controls">
            <input type="number" min="0" max="255" bind:value={stepAddress} placeholder="Module address" />
            <button on:click={stepModule}>Run Step</button>
          </div>
          {#if stepResult}
            <p class="debug-result">Stepped module {stepResult.addr}</p>
          {/if}
        </div>

        <div class="debug-option">
          <h3>Read Module Type</h3>
          <p>Sends the TYPE read command to one module and shows the returned module type in this page.</p>
          <div class="debug-controls">
            <input type="number" min="0" max="255" bind:value={typeAddress} placeholder="Module address" />
            <button on:click={getModuleType}>Read Type</button>
          </div>
          {#if typeResult}
            <p class="debug-result">Module {typeResult.addr}: {typeResult.typeName} ({typeResult.typeHex})</p>
          {/if}
        </div>

        <div class="debug-option">
          <h3>Change Module Address</h3>
          <p>Changes the RS485 address of a module. Useful when reconfiguring the board or replacing modules.</p>
          <div class="debug-controls">
            <input type="number" min="1" max="255" bind:value={oldAddrChange} placeholder="Old address" />
            <input type="number" min="1" max="255" bind:value={newAddrChange} placeholder="New address" />
            <button on:click={changeModuleAddr}>Change Address</button>
          </div>
          {#if addrResult}
            <p class="debug-result">Changed module address from {addrResult.oldAddr} to {addrResult.newAddr}</p>
          {/if}
        </div>
      </div>
    {/if}
  </div>

  <footer>{$wiStrings["copyright"]} &bull; {$wiVersion}</footer>
</main>

<style>
  .page-tabs {
    display: flex;
    background: #d6d6d6;
    border-bottom: 1px solid #bdbdbd;
  }

  .page-tabs button {
    flex: 1;
    border: 0;
    background: transparent;
    color: #A20013;
    padding: 10px 12px;
    cursor: pointer;
    font-weight: 600;
  }

  .page-tabs button.active {
    background: #ffffff;
    color: #A20013;
  }

  .save-line {
    height: 48px;
    background: #e5e5e5;
    text-align: left;
  }

  .save-line button,
  .debug-block button,
  .mirror-block button {
    margin: 7px 10px;
    padding: 10px 20px;
    border: 0;
    border-radius: 5px;
    box-shadow: none;
    background: #A20013;
    color: #ffffff;
    cursor: pointer;
  }

  .board-loading {
    padding: 20px;
    color: #4a4a4a;
  }

  .board-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.9rem;
  }

  .board-table th {
    background: #A20013;
    color: #fff;
    padding: 8px 10px;
    text-align: left;
  }

  .board-table td {
    padding: 7px 10px;
    border-bottom: 1px solid #ddd;
  }

  .board-table tr:nth-child(even) td {
    background: #f5f5f5;
  }

  .board-table select {
    padding: 5px 8px;
    border: 1px solid #bcbcbc;
    border-radius: 4px;
    font-size: 0.9rem;
    width: 100%;
    min-width: 160px;
  }

  .track-heading {
    margin: 18px 10px 8px 10px;
    color: #A20013;
    font-size: 1.05rem;
    border-bottom: 2px solid #e2b8bd;
    padding-bottom: 4px;
  }

  .track-section {
    margin-bottom: 14px;
  }

  .board-visual-wrap {
    margin: 0 10px 12px 10px;
    overflow-x: auto;
  }

  .board-visual {
    display: grid;
    gap: 6px;
  }

  .board-visual-row {
    display: flex;
    gap: 6px;
    width: max-content;
  }

  .board-visual-cell {
    box-sizing: border-box;
    width: 86px;
    flex: 0 0 auto;
    height: 56px;
    background: #fff4f5;
    border: 1px solid #e2b8bd;
    border-radius: 6px;
    text-align: center;
    padding: 4px;
  }

  .board-visual-cell.missing {
    background: #f8f8f8;
    border-color: #e0e0e0;
    color: #999;
  }

  .board-visual-cell.filler {
    background: #fafafa;
    border-style: dashed;
  }

  .visual-label {
    font-weight: 700;
    color: #A20013;
    line-height: 1.1;
    min-height: 16px;
  }

  .visual-addr {
    margin-top: 4px;
    font-size: 0.75rem;
    color: #666;
  }

  .save-status {
    margin-left: 10px;
    font-weight: 600;
  }

  .save-status.ok {
    color: #2e7d32;
  }

  .save-status.error {
    color: #c62828;
  }

  .debug-block,
  .mirror-block {
    min-height: 120px;
  }

  .debug-intro {
    margin: 0 0 8px 10px;
    color: #4a4a4a;
  }

  .debug-option {
    margin: 12px 10px;
    padding: 12px;
    background: #f2f2f2;
    border: 1px solid #d8d8d8;
    border-radius: 6px;
  }

  .debug-option h3 {
    margin: 0 0 6px 0;
    color: #A20013;
    font-size: 1rem;
  }

  .debug-option p {
    margin: 0;
    color: #4a4a4a;
    line-height: 1.4;
  }

  .debug-controls {
    margin-top: 10px;
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
  }

  .debug-controls input {
    width: 160px;
    padding: 9px 10px;
    border: 1px solid #bcbcbc;
    border-radius: 5px;
    font-size: 0.95rem;
  }

  .debug-result {
    margin-top: 10px;
    padding: 8px 10px;
    background: #fff0f2;
    border: 1px solid #e2b8bd;
    border-radius: 5px;
    color: #A20013;
    font-weight: 600;
  }

  .debug-result.error {
    color: #c62828;
  }

  .module-layout-editor {
    margin-top: 12px;
    display: grid;
    gap: 8px;
  }

  .module-layout-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
    padding: 8px;
    background: #ffffff;
    border: 1px solid #dddddd;
    border-radius: 5px;
  }

  .module-layout-row-label {
    min-width: 54px;
    color: #A20013;
    font-weight: 600;
  }

  .module-layout-cells {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    align-items: center;
  }

  .module-layout-cell {
    display: flex;
    gap: 4px;
    align-items: center;
  }

  .module-layout-cell input {
    width: 82px;
    padding: 8px 9px;
    border: 1px solid #bcbcbc;
    border-radius: 5px;
    font-size: 0.9rem;
  }

  .module-layout-cell input.module-layout-length {
    width: 54px;
  }

  .debug-block button.small-button {
    margin: 0;
    padding: 7px 10px;
    min-width: 34px;
  }

  .file-button {
    margin: 7px 10px;
    padding: 10px 20px;
    border: 0;
    border-radius: 5px;
    background: #A20013;
    color: #ffffff;
    cursor: pointer;
  }

  .file-button input {
    display: none;
  }

  .mirror-row {
    margin: 10px;
    display: flex;
    gap: 10px;
    align-items: center;
  }

  .mirror-label {
    min-width: 180px;
    color: #A20013;
    font-weight: 600;
  }

  .mirror-row input {
    padding: 8px 10px;
    border: 1px solid #bcbcbc;
    border-radius: 5px;
    min-width: 220px;
  }

  .mirror-row select {
    padding: 8px 10px;
    border: 1px solid #bcbcbc;
    border-radius: 5px;
    min-width: 220px;
    background: #fff;
  }

  .mirror-search-wrap {
    display: flex;
    gap: 8px;
    width: 100%;
  }

  .mirror-search-wrap input {
    flex: 1;
  }

  .station-results {
    list-style: none;
    margin: 6px 0 0 0;
    padding: 0;
  }

  .station-result-btn {
    width: 100%;
    text-align: left;
    margin: 2px 0;
    padding: 8px 10px;
    border: 1px solid #e2b8bd;
    background: #fff4f5;
    color: #A20013;
    border-radius: 4px;
    cursor: pointer;
  }

  .station-id {
    color: #666;
    font-size: 0.85em;
  }

  .debug-block button.zero-button {
    background: #d97627;
  }

  .debug-block button.restart-button {
    background: #5d6874;
  }

  .save-line button:hover,
  .debug-block button:hover,
  .mirror-block button:hover {
    opacity: 0.8;
  }
</style>
