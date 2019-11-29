# MTZ reader in WebAssembly

The MTZ file format is used for the storage of reflection data --
the data from diffraction experiments in macromolecular crystallography.

This library is primarily intended for web-based molecular viewers.
It can read MTZ file and transform map coefficients (reciprocal space)
to a density map (real space).

* It is part of [GEMMI](https://project-gemmi.github.io/) (license: MPL 2.0),
  and it is built on the C++ code documented [here](https://gemmi.readthedocs.io/en/latest/hkl.html).
* It uses [PocketFFT](https://gitlab.mpcdf.mpg.de/mtr/pocketfft) (license: MIT) for discrete Fourier transform.
* It uses [Emscripten](https://emscripten.org/) to compile C++ to WASM and JavaScript.

Emscripten is run with option `MODULARIZE`,
which wraps everything in a function which produces a module instance:

    var module = GemmiMtz();
    
WASM code needs to be first compiled.
To wait until everything is ready use a promise-like `then` method:

    GemmiMtz().then(function(module) {
      mtz = module.readMtz(array_buffer);
      ...
      mtz.delete();
    });

The `readMtz` function above takes ArrayBuffer with the content of an MTZ files and returns Mtz object with:

* `cell` -- object representing unit cell with properties corresponding to cell parameters (`a`, `b`, `c`, `alpha`, `beta`, `gamma`),
* `calculate_map_from_labels(f_label: string, phi_label: string)` -- calculates a map by transforming map coefficients from the requested columns; returns Float32Array (length == nx * ny * nz) representing density values on the grid,
* `calculate_map(diff_map: Boolean)` -- calculates a map with default labels:
  - `FWT`/`PHWT` or `2FOFCWT`/`PH2FOFCWT` for normal map (`diff_map===false`),
  - `DELFWT`/`PHDELWT` or `FOFCWT`/`PHFOFCWT` for difference map,
* `last_error` -- contains error message if the functions above return null,
* `nx`, `ny`, `nz` -- dimension of the last calculated map (necessary to interpret the flat array as a 3D array),
* `rmsd` -- also a property of the last calculated map,
* `delete()` -- frees the allocated memory (inside the WebAssembly instance’s memory).

`calculate_map` returns a map as a Float32Array view of the WASM memory.
This view is invalidated when the next map is calculated, so you may want to copy it first.

You can see it in action on https://uglymol.github.io/view/