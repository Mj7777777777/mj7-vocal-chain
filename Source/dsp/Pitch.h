// MJ7 Vocal Chain - detection de hauteur (MPM / NSDF) et correction de justesse (autotune).
#pragma once
#include "Common.h"
#include <atomic>

namespace mj7
{
/** Detecteur de hauteur monophonique.
    Algorithme : McLeod Pitch Method (difference normalisee, NSDF) sur le signal filtre et decime
    vers ~22 kHz. Plage 75 Hz - 1000 Hz. Une estimation toutes les ~6 ms. */
class PitchDetector
{
public:
    struct Result { float freq = 0.0f; float clarity = 0.0f; bool voiced = false; };

    void prepare (float sampleRate)
    {
        sr = sampleRate; dec = std::max (1, (int) std::floor (sr / 22050.0f)); srDec = sr / (float) dec;
        lp1.setLowpass (std::min (2800.0f, 0.4f * srDec), 0.707f, sr); lp2.copyCoefs (lp1);
        hp.setHighpass (60.0f, 0.707f, sr);
        tauMin = std::max (2, (int) (srDec / 1000.0f)); tauMax = (int) (srDec / 75.0f) + 2;
        win = nextPow2 ((int) (2.4f * (float) tauMax)); hop = std::max (32, (int) (0.0058f * srDec));
        ring.assign ((size_t) win, 0.0f); frame.assign ((size_t) win, 0.0f); nsdf.assign ((size_t) tauMax + 2, 0.0f);
        reset();
    }
    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f); lp1.reset(); lp2.reset(); hp.reset();
        w = 0; decCount = 0; hopCount = 0; last = {};
    }
    /** Nombre d'echantillons (cadence d'origine) entre deux estimations. */
    int hopSamples() const noexcept { return hop * dec; }

    /** Pousse un echantillon. Renvoie true quand une nouvelle estimation est disponible dans last. */
    bool push (float x) noexcept
    {
        const float y = lp2.process (lp1.process (hp.process (x)));
        if (++decCount < dec) return false;
        decCount = 0;
        ring[(size_t) w] = y; if (++w >= win) w = 0;
        if (++hopCount < hop) return false;
        hopCount = 0;
        analyse();
        return true;
    }
    Result last;

private:
    void analyse() noexcept
    {
        const int W = win;
        for (int i = 0; i < W; ++i) { int j = w + i; if (j >= W) j -= W; frame[(size_t) i] = ring[(size_t) j]; }
        const float* x = frame.data();

        float energy = 0.0f;
        for (int i = 0; i < W; ++i) energy += x[i] * x[i];
        last = {};
        if (energy / (float) W < 1.0e-8f) return;                     // sous -80 dBFS : silence

        float m = 2.0f * energy;
        for (int tau = 0; tau <= tauMax; ++tau)
        {
            if (tau > 0) m -= x[tau - 1] * x[tau - 1] + x[W - tau] * x[W - tau];
            float acf = 0.0f;
            const int lim = W - tau;
            for (int j = 0; j < lim; ++j) acf += x[j] * x[j + tau];
            nsdf[(size_t) tau] = m > 1.0e-12f ? 2.0f * acf / m : 0.0f;
        }

        // maxima de chaque lobe positif situe apres le premier passage par zero
        int tau = 1;
        while (tau < tauMax && nsdf[(size_t) tau] > 0.0f) ++tau;
        float best = 0.0f; int peaks[32]; int numPeaks = 0;
        while (tau < tauMax)
        {
            while (tau < tauMax && nsdf[(size_t) tau] <= 0.0f) ++tau;
            int pk = -1; float pv = 0.0f;
            while (tau < tauMax && nsdf[(size_t) tau] > 0.0f)
            {
                if (nsdf[(size_t) tau] > pv && nsdf[(size_t) tau] >= nsdf[(size_t) tau - 1] && nsdf[(size_t) tau] >= nsdf[(size_t) tau + 1])
                { pv = nsdf[(size_t) tau]; pk = tau; }
                ++tau;
            }
            if (pk >= tauMin && numPeaks < 32) { peaks[numPeaks++] = pk; best = std::max (best, pv); }
        }
        if (numPeaks == 0 || best < 0.5f) return;

        for (int i = 0; i < numPeaks; ++i)
        {
            const int p = peaks[i];
            if (nsdf[(size_t) p] >= 0.9f * best)
            {
                const float a = nsdf[(size_t) p - 1], b = nsdf[(size_t) p], c = nsdf[(size_t) p + 1];
                const float den = a - 2.0f * b + c;
                const float shift = std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f;
                last.freq = srDec / ((float) p + clampf (shift, -0.5f, 0.5f));
                last.clarity = b;
                last.voiced = b > 0.75f;
                return;
            }
        }
    }

    float sr = 48000.0f, srDec = 24000.0f; int dec = 2, tauMin = 24, tauMax = 322, win = 1024, hop = 128;
    int w = 0, decCount = 0, hopCount = 0;
    Biquad lp1, lp2, hp; std::vector<float> ring, frame, nsdf;
};

/** Notes autorisees pour chaque gamme (relatif a la tonique). */
inline bool scaleAllows (int scale, int degree) noexcept
{
    static const bool tables[5][12] = {
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },    // chromatique
        { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 },    // majeure
        { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 },    // mineure naturelle
        { 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 },    // pentatonique majeure
        { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 } };  // pentatonique mineure
    return tables[std::clamp (scale, 0, 4)][((degree % 12) + 12) % 12];
}

/** Note d'harmonie pour un intervalle donne, choisie dans la gamme.
    0 = voix coupee, 1 octave basse, 2 sixte basse, 3 quarte basse, 4 tierce basse,
    5 unisson, 6 tierce haute, 7 quinte haute, 8 octave haute. */
inline int harmonyNote (int leadNote, int interval, int key, int scale) noexcept
{
    static const int cand[9][3] = { { 0, 0, 0 }, { -12, -12, -12 }, { -8, -9, -7 }, { -5, -6, -4 }, { -3, -4, -2 },
                                    { 0, 0, 0 }, { 4, 3, 5 }, { 7, 6, 8 }, { 12, 12, 12 } };
    const int* c = cand[std::clamp (interval, 0, 8)];
    for (int i = 0; i < 3; ++i) if (scaleAllows (scale, leadNote + c[i] - key)) return leadNote + c[i];
    return leadNote + c[0];
}

struct HarmonyParams
{
    bool on = false, stack = false;
    int interval[4] = { 0, 0, 0, 0 };          // voir harmonyNote()
    float gain[4] = { 0.5f, 0.5f, 0.5f, 0.5f }; // gain lineaire par voix
    float width = 0.7f, formant = 0.0f, mix = 0.6f;
};

/** Moteur de hauteur : autotune, formants et harmoniseur.
    Principe (PSOLA) : la voix est decoupee en grains de deux periodes, fenetres, puis recolles a la
    periode voulue. L'espacement des grains fixe la hauteur ; la vitesse de lecture a l'interieur de
    chaque grain fixe les formants. Les deux sont donc independants. Les positions de lecture avancent
    par multiples entiers de la periode mesuree : les grains restent en phase entre eux.
    Chaque voix d'harmonie est un deuxieme jeu de grains lu dans la meme memoire.
    Limites connues : monophonique ; sur les consonnes et les voix tres bruitees ou craquees la
    periode est mal definie (leger grain) ; les harmonies suivent la voix principale, elles n'ont
    pas de vibrato propre. */
class PitchCorrector
{
public:
    static constexpr int kMaxGrains = 12, kVoices = 4;

    void prepare (float sampleRate)
    {
        sr = sampleRate; det.prepare (sr);
        maxPeriod = sr / 75.0f; unvoicedPeriod = sr / 200.0f;
        const int size = (int) (0.2f * sr);
        inL.prepare (size); inR.prepare (size); inM.prepare (size); maxDelay = (float) (inL.mask - 4);
        lat = (int) std::round (0.016f * sr);
        aEnable = coefFromMs (12.0f, sr); aAvg = coefFromMs (10.0f, sr); aVoiced = coefFromMs (15.0f, sr);
        reset();
    }
    /** Latence fixe du moteur. 16 ms en mode Mix, 5 ms en mode Tracking. */
    void setLatencySamples (int n) noexcept { if (n != lat) { lat = n; resetVoices(); } }
    int latency() const noexcept { return lat; }

    void reset() noexcept
    {
        inL.reset(); inR.reset(); inM.reset(); det.reset(); now = 0;
        shift = desired = transSm = 0.0f; lastTarget = -1; histN = 0; voiced = false; voicedSm = 0.0f;
        period = unvoicedPeriod; midiDet = 60.0f; target = 60; enableSm = 0.0f; harmSm = 0.0f;
        detectedMidi.store (0.0f); targetMidi.store (0.0f);
        resetVoices();
    }
    void setParams (bool enabled_, int key_, int scale_, float speedMs, float amount01, float transposeSemis, float formantSemis = 0.0f) noexcept
    {
        enabled = enabled_; key = key_; scale = scale_; amount = amount01; transpose = transposeSemis;
        leadFormant = std::pow (2.0f, clampf (formantSemis, -12.0f, 12.0f) / 12.0f);
        aSpeed = coefFromMs (speedMs, sr); aTrans = coefFromMs (10.0f, sr);
    }
    void setHarmony (const HarmonyParams& h) noexcept
    {
        harm = h; harmFormant = std::pow (2.0f, clampf (h.formant, -12.0f, 12.0f) / 12.0f);
        static const float pos[kVoices] = { -1.0f, 1.0f, -0.45f, 0.45f };
        for (int v = 0; v < kVoices; ++v)
        {
            const float a = (0.5f + 0.5f * pos[v] * clampf (h.width, 0.0f, 1.0f)) * 0.5f * kPi;   // panoramique a puissance constante
            panL[v] = std::cos (a) * 1.4142f; panR[v] = std::sin (a) * 1.4142f;
        }
    }

    void process (float* L, float* R, int n) noexcept
    {
        static const float detune[kVoices] = { 0.06f, -0.07f, 0.04f, -0.05f };       // demi-tons
        static const float delayMs[kVoices] = { 12.0f, 18.0f, 9.0f, 22.0f };
        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (L[i] + R[i]);
            if (det.push (mono)) update();
            inL.write (L[i]); inR.write (R[i]); inM.write (mono); ++now;

            const float en = enableSm = (enabled ? 1.0f : 0.0f) + aEnable * (enableSm - (enabled ? 1.0f : 0.0f));
            const float hm = harmSm = (harm.on ? harm.mix : 0.0f) + aEnable * (harmSm - (harm.on ? harm.mix : 0.0f));
            voicedSm = (voiced ? 1.0f : 0.0f) + aVoiced * (voicedSm - (voiced ? 1.0f : 0.0f));
            shift = desired + aSpeed * (shift - desired);
            transSm = transpose + aTrans * (transSm - transpose);
            const float tIn = voiced ? period : unvoicedPeriod;
            const float leadOut = midiDet + shift + transSm;                       // note jouee par la voix principale

            const float dryL = inL.read (lat + 1), dryR = inR.read (lat + 1);      // voix non traitee, meme retard
            float yl = dryL, yr = dryR;
            if (en > 0.001f)
            {
                spawn (lead, voiced ? periodOf (leadOut) : tIn, tIn, leadFormant, 0.0f);
                float gl = 0.0f, gr = 0.0f;
                render (lead, gl, gr, true);
                yl = dryL + en * (gl - dryL); yr = dryR + en * (gr - dryR);
            }
            if (hm > 0.001f)
            {
                const float dev = harm.stack ? 0.0f : (enabled ? leadOut - transSm : midiDet) - (float) target;
                const float cons = 0.35f + 0.65f * voicedSm;                       // consonnes moins fortes dans les choeurs
                for (int v = 0; v < kVoices; ++v)
                {
                    if (harm.interval[v] == 0) continue;
                    const float note = (float) harmNote[v] + dev + transSm + (harm.stack ? 0.0f : detune[v]);
                    spawn (voices[v], voiced ? periodOf (note) : tIn, tIn, harmFormant, 0.001f * delayMs[v] * sr * (harm.stack ? 0.3f : 1.0f));
                    float g = 0.0f, unused = 0.0f;
                    render (voices[v], g, unused, false);
                    g *= hm * harm.gain[v] * cons;
                    yl += g * panL[v]; yr += g * panR[v];
                }
            }
            L[i] = yl; R[i] = yr;
        }
    }
    std::atomic<float> detectedMidi { 0.0f }, targetMidi { 0.0f };   // pour l'affichage (0 = pas de note)

private:
    struct Grain { double src = 0.0, centre = 0.0; float half = 1.0f, fr = 1.0f; bool active = false; };
    struct Voice { Grain g[kMaxGrains]; double nextCentre = 0.0, lastSrc = 0.0; float avgW = 1.0f; bool hasLast = false; };

    float periodOf (float midi) const noexcept
    {
        return clampf (sr / (440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f)), 12.0f, 2.0f * maxPeriod);
    }
    void resetVoices() noexcept
    {
        auto clear = [this] (Voice& v) { for (auto& g : v.g) g.active = false; v.nextCentre = (double) now + maxPeriod; v.hasLast = false; v.avgW = 1.0f; };
        clear (lead); for (auto& v : voices) clear (v);
    }
    /** Lance les grains dont le debut est atteint. tOut : periode voulue, tIn : periode mesuree. */
    void spawn (Voice& v, float tOut, float tIn, float fr, float extraDelay) noexcept
    {
        const float half = clampf (tIn / fr, 8.0f, 2.0f * maxPeriod);
        const double t = (double) now;
        if (v.nextCentre < t - 4.0 * maxPeriod) { v.nextCentre = t + half; v.hasLast = false; }   // voix restee a l'arret
        int guard = 0;
        while (t >= v.nextCentre - half && guard++ < 4)
        {
            Grain* g = nullptr;
            for (auto& c : v.g) if (! c.active) { g = &c; break; }
            if (g != nullptr)
            {
                const double ideal = v.nextCentre - (double) lat - extraDelay;
                double src = ideal;
                if (voiced && v.hasLast)                                         // avance d'un nombre entier de periodes
                {
                    const double k = std::round ((ideal - v.lastSrc) / tIn);
                    if (std::abs (k) <= 6.0) src = v.lastSrc + k * tIn;
                }
                const double hf = (double) half * fr;                            // le grain ne doit jamais lire le futur
                for (int s = 0; s < 6 && (src + hf > v.nextCentre + half - 3.0 || src - hf > v.nextCentre - half - 3.0); ++s) src -= tIn;
                g->src = src; g->centre = v.nextCentre; g->half = half; g->fr = fr; g->active = true;
                v.lastSrc = src; v.hasLast = voiced;
            }
            v.nextCentre += std::max (8.0f, tOut);
        }
    }
    void render (Voice& v, float& outL, float& outR, bool stereo) noexcept
    {
        const double t = (double) now;
        float sumW = 0.0f, l = 0.0f, r = 0.0f;
        for (auto& g : v.g)
        {
            if (! g.active) continue;
            const float dt = (float) (t - g.centre), u = dt / g.half;
            if (u >= 1.0f) { g.active = false; continue; }
            if (u <= -1.0f) continue;
            const float w = 0.5f + 0.5f * std::cos (kPi * u);
            const float delay = clampf ((float) (t - (g.src + (double) dt * g.fr)), 1.0f, maxDelay);
            if (stereo) { l += w * inL.readCubic (delay + 1.0f); r += w * inR.readCubic (delay + 1.0f); }
            else        l += w * inM.readCubic (delay + 1.0f);
            sumW += w;
        }
        v.avgW = sumW + aAvg * (v.avgW - sumW);
        const float norm = 1.0f / std::max (sumW, clampf (v.avgW, 0.4f, 1.0f));   // compense le recouvrement variable des fenetres
        outL = l * norm; outR = r * norm;
    }
    void update() noexcept
    {
        const auto r = det.last;
        if (! r.voiced)
        {
            voiced = false; desired = 0.0f; histN = 0;
            detectedMidi.store (0.0f); targetMidi.store (0.0f);
            return;
        }
        float midi = 69.0f + 12.0f * std::log2 (r.freq / 440.0f);
        hist[histN % 3] = midi; ++histN;
        if (histN >= 3)                                             // mediane de 3 : ecarte les erreurs d'octave isolees
        {
            const float a = hist[0], b = hist[1], c = hist[2];
            midi = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }
        if (! voiced) shift = 0.0f;                                 // debut de note : pas de glissement herite
        voiced = true; midiDet = midi;
        period = clampf (sr / (440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f)), 16.0f, maxPeriod);

        int best = (int) std::lround (midi); float bestDist = 100.0f;
        for (int nte = (int) std::lround (midi) - 6; nte <= (int) std::lround (midi) + 6; ++nte)
            if (scaleAllows (scale, nte - key))
            {
                const float dist = std::abs ((float) nte - midi);
                if (dist < bestDist) { bestDist = dist; best = nte; }
            }
        if (lastTarget >= 0 && scaleAllows (scale, lastTarget - key) && std::abs ((float) lastTarget - midi) < bestDist + 0.2f)
            best = lastTarget;                                       // hysteresis : evite de papillonner entre deux notes
        lastTarget = best; target = best;
        desired = ((float) best - midi) * amount;
        for (int v = 0; v < kVoices; ++v) harmNote[v] = harmonyNote (best, harm.interval[v], key, scale);
        detectedMidi.store (midi); targetMidi.store ((float) best);
    }

    PitchDetector det; DelayLine inL, inR, inM;
    Voice lead, voices[kVoices]; HarmonyParams harm;
    long long now = 0;
    float sr = 48000.0f, maxPeriod = 640.0f, unvoicedPeriod = 240.0f, period = 240.0f, maxDelay = 8000.0f;
    int lat = 768, key = 0, scale = 1, lastTarget = -1, histN = 0, target = 60, harmNote[kVoices] = { 60, 60, 60, 60 };
    float shift = 0.0f, desired = 0.0f, transSm = 0.0f, transpose = 0.0f, amount = 1.0f, aSpeed = 0.0f, aTrans = 0.0f, hist[3] = {};
    float midiDet = 60.0f, leadFormant = 1.0f, harmFormant = 1.0f, enableSm = 0.0f, harmSm = 0.0f, voicedSm = 0.0f;
    float aEnable = 0.0f, aAvg = 0.0f, aVoiced = 0.0f, panL[kVoices] = { 1, 1, 1, 1 }, panR[kVoices] = { 1, 1, 1, 1 };
    bool enabled = true, voiced = false;
};
} // namespace mj7
