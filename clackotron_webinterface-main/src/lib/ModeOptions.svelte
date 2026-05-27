<script>
  import {
    currentMode,
    currentOptions,
    currentValues,
    expertTemplate,
  } from "../store/state";
  import { wiIsExpert, wiStrings } from "../store/webinterface";

  const handleInput = (e) => {
    $currentValues[e.target.name] = e.target.value;
  };
</script>

<div id="mode-options" class="block">
  {#if $wiIsExpert && $currentMode == "expert"}
    <div class="mode-option">
      <div class="option-label">{$wiStrings["expert-template"]}</div>
      <input
        class="option-input"
        name="expert"
        type="string"
        bind:value={$expertTemplate}
      />
    </div>
  {/if}

  {#each $currentOptions as option}
    <div class="mode-option">
      <div class="option-label">{option.title}</div>
      <input
        class="option-input uppercase"
        name={option.template}
        type={option.valueType}
        maxlength={option.valueSize}
        min="0"
        max="255"
        value={$currentValues[option.template] || ""}
        on:input={handleInput}
      />
    </div>
  {/each}
</div>

<style>
  .mode-option {
    width: 100%;
    display: flex;
    margin-bottom: 10px;
  }

  .mode-option:last-of-type {
    margin-bottom: 0;
  }

  .option-label {
    flex-direction: row;
    width: 30%;
    line-height: 25px;
    color: #A20013;
  }

  .option-input {
    flex-direction: row;
    width: 70%;
    height: 25px;
    border: 1px solid #A20013;
    border-radius: 5px;
    color: #A20013;
  }

  .uppercase {
    text-transform: uppercase;
  }
</style>
