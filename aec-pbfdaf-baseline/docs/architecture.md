# Architecture Overview

This project implements a real-time acoustic echo cancellation
system based on a partitioned block frequency-domain adaptive filter
(PBFDAF).

The architecture is structured to separate platform-specific
integration, signal processing core logic, and user interaction.

---

## High-level structure

aec-pbfdaf-baseline/
├─ APO/ → Windows Audio Processing Object integration
├─ src/
│ ├─ aec_core → Core AEC and ERLE logic
│ └─ app_gui → Parameter control and visualization

---

## Processing flow

At each audio callback:

1. The far-end reference signal is transformed into the frequency domain
   using a partitioned FFT structure.
2. The adaptive filter estimates the echo contribution in the microphone
   signal.
3. The estimated echo is subtracted to produce the residual (error) signal.
4. Filter adaptation is conditionally applied based on double-talk detection.
5. ERLE and convergence statistics are updated online.

All processing is designed to meet real-time constraints:
- No dynamic memory allocation
- Deterministic execution paths
- Bounded computational cost per block

---

## Design decisions

### Why PBFDAF?

Partitioned block frequency-domain adaptive filtering offers a
practical trade-off between convergence speed, computational cost,
and scalability to long echo paths.

This makes it a suitable baseline for real-time AEC research.

### Measurement-driven design

The system explicitly exposes ERLE and convergence behavior.
This ensures that algorithmic changes can be evaluated objectively,
rather than relying on subjective listening alone.

---

## Intended use

This architecture is intended for:
- Research baselines
- Algorithm comparison
- Educational exploration of real-time AEC systems

It is not designed as a production-ready echo canceller.
