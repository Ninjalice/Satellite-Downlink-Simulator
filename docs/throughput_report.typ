= Orbital Simulator: Throughput and Link Simulation Report

#let code(path, line) = [#path + ":" + str(line)]

== Objective

This report explains how the simulator computes *transmission speed* (throughput in Mbps)
and how the communication link is simulated frame by frame.

The implementation is currently in:
- `main.cpp` (link loop and RF metrics)
- `src/orbit.cpp` (rain fade and throughput table model)

== High-Level Model

For each antenna, every simulation step does:

1. Compute satellite geometry relative to the antenna (range, azimuth, elevation).
2. Determine if the satellite is visible and if antenna pointing is locked.
3. Estimate propagation losses and received power.
4. Derive link quality metrics: C/N0, SNR, Eb/N0, margin, BER.
5. Convert quality to throughput in Mbps (Shannon or Eb/N0 table).
6. If not visible or not locked, throughput is forced to zero.

== Inputs (Scenario Parameters)

From JSON configuration (`config/satellites.json`, `config/antennas.json`, `config/sim.json`):

- Satellite radio parameters:
  - `tx_power_dbw`
  - `tx_gain_dbi`
  - `downlink_freq_hz`
  - `bandwidth_hz`
  - `required_ebn0_db`
- Antenna parameters:
  - `rx_gain_dbi`
  - `system_temp_k`
  - `misc_losses_db`
  - tracking slew limits and elevation mask
- Simulation parameters:
  - `rain_rate_mm_h`
  - `use_shannon_model`
  - `shannon_efficiency`
  - `elevation_mask_deg`

== Per-Antenna Simulation Steps

=== 1) Visibility and pointing lock

The link is considered *visible* when elevation exceeds the mask.
The link is *locked* when tracking errors (azimuth/elevation) are below thresholds.

Conceptually:

`visible := elevation >= max(mask, antenna.min_elevation)`

`locked := visible and |az_error| < threshold and |el_error| < threshold`

Only `visible and locked` can carry traffic.

=== 2) Channel and propagation losses

The simulator computes:

- Free-space path loss (FSPL)
- Rain attenuation (elevation-dependent)
- Atmospheric slant-path loss (simple elevation model)
- Misc antenna/system losses

FSPL model used:

`FSPL_dB = 92.45 + 20*log10(range_km) + 20*log10(freq_GHz)`

Rain fade helper (`src/orbit.cpp`):

`L_rain = 0.02 * rainRate / sin(elevation)`

(with lower bounds to avoid singularities near horizon).

=== 3) Received power and noise density

Received power:

`P_rx_dBW = P_tx_dBW + G_tx_dBi + G_rx_dBi - FSPL - L_rain - L_atm - L_misc`

Noise density:

`N0_dBW_per_Hz = -228.6 + 10*log10(T_sys_K)`

Carrier-to-noise density:

`C/N0 = P_rx - N0`

=== 4) SNR, Eb/N0, margin and BER

SNR over receiver bandwidth:

`SNR_dB = C/N0 - 10*log10(B)`

The code uses an assumed bit rate:

`R_b = 0.8 * B`

Then:

`Eb/N0 = C/N0 - 10*log10(R_b)`

Link margin:

`margin_dB = Eb/N0 - EbN0_required`

BER approximation (BPSK-like):

`BER = 0.5 * erfc(sqrt(10^(Eb/N0 / 10)))`

== Throughput Computation

If `visible and locked`:

- Shannon mode (`use_shannon_model = true`):

`R = B * log2(1 + SNR_lin) * eta`

where `eta` is `shannon_efficiency` in `[0.1, 1.0]`.

- Table mode (`use_shannon_model = false`):

A piecewise mapping from `Eb/N0` to Mbps is used (`src/orbit.cpp`):

- `< 2 dB` -> `0.10 Mbps`
- `< 5 dB` -> `0.50 Mbps`
- `< 8 dB` -> `1.00 Mbps`
- `< 11 dB` -> `2.00 Mbps`
- `< 14 dB` -> `5.00 Mbps`
- `>= 14 dB` -> `10.00 Mbps`

Else (not visible or not locked):

`throughput = 0`

== Runtime Aggregation and UI

Each antenna stores a `LinkTelemetry` snapshot with:

- `fspl_db`, `cn0_dbhz`, `ebn0_db`, `margin_db`, `ber`, `throughput_mbps`

The app also computes:

- `activeLinks`
- `totalThroughput` as the sum of active per-antenna throughput
- rolling history arrays for margin and throughput plots

These are shown in:

- Mission Overview cards
- Links tab table
- Link Focus floating panel
- History tab plots

== Practical Interpretation

The model is a *physical-layer throughput estimate*, not a packet/network stack simulation.

It is best interpreted as:

- an instantaneous RF link-capacity indicator,
- driven by geometry, propagation loss, and antenna pointing,
- with optional Shannon-based upper-bound behavior.

== Code Pointers

Main throughput path:

- `main.cpp` around line 744: FSPL and losses
- `main.cpp` around line 748: received power
- `main.cpp` around line 751: noise density and C/N0
- `main.cpp` around line 753: SNR
- `main.cpp` around line 755: Eb/N0 and margin
- `main.cpp` around line 760: throughput branch

Helpers:

- `src/orbit.cpp` around line 55: `rainFadeDb`
- `src/orbit.cpp` around line 60: `throughputTableMbps`

UI controls:

- `main.cpp` around line 1019: rain and mask sliders
- `main.cpp` around line 1021: Shannon model toggle
- `main.cpp` around line 1022: Shannon efficiency slider

== Conclusion

The simulator combines geometric visibility, tracking lock, and a simplified RF link budget
to estimate instantaneous transmission speed in Mbps.

The result is physically meaningful for mission visualization and operational trend analysis,
while remaining computationally lightweight for real-time interaction.
