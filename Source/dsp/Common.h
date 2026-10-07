// MJ7 Vocal Chain - briques DSP de base (aucune dependance a JUCE).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mj7
{
constexpr float kPi = 3.14159265358979323846f;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline int nextPow2 (int v) noexcept { int p = 1; while (p < v) p <<= 1; return p; }

/** Coefficient d'un lissage un pole pour une constante de temps en ms. */
inline float coefFromMs (float ms, float sr) noexcept
{
    return ms <= 0.0f ? 0.0f : std::exp (-1.0f / (0.001f * ms * sr));
}

/** Lissage un pole d'une valeur de controle (evite les clics). */
struct Smooth
{
    float z = 0.0f, a = 0.0f;
    void setTime (float ms, float sr) noexcept { a = coefFromMs (ms, sr); }
    void reset (float v) noexcept { z = v; }
    float next (float target) noexcept { z = target + a * (z - target); return z; }
};

/** Filtre biquad (formules RBJ), forme transposee directe II. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0f; }
    void copyCoefs (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** Gain du filtre a la frequence f, en dB (pour dessiner la courbe d'EQ). */
    float magnitudeDb (float f, float sr) const noexcept
    {
        const float w = 2.0f * kPi * f / sr, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0f * w), s2 = std::sin (2.0f * w);
        const float nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = 1.0f + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0f * std::log10 (std::max (1.0e-12f, (nr * nr + ni * ni) / std::max (1.0e-12f, dr * dr + di * di)));
    }

    void set (float nb0, float nb1, float nb2, float na0, float na1, float na2) noexcept
    {
        const float inv = 1.0f / na0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }

    static float safeFreq (float f, float sr) noexcept { return clampf (f, 10.0f, 0.49f * sr); }

    void setLowpass (float f, float q, float sr) noexcept
    {
        const float w = 2.0f * kPi * safeFreq (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set ((1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void setHighpass (float f, float q, float sr) noexcept
    {
        const float w = 2.0f * kPi * safeFreq (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set ((1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void setBandpass (float f, float q, float sr) noexcept
    {
        const float w = 2.0f * kPi * safeFreq (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set (al, 0.0f, -al, 1 + al, -2 * c, 1 - al);
    }
    void setPeak (float f, float q, float gainDb, float sr) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w = 2.0f * kPi * safeFreq (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf (float f, float gainDb, float sr) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f), sq = std::sqrt (A);
        const float w = 2.0f * kPi * safeFreq (f, sr) / sr, c = std::cos (w);
        const float al = std::sin (w) * 0.5f * std::sqrt (2.0f);
        set (A * ((A + 1) - (A - 1) * c + 2 * sq * al), 2 * A * ((A - 1) - (A + 1) * c),
             A * ((A + 1) - (A - 1) * c - 2 * sq * al),
             (A + 1) + (A - 1) * c + 2 * sq * al, -2 * ((A - 1) + (A + 1) * c),
             (A + 1) + (A - 1) * c - 2 * sq * al);
    }
    void setHighShelf (float f, float gainDb, float sr) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f), sq = std::sqrt (A);
        const float w = 2.0f * kPi * safeFreq (f, sr) / sr, c = std::cos (w);
        const float al = std::sin (w) * 0.5f * std::sqrt (2.0f);
        set (A * ((A + 1) + (A - 1) * c + 2 * sq * al), -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - 2 * sq * al),
             (A + 1) - (A - 1) * c + 2 * sq * al, 2 * ((A - 1) - (A + 1) * c),
             (A + 1) - (A - 1) * c - 2 * sq * al);
    }
};

/** Paire de biquads partageant les memes coefficients (gauche / droite). */
struct StereoBiquad
{
    Biquad l, r;
    void reset() noexcept { l.reset(); r.reset(); }
    void sync() noexcept { r.copyCoefs (l); }
    void process (float* L, float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i) { L[i] = l.process (L[i]); R[i] = r.process (R[i]); }
    }
};

/** Retard entier fixe (compensation de latence). */
struct FixedDelay
{
    std::vector<float> buf; int w = 0, len = 0;
    void prepare (int maxLen) { buf.assign ((size_t) std::max (1, maxLen + 1), 0.0f); w = 0; len = 0; }
    void setDelay (int d) noexcept { len = std::clamp (d, 0, (int) buf.size() - 1); }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    float process (float x) noexcept
    {
        if (len == 0) return x;
        const int n = (int) buf.size();
        buf[(size_t) w] = x;
        int r = w - len; if (r < 0) r += n;
        if (++w >= n) w = 0;
        return buf[(size_t) r];
    }
};

/** Ligne a retard circulaire avec lecture interpolee. */
struct DelayLine
{
    std::vector<float> buf; int mask = 0, w = 0;
    void prepare (int minSize) { const int n = nextPow2 (std::max (4, minSize + 4)); buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }
    void write (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    /** Lit l'echantillon ecrit il y a d echantillons (d >= 1 apres write). */
    float read (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
    float readLin (float d) const noexcept
    {
        const int i = (int) d; const float f = d - (float) i;
        const float a = buf[(size_t) ((w - i) & mask)], b = buf[(size_t) ((w - i - 1) & mask)];
        return a + f * (b - a);
    }
    /** Interpolation cubique (Hermite) pour le decalage de hauteur. */
    float readCubic (float d) const noexcept
    {
        const int i = (int) d; const float f = d - (float) i;
        const float xm1 = buf[(size_t) ((w - i + 1) & mask)], x0 = buf[(size_t) ((w - i) & mask)];
        const float x1 = buf[(size_t) ((w - i - 1) & mask)], x2 = buf[(size_t) ((w - i - 2) & mask)];
        const float c = (x1 - xm1) * 0.5f, v = x0 - x1, w2 = c + v, a = w2 + v + (x2 - x0) * 0.5f, b = w2 + a;
        return ((((a * f) - b) * f + c) * f + x0);
    }
};
} // namespace mj7
