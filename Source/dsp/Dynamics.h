// MJ7 Vocal Chain - traitements dynamiques : gate, compresseurs, de-esser / EQ dynamique, limiteur.
#pragma once
#include "Common.h"

namespace mj7
{
/** Expander descendant doux (ratio 2:1) avec plage d'attenuation limitee. */
class Gate
{
public:
    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() noexcept { level = 0.0f; grDb = 0.0f; }
    void setParams (float threshDb, float rangeDb, float releaseMs) noexcept
    {
        thresh = threshDb; range = rangeDb;
        aOpen = coefFromMs (1.5f, sr); aClose = coefFromMs (releaseMs, sr);
        aLevel = coefFromMs (15.0f, sr);
    }
    void process (float* L, float* R, int n) noexcept
    {
        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::abs (L[i]), std::abs (R[i]));
            level = pk > level ? pk : pk + aLevel * (level - pk);          // detecteur de crete
            const float db = gainToDb (level);
            const float target = db < thresh ? -std::min (range, thresh - db) : 0.0f;
            const float a = target > grDb ? aOpen : aClose;
            grDb = target + a * (grDb - target);
            const float g = dbToGain (grDb);
            L[i] *= g; R[i] *= g;
            worst = std::min (worst, grDb);
        }
        lastGr = worst;
    }
    float lastGr = 0.0f;   // reduction de gain du dernier bloc (dB, negative)
private:
    float sr = 48000.0f, thresh = -55.0f, range = 10.0f;
    float aOpen = 0, aClose = 0, aLevel = 0, level = 0, grDb = 0;
};

/** Compresseur a detection de crete, liaison stereo, genou doux.
    Mode opto : release dependant du programme (lent quand la reduction est faible). */
class Compressor
{
public:
    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() noexcept { env = 0.0f; makeupSm.reset (1.0f); mixSm.reset (1.0f); }
    void setParams (float threshDb, float ratio_, float attackMs, float releaseMs,
                    float makeupDb, float mix01, bool optoMode = false) noexcept
    {
        thresh = threshDb; slope = 1.0f / std::max (1.0f, ratio_) - 1.0f;
        aAtt = coefFromMs (attackMs, sr); aRel = coefFromMs (releaseMs, sr);
        relMs = releaseMs; makeup = dbToGain (makeupDb); mix = mix01; opto = optoMode;
        makeupSm.setTime (20.0f, sr); mixSm.setTime (20.0f, sr);
    }
    void process (float* L, float* R, int n) noexcept
    {
        const float knee = 6.0f;
        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::abs (L[i]), std::abs (R[i]));
            const float over = gainToDb (pk) - thresh;
            float gr;
            if (2.0f * over < -knee)                 gr = 0.0f;
            else if (2.0f * std::abs (over) <= knee) gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            else                                     gr = slope * over;

            if (gr < env) env = gr + aAtt * (env - gr);
            else
            {
                float a = aRel;
                if (opto)   // plus la reduction est profonde, plus le debut du retour est rapide
                    a = coefFromMs (relMs * (0.25f + 1.75f * (1.0f - std::min (1.0f, -env / 10.0f))), sr);
                env = gr + a * (env - gr);
            }
            worst = std::min (worst, env);
            const float g = dbToGain (env) * makeupSm.next (makeup);
            const float m = mixSm.next (mix);
            L[i] = L[i] * (1.0f - m) + L[i] * g * m;
            R[i] = R[i] * (1.0f - m) + R[i] * g * m;
        }
        lastGr = worst;
    }
    float lastGr = 0.0f;
private:
    float sr = 48000.0f, thresh = -18.0f, slope = -0.75f, aAtt = 0, aRel = 0, relMs = 80.0f;
    float makeup = 1.0f, mix = 1.0f, env = 0.0f; bool opto = false;
    Smooth makeupSm, mixSm;
};

/** Bande dynamique : une cloche qui se creuse seulement quand sa zone depasse le seuil.
    Sert de de-esser et d'EQ dynamique. */
class DynamicBand
{
public:
    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() noexcept { sc.reset(); fl.reset(); fr.reset(); env = 0.0f; gr = 0.0f; counter = 0; appliedGr = -1.0f; }
    void setParams (float freq_, float threshDb, float rangeDb, float q_) noexcept
    {
        if (freq_ != freq || q_ != q) { freq = freq_; q = q_; sc.setBandpass (freq, 2.0f, sr); appliedGr = -1.0f; }
        thresh = threshDb; range = rangeDb;
        aAtt = coefFromMs (1.0f, sr); aRel = coefFromMs (70.0f, sr);
    }
    void process (float* L, float* R, int n) noexcept
    {
        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float s = std::abs (sc.process (0.5f * (L[i] + R[i])));
            env = s > env ? s : s + aRel * (env - s);
            const float target = clampf ((gainToDb (env) - thresh) * 0.8f, 0.0f, range);
            const float a = target > gr ? aAtt : aRel;
            gr = target + a * (gr - target);
            if ((counter++ & 15) == 0 && std::abs (gr - appliedGr) > 0.05f)
            {
                appliedGr = gr;
                fl.setPeak (freq, q, -gr, sr); fr.copyCoefs (fl);
            }
            L[i] = fl.process (L[i]); R[i] = fr.process (R[i]);
            worst = std::max (worst, gr);
        }
        lastGr = -worst;
    }
    float lastGr = 0.0f;
private:
    float sr = 48000.0f, freq = 0.0f, q = 0.0f, thresh = -30.0f, range = 6.0f;
    float aAtt = 0, aRel = 0, env = 0, gr = 0, appliedGr = -1.0f; int counter = 0;
    Biquad sc, fl, fr;
};

/** Limiteur a anticipation (2 ms). Garantit que la crete echantillon ne depasse pas le plafond.
    Limite connue : pas de detection inter-echantillons (true peak). */
class Limiter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        la = std::max (8, (int) (0.002f * sr));
        dl.prepare (la); dr.prepare (la); dl.setDelay (la); dr.setDelay (la);
        req.assign ((size_t) la + 1, 1.0f); avg.assign ((size_t) la, 1.0f);
        reset();
    }
    int latency() const noexcept { return look ? la : 0; }
    /** false = mode Tracking : aucune latence, le gain suit la crete instantanement. */
    void setLookahead (bool on) noexcept { if (on != look) { look = on; reset(); } }
    void reset() noexcept
    {
        dl.reset(); dr.reset(); std::fill (req.begin(), req.end(), 1.0f); std::fill (avg.begin(), avg.end(), 1.0f);
        rp = ap = 0; avgSum = (float) la; rel = 1.0f;
    }
    void setParams (float ceilingDb, bool enabled_) noexcept
    {
        ceiling = dbToGain (ceilingDb); enabled = enabled_; relCoef = 1.0f - coefFromMs (80.0f, sr);
    }
    void process (float* L, float* R, int n) noexcept
    {
        float worst = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::abs (L[i]), std::abs (R[i]));
            const float g = (enabled && pk > ceiling) ? ceiling / pk : 1.0f;
            if (! look)
            {
                rel = std::min (g, rel + (1.0f - rel) * relCoef);
                L[i] *= rel; R[i] *= rel; worst = std::min (worst, rel);
                continue;
            }
            req[(size_t) rp] = g; if (++rp > la) rp = 0;
            float mn = 1.0f;                                   // minimum glissant sur la fenetre d'anticipation
            for (float v : req) mn = std::min (mn, v);
            rel = std::min (mn, rel + (1.0f - rel) * relCoef);
            avgSum += rel - avg[(size_t) ap]; avg[(size_t) ap] = rel; if (++ap >= la) ap = 0;
            if ((i & 1023) == 0) { avgSum = 0.0f; for (float v : avg) avgSum += v; }   // evite la derive d'arrondi
            const float gain = std::min (1.0f, avgSum / (float) la);
            L[i] = dl.process (L[i]) * gain; R[i] = dr.process (R[i]) * gain;
            if (enabled) { L[i] = clampf (L[i], -ceiling, ceiling); R[i] = clampf (R[i], -ceiling, ceiling); }
            worst = std::min (worst, gain);
        }
        lastGr = gainToDb (worst);
    }
    float lastGr = 0.0f;
private:
    float sr = 48000.0f, ceiling = 0.89f, rel = 1.0f, relCoef = 0.001f, avgSum = 0.0f;
    bool enabled = true, look = true; int la = 96, rp = 0, ap = 0;
    FixedDelay dl, dr; std::vector<float> req, avg;
};
} // namespace mj7
