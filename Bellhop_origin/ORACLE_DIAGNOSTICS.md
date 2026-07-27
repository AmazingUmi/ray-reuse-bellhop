# Bellhop 2-D ray-step oracle

The original 2-D Bellhop executable contains an optional, single-ray diagnostic
for validating the F2CPP port. It is disabled by default: an ordinary run opens
no diagnostic files and follows the original `Step2D` call path.

## Enabling the diagnostic

Create an output directory, then set both required environment variables:

```bash
mkdir -p /tmp/bellhop-oracle
cd /path/to/case
BELLHOP_ORACLE_DIR=/tmp/bellhop-oracle \
BELLHOP_ORACLE_ALPHA=150 \
/path/to/Bellhop_origin/bin/bellhop case_root
```

- `BELLHOP_ORACLE_DIR` is an existing writable directory. The program does not
  create or remove directories.
- `BELLHOP_ORACLE_ALPHA` is the 1-based launch-angle index after Bellhop reads
  the environment.
- `BELLHOP_ORACLE_SOURCE` is an optional 1-based source-depth index and
  defaults to `1`.

Setting only one required variable, using an out-of-range index, or naming an
unwritable directory is an explicit fatal input error. Each process exports at
most one source/angle pair and replaces `ray_points.csv`,
`reflection_events.csv`, and `manifest.json` in the selected directory.

## Schema version 2

`manifest.json` identifies the schema as
`bellhop.fortran.ray_step_oracle`, records the selected source and launch angle,
frequency, point/step/reflection counts, curvature mode, beam-shift status,
Bellhop's reported `Nsteps`, and a classified termination reason. The
validator remains backward compatible with schema version 1.

`ray_points.csv` contains one row for every stored `ray2D` point:

- `source` is the initial condition;
- `integrated` is a point produced by `Step2D`;
- `top_reflection` or `bottom_reflection` is the duplicate boundary point
  produced by `Reflect2D` with reflected state.

Every row stores 64-bit position `r/z`, slowness `t`, both dynamic `p/q`
components, sound speed, complex travel time (`tau_real_s`, `tau_imag_s`),
amplitude, phase, and bounce counters. Numeric output uses 17 digits after the
decimal point in scientific notation.

Rows with `step_valid=1` additionally store the actual modified-box quadrature:

- final `h`, initial `halfh`, and final weights `hw0/hw1`;
- endpoint integrand `c0/cimag0`;
- midpoint position, slowness, dynamic `p/q`, and `c1/cimag1`.

Rows not produced by integration use finite zero placeholders for these fields
and set `step_valid=0`; consumers must use the validity flag rather than
interpreting the placeholders as quadrature data.

`reflection_events.csv` contains one independently indexed row per call to
`Reflect2D`. It joins to `ray_points.csv` using 1-based
`pre_point_index/post_point_index` and records:

- sea-surface/seabed identity, the original boundary condition character, and
  the active 1-based boundary segment;
- actual pre/post coordinates without projection, tangent, normal,
  incident/reflected slowness, dynamic `p/q`, sound speed, and complex travel
  time;
- the raw dimensionless complex pressure reflection coefficient, magnitude,
  wrapped phase, and cumulative amplitude/phase before and after applying it;
- whether the acoustic-half-space `|R| < 1e-5` legacy amplitude kill or
  optional beam-shift branch was applied.

Rigid and vacuum rows therefore export `R=+1` and `R=-1`, respectively.
Acoustic/file coefficients retain their original complex value even when the
legacy kill sets the propagated amplitude to zero.

Termination reasons currently distinguish:

- source on/outside the boundaries;
- range or depth box exit;
- legacy amplitude threshold;
- two consecutive points outside the top or bottom;
- dynamic `q` overflow;
- trajectory storage exhaustion.

The first matching legacy stop condition is reported if multiple predicates
become true together.

The repository includes a dependency-free schema/finite-value check:

```bash
python3 test/standard_cases/codes/validate_ray_oracle.py \
  /tmp/bellhop-oracle
```

After building the F2CPP Debug preset, a full direct-ray state comparison can
be run against a `constant_speed_direct` oracle:

```bash
python3 test/standard_cases/codes/compare_f2cpp_geometry_oracle.py \
  /tmp/bellhop-oracle \
  Bellhop_F2CPP/build/debug/bellhop_f2cpp_geometry_oracle_probe
```

The comparison covers every source/integrated point and all F2CPP geometry
fields: position, slowness, dynamic `p/q`, sound speed, real travel time,
actual step/weights, and predictor midpoint position. It reports the worst
scaled error under the D-07 component tolerances.

For the repository's `constant_speed_vacuum_rigid` case, use the same probe
with its reflected-path configuration:

```bash
python3 test/standard_cases/codes/compare_f2cpp_geometry_oracle.py \
  /tmp/bellhop-oracle \
  Bellhop_F2CPP/build/debug/bellhop_f2cpp_geometry_oracle_probe \
  --probe-configuration vacuum-rigid
```

This additionally compares reflection-derived rows, bounce counters, and the
integrated-step/reflection-edge sequence.

For `munk_cerveny_cc`, select the matching 27-node C-linear fixture:

```bash
python3 test/standard_cases/codes/compare_f2cpp_geometry_oracle.py \
  /tmp/bellhop-oracle \
  Bellhop_F2CPP/build/debug/bellhop_f2cpp_geometry_oracle_probe \
  --probe-configuration munk
```

## Cartesian Cerveny influence diagnostic

A second, independent diagnostic captures one ray's contribution at one
receiver after image summation and KMAH correction, but before field
accumulation and `ScalePressure`. It is enabled only when all five selectors
are set:

```bash
BELLHOP_INFLUENCE_ORACLE_DIR=/tmp/bellhop-oracle \
BELLHOP_INFLUENCE_ORACLE_SOURCE=1 \
BELLHOP_INFLUENCE_ORACLE_ALPHA=150 \
BELLHOP_INFLUENCE_ORACLE_RECEIVER_RANGE=11 \
BELLHOP_INFLUENCE_ORACLE_RECEIVER_DEPTH=11 \
/path/to/Bellhop_origin/bin/bellhop case_root
```

All four indices are 1-based. The directory must already exist and be
writable. Setting only some selectors, selecting an unsupported mode, or
selecting a receiver without exactly one retained range evaluation is a fatal
input/diagnostic error. Ordinary runs perform no influence-diagnostic I/O.

`influence_manifest.json` identifies schema
`bellhop.fortran.cartesian_cerveny_influence_sample` version 1 and records the
selected indices and coordinates, frequency, source sound speed, epsilon,
beam window, image count, boundary depths, and contribution stage.
`influence_images.csv` contains one row in each enabled image order
(true/surface/bottom), with common fields repeated:

- the two ray-point indices and receiver-range interpolation state;
- complex `q`, `gamma`, `tau`, KMAH before/after receiver interpolation, and
  the complex square-root constant before/after KMAH correction;
- image displacement, polarity, strict window decision, Hermite taper, and
  complex image contribution;
- ordered image sum, final complex128 ray contribution, and the actual
  complex64 increment used by the legacy pressure field.

When ray and influence diagnostics target the same source/angle and directory,
the validator independently joins the influence bracket to `ray_points.csv`
and replays interpolation, BranchCut, window/Hermite, image summation,
`const * sum`, and complex64 quantization:

```bash
python3 test/standard_cases/codes/validate_influence_oracle.py \
  /tmp/bellhop-oracle
```

The frozen regression samples cover a constant-speed direct ray, a Munk
geometric-caustic neighborhood with `KMAH=+1`, and a later Munk receiver after
the legacy branch crossing with `KMAH=-1`.

## Scope and limitations

Ray schema version 2 covers step states and independent reflection events.
Influence schema version 1 separately covers one coherent Cartesian Cerveny
ray/receiver contribution for the `CC/MS` path. Full-field per-ray
decomposition and non-Cartesian influence routines remain outside the schemas.

For an absorbing boundary, the original frequency-dependent
`Amp < 0.005` termination remains in force. Such output is therefore an active
prefix oracle, not a frequency-independent complete geometry path.
