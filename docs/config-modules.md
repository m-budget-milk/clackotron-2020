---
title: "Config: modules.json"
permalink: /docs/config-modules
parent: "Configuration"
nav_order: 3
---

# Configuration: `modules.json`
The `modules.json` file contains the configuration that informs Clackotron 2000 about the connected modules. Each SBB split flap module has its own distinct address. If you have multiple modules with the same address, disconnect one of them first and rewrite the address [TODO: Link on how to]

The `modules.json` file can be edited on your computer and uploaded as part of the filesystem via PlatformIO or directly on the device by enabling the config mode and opening `http://clackotronip:8080/edit?file=modules`. When editing directly on the device, you must manually reboot the device after editing.

## File format
The `modules.json` file contains a JSON object with a single key `"modules"`. This key expects a list of rows. Each row is a list of module cells.

A module cell can be an object with an address and display length:
```json
{ "address": 10, "length": 2 }
```

For backwards compatibility, a cell may also be just an address number. In that case, the length defaults to `1`. The special string `"X"` can be used as a visual placeholder.

The total number of configured addresses is used for determining how many characters are written to the modules. The two-dimensional arrangement is used for properly displaying the preview in the webinterface.

## Default
```json
{
  "modules": [
    [
      { "address": 1, "length": 1 },
      { "address": 2, "length": 1 }
    ],
    [
      { "address": 3, "length": 2 },
      "X",
      { "address": 4, "length": 1 }
    ]
  ]
}
```

## Examples
The module array is arranged in rows and then columns. So for example, the configuration `[[1,2], [3,4]]` stands for an arrangement with two rows, where the first row contains the modules `1` and `2` and the second row contains the modules `3` and `4`.

### Two rows with two modules each
```json
{
  "modules": [
    [1, 2],
    [3, 4]
  ]
}
```

### A single row with serial numbers 1 - 10
```json
{
  "modules": [
    [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
  ]
}
```

### Three rows with 2x4 and 1x3 serial numbers
```json
{
  "modules": [
    [12, 21, 33, 14],
    [31, 13, 23, 19],
    [10, 9, 43]
  ]
}
```

