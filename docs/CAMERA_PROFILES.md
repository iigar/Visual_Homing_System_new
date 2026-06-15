# Camera Profiles (M10)

A camera profile binds a sensor's capture/target geometry, field of view, and
the matcher/route-quality thresholds tuned for it. Defined in
`core/include/vh/camera_profile.hpp`; CLI: `tools/vh_camera_profile`.

## Fields

| Field | Meaning |
|-------|---------|
| `id` | profile identifier |
| `sensor` | `visible` / `thermal` / `other` |
| `capture_width/height` | native sensor capture resolution |
| `target_width/height` | downscaled frame the matcher works on (≤ capture) |
| `pixel_format` | `gray8` / `thermal16` |
| `horizontal/vertical_fov_rad` | field of view in radians, `(0, π)` |
| `min_confidence_mille` | matcher confidence gate |
| `matcher_window_radius` | matcher sliding-window radius |
| `low_texture_fraction_mille` / `ambiguous_nearest_fraction_mille` / `average_nearest_mad_min` | route-quality thresholds |
| `mean_normalise` | per-frame mean subtraction hint (brightness robustness) |

## FOV → radians-per-pixel

The matcher's direction error (M5) needs radians-per-pixel. M10 derives it from
the profile instead of hand-passing it:

```
rad_per_pixel = horizontal_fov_rad / target_width
matcher_microrad_per_pixel = round(rad_per_pixel * 1e6)
```

`to_matcher_config(profile)` fills `MatcherConfig.microrad_per_pixel` from this.

## Ground footprint (FOV + altitude)

Approximate nadir footprint at altitude/range `h` (metres):

```
ground_width  = 2 * h * tan(horizontal_fov_rad / 2)
ground_height = 2 * h * tan(vertical_fov_rad   / 2)
meters_per_pixel = ground_width / width   (capture or target)
```

`compute_ground_footprint` rejects non-finite / non-positive altitude and any
invalid profile.

## Resolution / altitude relationship (READ BEFORE FIELD USE)

**Higher altitude or larger range increases ground metres-per-pixel.** This:

- erases texture (each pixel covers more ground → less distinctiveness),
- can make a route **recorded at one altitude mismatch** when matched at a
  **different altitude** (the visual scale differs).

Before any field-readiness evidence, log: the expected ground footprint, the
route's recorded altitude/range assumption, the current altitude/range, and the
visual-scale mismatch diagnostic (`visual_scale_mismatch`).

Example (IMX219, 62.2°×48.8°, target 64×48):

| Altitude | ground_width | m/px (target) |
|----------|--------------|---------------|
| 10 m | ~13.6 m | ~0.21 |
| 30 m | ~40.7 m | ~0.64 |
| 60 m | ~81.5 m | ~1.27 |

## SAFETY: diagnostics first

Barometer/rangefinder altitude, ground footprint, meters-per-pixel, and visual-
or barometer-scale mismatch are **DIAGNOSTICS ONLY**. They must **not** affect
live commands without dry-run evidence and a separate safety decision
(DECISIONS D-024). `visual_scale_mismatch` returns a flag for logging — nothing
in the command path consumes it.

## IMX219 built-in profile

`imx219_profile()` ships nominal datasheet FOV (~62.2° × 48.8°).

> ⚠️ **FOV must be MEASURED for the real lens/crop** before field use. The
> sensor's full FOV differs from the cropped/binned capture mode actually used.

## Thermal (Caddx Thermal 256)

A thermal primary is a **separate hardware/capture milestone** — define its
capture transport, pixel-format conversion, calibration, normalization,
route-quality policy, and tests there. Pi libcamera support does **not** cover
non-libcamera thermal devices.
