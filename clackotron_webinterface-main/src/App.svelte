<script>
  import { onMount } from "svelte";
  import {
    boardRows,
    boardTrackGroups,
    boardLayout,
    boardLoaded,
    loadBoardData,
    saveBoardPositions,
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
  $: boardVisualization = ($boardLayout || []).map((row) => {
    const cells = [];
    let pendingFillers = 0;

    for (const raw of row) {
      const token = String(raw);
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

  .save-line button:hover,
  .debug-block button:hover,
  .mirror-block button:hover {
    opacity: 0.8;
  }
</style>
