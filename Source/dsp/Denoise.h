// MJ7 Vocal Chain - reduction de bruit spectrale.
#pragma once
#include "Analyzer.h"
#include <atomic>

namespace mj7
{
/** Reduction de bruit par attenuation spectrale (filtre de Wiener lisse).
    Le son est decoupe en trames qui se recouvrent (fenetre racine de Hann, recouvrement 75 %) ;
    chaque bande de frequence est attenuee selon son rapport au bruit estime.
    Le bruit vient soit d'un profil appris (bouton ANALYSER), soit d'une estimation automatique qui
    suit le minimum de chaque bande. Latence : une trame (21 ms a 48 kHz).
    Limites connues : a forte dose, leger "gargouillis" dans le fond ; un bruit qui change
    (clics, voix de fond) n'est pas retire. */
class NoiseReducer
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate; N = sr > 120000.0f ? 4096 : (sr > 60000.0f ? 2048 : 1024); hop = N / 4; bins = N / 2 + 1;
        win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) N));
        for (auto* v : { &inL, &inR, &accL, &accR, &outL, &outR, &reL, &imL, &reR, &imR }) v->assign ((size_t) N, 0.0f);
        for (auto* v : { &psm, &adaptive, &gain, &gs, &profile, &pending }) v->assign ((size_t) bins, 0.0f);
        rise = std::pow (2.0f, 1.0f / (2.5f * sr / (float) hop));
        hasProfile = false; pendingReady.store (false);
        reset();
    }
    int latency() const noexcept { return N; }
    int numBins() const noexcept { return bins; }
    int fftSize() const noexcept { return N; }
    void reset() noexcept
    {
        for (auto* v : { &inL, &inR, &accL, &accR, &outL, &outR }) std::fill (v->begin(), v->end(), 0.0f);
        std::fill (gain.begin(), gain.end(), 1.0f); std::fill (psm.begin(), psm.end(), 0.0f);
        rover = N - hop; first = true;
    }
    /** amount 0..1 ; inputGain : gain d'entree applique avant ce module (le profil est mesure sur le signal brut). */
    void setParams (float amount01, float inputGain) noexcept { amount = clampf (amount01, 0.0f, 1.0f); profileScale = inputGain * inputGain; }

    /** Fil des messages : depose un profil de bruit (puissance par bande, numBins() valeurs). Vide = estimation automatique. */
    void loadProfile (const std::vector<float>& binPower)
    {
        if (pendingReady.load()) return;
        if ((int) binPower.size() == bins) { pending = binPower; pendingKind = 1; }
        else pendingKind = 0;
        pendingReady.store (true);
    }

    void process (float* L, float* R, int n) noexcept
    {
        if (pendingReady.load (std::memory_order_acquire))
        {
            hasProfile = pendingKind == 1;
            if (hasProfile) profile = pending;       // meme taille : simple copie, sans allocation
            pendingReady.store (false, std::memory_order_release);
        }
        const int lat = N - hop;
        for (int i = 0; i < n; ++i)
        {
            inL[(size_t) rover] = L[i]; inR[(size_t) rover] = R[i];
            L[i] = outL[(size_t) (rover - lat)]; R[i] = outR[(size_t) (rover - lat)];
            if (++rover >= N)
            {
                frame();
                for (int k = 0; k < hop; ++k) { outL[(size_t) k] = accL[(size_t) k]; outR[(size_t) k] = accR[(size_t) k]; }
                std::memmove (accL.data(), accL.data() + hop, sizeof (float) * (size_t) (N - hop)); std::fill (accL.end() - hop, accL.end(), 0.0f);
                std::memmove (accR.data(), accR.data() + hop, sizeof (float) * (size_t) (N - hop)); std::fill (accR.end() - hop, accR.end(), 0.0f);
                std::memmove (inL.data(), inL.data() + hop, sizeof (float) * (size_t) (N - hop));
                std::memmove (inR.data(), inR.data() + hop, sizeof (float) * (size_t) (N - hop));
                rover = lat;
            }
        }
    }
    float lastReductionDb = 0.0f;     // attenuation moyenne de la derniere trame (dB, <= 0)

private:
    void frame() noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            reL[(size_t) i] = inL[(size_t) i] * win[(size_t) i]; imL[(size_t) i] = 0.0f;
            reR[(size_t) i] = inR[(size_t) i] * win[(size_t) i]; imR[(size_t) i] = 0.0f;
        }
        detail::fft (reL, imL); detail::fft (reR, imR);

        const float over = 1.0f + 2.5f * amount, floorG = dbToGain (-(6.0f + 18.0f * amount));
        for (int k = 0; k < bins; ++k)
        {
            const size_t z = (size_t) k;
            const float p = 0.5f * (reL[z] * reL[z] + imL[z] * imL[z] + reR[z] * reR[z] + imR[z] * imR[z]);
            psm[z] = first ? p : 0.6f * psm[z] + 0.4f * p;
            adaptive[z] = (first || psm[z] < adaptive[z]) ? psm[z] : adaptive[z] * rise;
            const float noise = hasProfile ? profile[z] * profileScale : adaptive[z] * 4.0f;
            const float g = clampf ((psm[z] - over * noise) / std::max (psm[z], 1.0e-20f), floorG, 1.0f);
            gain[z] = g > gain[z] ? 0.5f * (gain[z] + g) : 0.8f * gain[z] + 0.2f * g;       // remonte vite, redescend doucement
        }
        first = false;
        float sum = 0.0f;
        for (int k = 0; k < bins; ++k)                                                    // lissage entre bandes voisines
        {
            const float a = gain[(size_t) std::max (0, k - 1)], b = gain[(size_t) k], c = gain[(size_t) std::min (bins - 1, k + 1)];
            gs[(size_t) k] = amount <= 0.0f ? 1.0f : 0.25f * a + 0.5f * b + 0.25f * c; sum += gs[(size_t) k];
        }
        lastReductionDb = gainToDb (sum / (float) bins);
        for (int k = 0; k < bins; ++k)
        {
            const float g = gs[(size_t) k]; const size_t a = (size_t) k, b = (size_t) ((N - k) % N);
            reL[a] *= g; imL[a] *= g; reR[a] *= g; imR[a] *= g;
            if (b != a) { reL[b] *= g; imL[b] *= g; reR[b] *= g; imR[b] *= g; }
        }
        // transformee inverse : conjugue, transformee directe, conjugue, division par N
        for (int i = 0; i < N; ++i) { imL[(size_t) i] = -imL[(size_t) i]; imR[(size_t) i] = -imR[(size_t) i]; }
        detail::fft (reL, imL); detail::fft (reR, imR);
        const float scale = 1.0f / ((float) N * 2.0f);        // 1/N pour l'inverse, 1/2 pour la somme des fenetres
        for (int i = 0; i < N; ++i)
        {
            accL[(size_t) i] += reL[(size_t) i] * win[(size_t) i] * scale;
            accR[(size_t) i] += reR[(size_t) i] * win[(size_t) i] * scale;
        }
    }

    float sr = 48000.0f, amount = 0.5f, profileScale = 1.0f, rise = 1.0015f;
    int N = 1024, hop = 256, bins = 513, rover = 768, pendingKind = 0; bool first = true, hasProfile = false;
    std::atomic<bool> pendingReady { false };
    std::vector<float> win, inL, inR, accL, accR, outL, outR, reL, imL, reR, imR, psm, adaptive, gain, gs, profile, pending;
};
} // namespace mj7
