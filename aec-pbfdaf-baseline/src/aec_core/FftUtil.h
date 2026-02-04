#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

// Compact, header-only FFT implementation for AEC/NS
// Uses Cooley-Tukey radix-2 algorithm
// Not as fast as FFTW/IPP but significantly faster than O(N^2) DFT

class FftUtil {
public:
    static void fft(std::vector<std::complex<float>>& x) {
        size_t n = x.size();
        if (n <= 1) return;

        // Bit-reversal permutation
        size_t i = 0;
        for (size_t j = 1; j < n - 1; ++j) {
            for (size_t k = n >> 1; k > (i ^= k); k >>= 1);
            if (j < i) std::swap(x[j], x[i]);
        }

        // Butterfly operations
        for (size_t len = 2; len <= n; len <<= 1) {
            float ang = -2.0f * 3.14159265358979323846f / len;
            std::complex<float> wlen(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len) {
                std::complex<float> w(1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j) {
                    std::complex<float> u = x[i + j];
                    std::complex<float> v = x[i + j + len / 2] * w;
                    x[i + j] = u + v;
                    x[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    static void ifft(std::vector<std::complex<float>>& x) {
        // Conjugate input
        for (auto& val : x) val = std::conj(val);
        
        // Forward FFT
        fft(x);
        
        // Conjugate again and scale
        float invN = 1.0f / (float)x.size();
        for (auto& val : x) {
            val = std::conj(val) * invN;
        }
    }

    // Real-to-Complex helper
    static void fft_real(const std::vector<float>& in, std::vector<std::complex<float>>& out) {
        out.resize(in.size());
        for(size_t i=0; i<in.size(); ++i) out[i] = {in[i], 0.0f};
        fft(out);
    }

    // Complex-to-Real helper (takes magnitude or real part? usually we want real part after IFFT)
    static void ifft_real(std::vector<std::complex<float>>& in, std::vector<float>& out) {
        ifft(in);
        out.resize(in.size());
        for(size_t i=0; i<in.size(); ++i) out[i] = in[i].real();
    }
};
