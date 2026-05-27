<script>
  import { currentPreview } from "../store/state";
</script>

<div id="mode-preview" class="block">
  <table class="preview-grid">
    {#each $currentPreview as row, ir}
      <tr>
        {#each row as char, ic}
          <td>{char}</td>
        {/each}
      </tr>
    {/each}
  </table>
</div>

<style>
  .preview-grid {
    margin: 0 auto;
  }

  .preview-grid td {
    width: 50px;
    height: 50px;
    border: 1px solid #A20013;
    text-align: center;
    text-transform: uppercase;
    font-weight: bold;
    color: #A20013;
  }
</style>
