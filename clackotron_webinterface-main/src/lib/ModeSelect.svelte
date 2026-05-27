<script>
  import { currentMode } from "../store/state";
  import { wiModes, wiIsExpert, wiStrings } from "../store/webinterface";
</script>

<div id="mode-select" class="block">
  {#each $wiModes as mode}
    <div class="mode-option">
      <input
        id="mode-{mode.mode}"
        class="mode-button"
        type="radio"
        name="mode"
        bind:group={$currentMode}
        value={mode.mode}
      />
      <label class="mode-label" for="mode-{mode.mode}">
        {mode.title}
      </label>
    </div>
  {/each}

  {#if $wiIsExpert}
    <div class="mode-option">
      <input
        id="mode-expert"
        class="mode-button"
        type="radio"
        name="mode"
        bind:group={$currentMode}
        value="expert"
      />
      <label class="mode-label" for="mode-expert">
        {$wiStrings["expert-mode"]}
      </label>
    </div>
  {/if}
</div>

<style>
  #mode-select {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
  }

  .mode-option {
    position: relative;
    height: 7rem;
    width: 7rem;
    margin: 0.5rem;
  }

  .mode-button {
    position: absolute;
    opacity: 0;
    top: 0;
    left: 0;
    height: 100%;
    width: 100%;
    margin: 0;
    cursor: pointer;
  }

  .mode-label {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    width: 100%;
    height: 100%;
    border: 2px solid #A20013;
    color: #A20013;
    border-radius: 5px;
    transition: all 300ms ease-in-out;
    text-align: center;
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 1px;
    overflow-wrap: break-word;
    line-break: anywhere;
    white-space: break-spaces;
  }

  .mode-button:checked + .mode-label {
    background-color: #A20013;
    color: #ffffff;
  }
</style>
