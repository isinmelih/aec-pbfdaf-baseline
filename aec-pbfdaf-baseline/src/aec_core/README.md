## AEC Core

This directory contains the core implementation of a real-time
Partitioned Block Frequency-Domain Adaptive Filter (PBFDAF)
for acoustic echo cancellation.

The design is intentionally focused on clarity, correctness,
and real-time behavior rather than product-level optimizations.
It is intended to serve as a research and benchmarking baseline.

### Key characteristics

- Partitioned block frequency-domain adaptive filtering (PBFDAF)
- Linear convolution constraint for correct echo path modeling
- Double-talk handling based on energy and coherence criteria
- Online ERLE measurement and convergence statistics
- Allocation-free real-time processing path

### Scope and limitations

This implementation does not aim to be state-of-the-art or
feature-complete. Instead, it prioritizes:

- Transparent algorithmic structure
- Measurable behavior
- Ease of modification for research and experimentation

The code is suitable as a reference baseline for comparing
alternative AEC designs, parameter choices, or learning-based
approaches.
