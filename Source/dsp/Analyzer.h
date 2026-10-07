// MJ7 Vocal Chain - analyse hors temps reel d'un extrait de voix et proposition de reglages.
#pragma once
#include "Common.h"
#include "Pitch.h"

namespace mj7
{
/** Cible de couleur d'un style : niveaux moyens des zones du spectre, en dB par rapport aux mediums
    (500 Hz - 2 kHz), et reduction de gain visee pour chaque compresseur.
    Ces cibles sont des points de depart regles a la main, pas une norme. */
struct StyleTarget
{
    float low = 3.0f, lowMid = 4.0f, pres = -7.0f, air = -16.0f;
    float c1Gr = 5.0f, c2Gr = 2.5f;
};

struct AnalysisResult
{
    bool valid = false;
    // --- mesures ---
    float peakDb = -120, rmsDb = -120, crestDb = 0, noiseDb = -120; bool noiseKnown = false;
    float lowHz = 0, medianHz = 0; int key = -1, scale = 1; float keyConfidence = 0;
    float bandLow = 0, bandLowMid = 0, bandPres = 0, bandAir = 0;      // dB relatifs aux mediums
    int numRes = 0; float resFreq[3] = {}, resCut[3] = {};
    bool sibilance = false; float sibFreq = 6500;
    // --- reglages proposes ---
    float inGain = 0, hpf = 80, gateThresh = -70;
    float eqF[4] = { 300, 500, 1000, 3000 }, eqG[4] = {}, eqQ[4] = { 1.0f, 4.0f, 4.0f, 4.0f };
    float dsFreq = 6500, dsThresh = -30, c1Thresh = -18, c2Thresh = -20;
    float toneLow = 0, tonePres = 0, toneAir = 0, dq1Thresh = -26, dq2Thresh = -32;
    // --- profil de bruit (puissance par Hz, pas de noiseBinHz), mesure dans les silences ; vide si pas de silence ---
    std::vector<float> noiseDensity; float noiseBinHz = 0;
};

namespace detail
{
inline void fft (std::vector<float>& re, std::vector<float>& im)
{
    const int n = (int) re.size();
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.141592653589793 / (double) len;
        const float wr = (float) std::cos (ang), wi = (float) std::sin (ang);
        for (int i = 0; i < n; i += len)
        {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k)
            {
                const size_t a = (size_t) (i + k), b = (size_t) (i + k + len / 2);
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                const float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}
inline float percentile (std::vector<float> v, float p)
{
    if (v.empty()) return 0.0f;
    std::sort (v.begin(), v.end());
    return v[(size_t) clampf (p * (float) (v.size() - 1), 0.0f, (float) (v.size() - 1))];
}
inline float powDb (double p) { return (float) (10.0 * std::log10 (std::max (p, 1.0e-14))); }
}

inline AnalysisResult analyseVoice (const float* x, int numSamples, float sr, const StyleTarget& target,
                                    float c1Ratio, float c1MakeupDb)
{
    using namespace detail;
    AnalysisResult r;
    const int N = nextPow2 ((int) (sr * 0.085f)), hop = N / 2, half = N / 2;
    if (numSamples < 4 * N) return r;
    const int numFrames = (numSamples - N) / hop + 1;
    const float binHz = sr / (float) N;
    const double norm = 2.0 / ((double) N * (double) N * 0.375);
    auto bin = [binHz, half] (float f) { return std::clamp ((int) std::lround (f / binHz), 1, half - 1); };

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) N);

    // --- 1. niveaux par trame ---
    std::vector<float> frameDb ((size_t) numFrames), framePk ((size_t) numFrames);
    float peak = 0.0f;
    for (int f = 0; f < numFrames; ++f)
    {
        double e = 0.0; float pk = 0.0f;
        for (int i = 0; i < N; ++i) { const float s = x[f * hop + i]; e += (double) s * s; pk = std::max (pk, std::abs (s)); }
        frameDb[(size_t) f] = powDb (e / (double) N); framePk[(size_t) f] = gainToDb (pk); peak = std::max (peak, pk);
    }
    const float maxFrame = *std::max_element (frameDb.begin(), frameDb.end());
    if (maxFrame < -60.0f) return r;                                   // rien d'exploitable
    float noise = std::max (-100.0f, percentile (frameDb, 0.05f));
    r.noiseKnown = (maxFrame - noise) >= 25.0f;                         // il faut de vrais silences pour mesurer le bruit
    if (! r.noiseKnown) noise = maxFrame - 60.0f;
    const float activeThresh = std::max (noise + 12.0f, maxFrame - 35.0f);

    // --- 2. spectres ---
    std::vector<double> avg ((size_t) half, 0.0), sibSpec ((size_t) half, 0.0);
    std::vector<float> re ((size_t) N), im ((size_t) N), presLv, airLv, sibLv, vowelSibLv, activePk;
    double activeEnergy = 0.0; int numActive = 0, numSib = 0;
    const int bSibLo = bin (4000.0f), bSibHi = bin (std::min (11000.0f, 0.45f * sr));
    const int bAllLo = bin (100.0f), bAllHi = bin (std::min (16000.0f, 0.45f * sr));
    const int bPresLo = bin (2200.0f), bPresHi = bin (4500.0f), bAirLo = bin (6000.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        if (frameDb[(size_t) f] < activeThresh) continue;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = x[f * hop + i] * win[(size_t) i]; im[(size_t) i] = 0.0f; }
        fft (re, im);
        double all = 0.0, sib = 0.0, pres = 0.0, air = 0.0;
        for (int k = 1; k < half; ++k)
        {
            const double p = ((double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k]) * norm;
            re[(size_t) k] = (float) p;                                 // reutilise re[] comme spectre de puissance
            if (k >= bAllLo && k <= bAllHi) all += p;
            if (k >= bSibLo && k <= bSibHi) sib += p;
            if (k >= bPresLo && k <= bPresHi) pres += p;
            if (k >= bAirLo && k <= bSibHi) air += p;
        }
        const bool isSib = all > 0.0 && sib / all > 0.4;
        if (isSib) { ++numSib; sibLv.push_back (powDb (sib)); for (int k = 1; k < half; ++k) sibSpec[(size_t) k] += re[(size_t) k]; }
        else       { vowelSibLv.push_back (powDb (sib)); for (int k = 1; k < half; ++k) avg[(size_t) k] += re[(size_t) k]; }
        presLv.push_back (powDb (pres)); airLv.push_back (powDb (air));
        activePk.push_back (framePk[(size_t) f]);
        activeEnergy += std::pow (10.0, (double) frameDb[(size_t) f] / 10.0); ++numActive;
    }
    if (numActive < 8) return r;

    // --- 2 bis. profil de bruit : spectre moyen des trames de silence ---
    if (r.noiseKnown)
    {
        std::vector<double> nz ((size_t) half, 0.0); int count = 0;
        for (int f = 0; f < numFrames && count < 80; ++f)
        {
            if (frameDb[(size_t) f] > noise + 6.0f || frameDb[(size_t) f] >= activeThresh - 6.0f) continue;
            for (int i = 0; i < N; ++i) { re[(size_t) i] = x[f * hop + i] * win[(size_t) i]; im[(size_t) i] = 0.0f; }
            fft (re, im);
            for (int k = 0; k < half; ++k) nz[(size_t) k] += ((double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k]) * norm;
            ++count;
        }
        if (count >= 4)
        {
            r.noiseDensity.resize ((size_t) half); r.noiseBinHz = binHz;
            for (int k = 0; k < half; ++k) r.noiseDensity[(size_t) k] = (float) (nz[(size_t) k] / (double) count / (double) binHz);
        }
    }

    r.valid = true;
    r.peakDb = gainToDb (peak); r.rmsDb = powDb (activeEnergy / (double) numActive);
    r.crestDb = r.peakDb - r.rmsDb; r.noiseDb = noise;

    // --- 3. gain d'entree : voix active a -18 dBFS RMS, crete sous -3 dBFS ---
    r.inGain = clampf (std::min (-18.0f - r.rmsDb, -3.0f - r.peakDb), -24.0f, 24.0f);
    const float g = r.inGain;

    // --- 4. hauteur, tessiture, tonalite ---
    {
        PitchDetector det; det.prepare (sr);
        std::vector<float> freqs; double hist[12] = {};
        for (int i = 0; i < numSamples; ++i)
            if (det.push (x[i]) && det.last.voiced && det.last.clarity > 0.8f)
            {
                const int f = std::clamp ((i - N / 2) / hop, 0, numFrames - 1);
                if (frameDb[(size_t) f] < activeThresh) continue;
                freqs.push_back (det.last.freq);
                const int note = (int) std::lround (69.0f + 12.0f * std::log2 (det.last.freq / 440.0f));
                hist[((note % 12) + 12) % 12] += 1.0;
            }
        if (freqs.size() >= 40)
        {
            r.lowHz = percentile (freqs, 0.05f); r.medianHz = percentile (freqs, 0.5f);
            static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
            static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
            int distinct = 0; double hm = 0.0;
            for (double h : hist) { hm += h / 12.0; if (h > 0.03 * (double) freqs.size()) ++distinct; }
            double bestC = -2.0;
            for (int mode = 0; mode < 2 && distinct >= 3; ++mode)
            {
                const double* prof = mode == 0 ? maj : mnr;
                double pm = 0.0; for (int i = 0; i < 12; ++i) pm += prof[i] / 12.0;
                for (int k = 0; k < 12; ++k)
                {
                    double num = 0.0, da = 0.0, db = 0.0;
                    for (int i = 0; i < 12; ++i)
                    {
                        const double a = hist[(i + k) % 12] - hm, b = prof[i] - pm;
                        num += a * b; da += a * a; db += b * b;
                    }
                    const double c = num / std::sqrt (std::max (1.0e-12, da * db));
                    if (c > bestC) { bestC = c; r.key = k; r.scale = mode == 0 ? 1 : 2; }
                }
            }
            r.keyConfidence = (float) std::max (0.0, bestC);
        }
    }
    r.hpf = r.lowHz > 0.0f ? clampf (0.75f * r.lowHz, 60.0f, 140.0f) : 80.0f;

    // --- 5. equilibre spectral compare a la cible du style ---
    auto bandDb = [&avg, &bin] (float f1, float f2)
    {
        const int a = bin (f1), b = std::max (a, bin (f2)); double s = 0.0;
        for (int k = a; k <= b; ++k) s += avg[(size_t) k];
        return powDb (s / (double) (b - a + 1));
    };
    const float mid = bandDb (500.0f, 2000.0f), topHz = std::min (16000.0f, 0.45f * sr);
    r.bandLow = bandDb (100.0f, 250.0f) - mid; r.bandLowMid = bandDb (250.0f, 500.0f) - mid;
    r.bandPres = bandDb (2500.0f, 5000.0f) - mid; r.bandAir = bandDb (8000.0f, topHz) - mid;
    r.toneLow  = clampf (0.7f * (target.low - r.bandLow), -6.0f, 6.0f);
    r.tonePres = clampf (0.7f * (target.pres - r.bandPres), -6.0f, 6.0f);
    r.toneAir  = clampf (0.7f * (target.air - r.bandAir), -6.0f, 6.0f);
    r.eqG[0]   = -clampf (0.7f * (r.bandLowMid - target.lowMid), 0.0f, 6.0f);     // "boue" 300 Hz

    // --- 6. resonances : pics etroits qui depassent la courbe lissee ---
    {
        const int pts = 111;                                              // 250 Hz -> 6 kHz par 1/24 d'octave
        std::vector<float> fine ((size_t) pts), prom ((size_t) pts);
        for (int i = 0; i < pts; ++i)
        {
            const float fc = 250.0f * std::pow (2.0f, (float) i / 24.0f);
            fine[(size_t) i] = bandDb (fc * 0.944f, fc * 1.059f);
        }
        for (int i = 0; i < pts; ++i)
        {
            float s = 0.0f; int c = 0;
            for (int j = std::max (0, i - 12); j <= std::min (pts - 1, i + 12); ++j) { s += fine[(size_t) j]; ++c; }
            prom[(size_t) i] = fine[(size_t) i] - s / (float) c;
        }
        for (int n = 0; n < 3; ++n)
        {
            int best = -1; float bp = 4.0f;
            for (int i = 1; i < pts - 1; ++i)
                if (prom[(size_t) i] > bp && prom[(size_t) i] >= prom[(size_t) i - 1] && prom[(size_t) i] >= prom[(size_t) i + 1])
                { bp = prom[(size_t) i]; best = i; }
            if (best < 0) break;
            r.resFreq[r.numRes] = 250.0f * std::pow (2.0f, (float) best / 24.0f);
            r.resCut[r.numRes] = clampf (0.6f * (bp - 1.0f), 1.5f, 5.0f);
            ++r.numRes;
            for (int j = std::max (0, best - 8); j <= std::min (pts - 1, best + 8); ++j) prom[(size_t) j] = 0.0f;
        }
        for (int a = 0; a < r.numRes; ++a)                                 // tri par frequence
            for (int b = a + 1; b < r.numRes; ++b)
                if (r.resFreq[b] < r.resFreq[a]) { std::swap (r.resFreq[a], r.resFreq[b]); std::swap (r.resCut[a], r.resCut[b]); }
        for (int n = 0; n < r.numRes; ++n) { r.eqF[n + 1] = r.resFreq[n]; r.eqG[n + 1] = -r.resCut[n]; }
    }

    // --- 7. sifflantes ---
    const float vowelHigh = vowelSibLv.empty() ? -80.0f : percentile (vowelSibLv, 0.9f);
    if (numSib >= 3)
    {
        r.sibilance = true;
        double num = 0.0, den = 0.0;
        for (int k = bin (3500.0f); k <= bSibHi; ++k) { num += sibSpec[(size_t) k] * (double) k * binHz; den += sibSpec[(size_t) k]; }
        r.sibFreq = den > 0.0 ? clampf ((float) (num / den), 4500.0f, 9000.0f) : 6500.0f;
        r.dsFreq = r.sibFreq;
        r.dsThresh = clampf (std::max (percentile (sibLv, 0.5f) - 6.0f, vowelHigh + 2.0f) + g, -60.0f, 0.0f);
    }
    else
        r.dsThresh = clampf (vowelHigh + 4.0f + g, -60.0f, 0.0f);

    // --- 8. seuils dynamiques ---
    const float l90 = percentile (activePk, 0.9f) + g;
    const float k1 = 1.0f - 1.0f / std::max (1.5f, c1Ratio);
    r.c1Thresh = clampf (l90 - target.c1Gr / k1, -40.0f, -6.0f);
    r.c2Thresh = clampf (l90 - target.c1Gr + c1MakeupDb - target.c2Gr / (1.0f - 1.0f / 3.0f), -40.0f, -6.0f);
    r.dq1Thresh = clampf (percentile (presLv, 0.9f) + g + r.tonePres - 1.0f, -60.0f, 0.0f);
    r.dq2Thresh = clampf (percentile (airLv, 0.9f) + g + 0.8f * r.toneAir - 2.0f, -60.0f, 0.0f);
    r.gateThresh = r.noiseKnown ? clampf (std::min (noise + 8.0f, activeThresh - 6.0f) + g, -80.0f, -30.0f) : -70.0f;
    return r;
}
} // namespace mj7
