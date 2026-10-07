// MJ7 Vocal Chain - couleur et espace : saturation, doubleur, delay, reverbe, mesure de niveau.
#pragma once
#include "Common.h"

namespace mj7
{
/** Saturation. process() travaille a la cadence surechantillonnee fournie a prepare(). */
class Saturator
{
public:
    enum Mode { Lampe = 0, Bande, Ecretage, Fuzz, Bits };

    void prepare (float oversampledRate) { sr = oversampledRate; reset(); }
    void reset() noexcept { tone[0].reset(); tone[1].reset(); driveSm.reset (1.0f); mixSm.reset (0.0f); }
    void setParams (int mode_, float driveDb, float toneHz, float mix01) noexcept
    {
        mode = mode_; drive = dbToGain (driveDb); mix = mix01; driveNorm = clampf (driveDb / 36.0f, 0.0f, 1.0f);
        if (toneHz != toneFreq) { toneFreq = toneHz; tone[0].setLowpass (toneHz, 0.707f, sr); tone[1].copyCoefs (tone[0]); }
        driveSm.setTime (20.0f, sr); mixSm.setTime (20.0f, sr);
    }
    void process (float* x, int n, int ch) noexcept
    {
        Biquad& lp = tone[ch & 1];
        Smooth d = driveSm, m = mixSm;                 // meme trajectoire de lissage pour les deux canaux
        for (int i = 0; i < n; ++i)
        {
            const float dg = d.next (drive), mg = m.next (mix);
            const float wet = lp.process (shape (x[i] * dg) * std::pow (dg, -0.65f));
            x[i] = x[i] * (1.0f - mg) + wet * mg;
        }
        if (ch == 1 || mono) { driveSm = d; mixSm = m; }
    }
    bool mono = false;

    float shape (float v) const noexcept
    {
        switch (mode)
        {
            case Lampe:    { const float b = 0.2f; return std::tanh (v + b) - std::tanh (b); }
            case Bande:    { const float c = clampf (v, -1.5f, 1.5f); return c - (4.0f / 27.0f) * c * c * c; }
            case Ecretage: return clampf (v, -1.0f, 1.0f);
            case Fuzz:     { const float a = v >= 0.0f ? 1.0f - std::exp (-3.0f * v) : -(1.0f - std::exp (2.0f * v)) * 0.8f; return a; }
            case Bits:     { const float q = std::pow (2.0f, 9.0f - 6.0f * driveNorm); return clampf (std::round (v * q) / q, -1.0f, 1.0f); }
            default:       return v;
        }
    }
private:
    float sr = 192000.0f, drive = 1.0f, mix = 0.0f, toneFreq = 0.0f, driveNorm = 0.0f; int mode = 0;
    Biquad tone[2]; Smooth driveSm, mixSm;
};

/** Petit decaleur de hauteur a deux tetes de lecture (quelques cents), pour le doubleur. */
class MicroShift
{
public:
    void prepare (float sampleRate) { sr = sampleRate; win = 0.035f * sr; dl.prepare ((int) (0.11f * sr)); phase = 0.0f; }
    void reset() noexcept { dl.reset(); phase = 0.0f; }
    float process (float x, float cents, float baseDelaySamples) noexcept
    {
        dl.write (x);
        const float ratio = std::pow (2.0f, cents / 1200.0f);
        phase -= (ratio - 1.0f) / win;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float g1 = 0.5f - 0.5f * std::cos (2.0f * kPi * phase);
        const float base = 2.0f + baseDelaySamples;
        return g1 * dl.readLin (base + phase * win) + (1.0f - g1) * dl.readLin (base + p2 * win);
    }
private:
    DelayLine dl; float sr = 48000.0f, win = 1680.0f, phase = 0.0f;
};

/** Doubleur stereo : une copie legerement plus haute a gauche, plus basse a droite. */
class Doubler
{
public:
    void prepare (float sampleRate) { sr = sampleRate; up.prepare (sr); down.prepare (sr); mixSm.setTime (30.0f, sr); mixSm.reset (0.0f); }
    void reset() noexcept { up.reset(); down.reset(); }
    void setParams (float cents_, float delayMs, float mix01) noexcept { cents = cents_; delay = 0.001f * delayMs * sr; mix = mix01; }
    void process (float* L, float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (L[i] + R[i]);
            const float m = mixSm.next (mix);
            L[i] += m * up.process (mono, cents, delay);
            R[i] += m * down.process (mono, -cents, delay * 1.45f);
        }
    }
private:
    MicroShift up, down; Smooth mixSm; float sr = 48000.0f, cents = 9.0f, delay = 600.0f, mix = 0.0f;
};

/** Delay stereo avec filtres et saturation douce dans la boucle, ping-pong, entree commandable (throw). */
class StereoDelay
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate; dl.prepare ((int) (2.05f * sr)); dr.prepare ((int) (2.05f * sr));
        timeSm.setTime (80.0f, sr); timeSm.reset (0.35f * sr); sendSm.setTime (15.0f, sr); sendSm.reset (0.0f);
        reset();
    }
    void reset() noexcept { dl.reset(); dr.reset(); hp.reset(); lp.reset(); }
    void setParams (float timeMs, float feedback01, float hpHz, float lpHz, bool pingPong, float send01) noexcept
    {
        timeSamples = clampf (0.001f * timeMs * sr, 8.0f, 2.0f * sr); fb = feedback01; ping = pingPong; send = send01;
        if (hpHz != hpF) { hpF = hpHz; hp.l.setHighpass (hpHz, 0.707f, sr); hp.sync(); }
        if (lpHz != lpF) { lpF = lpHz; lp.l.setLowpass (lpHz, 0.707f, sr); lp.sync(); }
    }
    /** Lit inL/inR (voix seche) et ecrit le signal 100 % traite dans wetL/wetR. */
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float t = timeSm.next (timeSamples), s = sendSm.next (send);
            float yl = dl.readLin (t), yr = dr.readLin (t);
            yl = lp.l.process (hp.l.process (yl)); yr = lp.r.process (hp.r.process (yr));
            const float fl = std::tanh (yl * fb), fr = std::tanh (yr * fb);
            if (ping) { dl.write (0.5f * (inL[i] + inR[i]) * s + fr); dr.write (fl); }
            else      { dl.write (inL[i] * s + fl);                  dr.write (inR[i] * s + fr); }
            wetL[i] = yl; wetR[i] = yr;
        }
    }
private:
    DelayLine dl, dr; StereoBiquad hp, lp; Smooth timeSm, sendSm;
    float sr = 48000.0f, timeSamples = 16800.0f, fb = 0.3f, send = 1.0f, hpF = 0.0f, lpF = 0.0f; bool ping = false;
};

/** Reverbe a plaque (topologie Dattorro), trois tailles : plate, hall, room. */
class PlateReverb
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate; k = sr / 29761.0f;
        const float maxSize = 2.0f;
        pre.prepare ((int) (0.16f * sr));
        const int inLen[4] = { 142, 107, 379, 277 };
        for (int i = 0; i < 4; ++i) inAp[i].prepare ((int) ((float) inLen[i] * k * maxSize) + 8);
        const int tLen[8] = { 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
        for (int i = 0; i < 8; ++i) tank[i].prepare ((int) ((float) (tLen[i] + 40) * k * maxSize) + 8);
        hpL.reset(); hpR.reset(); sendSm.setTime (15.0f, sr); sendSm.reset (0.0f);
        setType (0); reset();
    }
    void reset() noexcept
    {
        pre.reset(); for (auto& a : inAp) a.reset(); for (auto& t : tank) t.reset();
        bw = dampL = dampR = outA = outB = 0.0f; lfo = 0.0f;
    }
    void setType (int t) noexcept
    {
        if (t == type) return;
        type = t; size = t == 1 ? 1.75f : (t == 2 ? 0.42f : 1.0f);
        diff1 = t == 2 ? 0.6f : 0.75f; diff2 = t == 2 ? 0.5f : 0.625f;
        reset();
    }
    void setParams (int type_, float predelayMs, float decaySec, float dampHz, float lowCutHz, float send01) noexcept
    {
        setType (type_);
        preSamples = clampf (0.001f * predelayMs * sr, 1.0f, 0.155f * sr);
        decay = clampf (std::pow (10.0f, -3.0f * (0.725f * size * 0.25f) / std::max (0.1f, decaySec)), 0.0f, 0.97f);
        dampCoef = 1.0f - std::exp (-2.0f * kPi * clampf (dampHz, 200.0f, 0.45f * sr) / sr);
        if (lowCutHz != hpF) { hpF = lowCutHz; hpL.setHighpass (lowCutHz, 0.707f, sr); hpR.copyCoefs (hpL); }
        send = send01;
    }
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) noexcept
    {
        const float s = k * size;
        auto len = [s] (int v) { return std::max (2, (int) ((float) v * s)); };
        const int a0 = len (142), a1 = len (107), a2 = len (379), a3 = len (277);
        const int d4453 = len (4453), d1800 = len (1800), d3720 = len (3720);
        const int d4217 = len (4217), d2656 = len (2656), d3163 = len (3163);
        const float m672 = (float) len (672), m908 = (float) len (908), exc = 12.0f * s;
        const float lfoInc = 2.0f * kPi * 0.8f / sr;

        for (int i = 0; i < n; ++i)
        {
            pre.write (0.5f * (inL[i] + inR[i]) * sendSm.next (send));
            float x = pre.readLin (preSamples);
            bw += 0.9995f * (x - bw); x = bw;
            x = allpass (inAp[0], x, a0, diff1); x = allpass (inAp[1], x, a1, diff1);
            x = allpass (inAp[2], x, a2, diff2); x = allpass (inAp[3], x, a3, diff2);

            lfo += lfoInc; if (lfo > 2.0f * kPi) lfo -= 2.0f * kPi;
            const float mod = std::sin (lfo);

            // moitie gauche du reservoir
            float l = x + decay * outB;
            l = allpassMod (tank[0], l, m672 + exc * (1.0f + mod), -0.7f);
            tank[1].write (l); l = tank[1].read (d4453);
            dampL += dampCoef * (l - dampL); l = dampL * decay;
            l = allpass (tank[2], l, d1800, 0.5f);
            tank[3].write (l); const float newA = tank[3].read (d3720);

            // moitie droite
            float r = x + decay * outA;
            r = allpassMod (tank[4], r, m908 + exc * (1.0f - mod), -0.7f);
            tank[5].write (r); r = tank[5].read (d4217);
            dampR += dampCoef * (r - dampR); r = dampR * decay;
            r = allpass (tank[6], r, d2656, 0.5f);
            tank[7].write (r); const float newB = tank[7].read (d3163);
            outA = newA; outB = newB;

            float yl = tank[5].read (len (266)) + tank[5].read (len (2974)) - tank[6].read (len (1913))
                     + tank[7].read (len (1996)) - tank[1].read (len (1990)) - tank[2].read (len (187)) - tank[3].read (len (1066));
            float yr = tank[1].read (len (353)) + tank[1].read (len (3627)) - tank[2].read (len (1228))
                     + tank[3].read (len (2673)) - tank[5].read (len (2111)) - tank[6].read (len (335)) - tank[7].read (len (121));
            wetL[i] = hpL.process (0.6f * yl); wetR[i] = hpR.process (0.6f * yr);
        }
    }
private:
    static float allpass (DelayLine& d, float x, int delay, float g) noexcept
    {
        const float z = d.read (delay), v = x - g * z;
        d.write (v); return z + g * v;
    }
    static float allpassMod (DelayLine& d, float x, float delay, float g) noexcept
    {
        const float z = d.readLin (delay), v = x - g * z;
        d.write (v); return z + g * v;
    }
    float sr = 48000.0f, k = 1.6f, size = 1.0f, diff1 = 0.75f, diff2 = 0.625f, decay = 0.5f, dampCoef = 0.5f;
    float preSamples = 1440.0f, send = 1.0f, hpF = 0.0f, bw = 0, dampL = 0, dampR = 0, outA = 0, outB = 0, lfo = 0;
    int type = -1;
    DelayLine pre, inAp[4], tank[8]; Biquad hpL, hpR; Smooth sendSm;
};

/** Mesure de niveau : crete, RMS et LUFS court terme (fenetre 3 s, ponderation K). */
class LevelMeter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate; blockLen = (int) (0.1f * sr);
        for (int c = 0; c < 2; ++c)
        {
            const double fs = sr;
            double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            double K = std::tan (3.141592653589793 * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            double a0 = 1.0 + K / Q + K * K;
            shelf[c].set ((float) ((Vh + Vb * K / Q + K * K) / a0), (float) (2.0 * (K * K - Vh) / a0), (float) ((Vh - Vb * K / Q + K * K) / a0),
                          1.0f, (float) (2.0 * (K * K - 1.0) / a0), (float) ((1.0 - K / Q + K * K) / a0));
            f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan (3.141592653589793 * f0 / fs);
            a0 = 1.0 + K / Q + K * K;
            hp[c].set (1.0f, -2.0f, 1.0f, 1.0f, (float) (2.0 * (K * K - 1.0) / a0), (float) ((1.0 - K / Q + K * K) / a0));
            shelf[c].reset(); hp[c].reset();
        }
        std::fill (std::begin (blocks), std::end (blocks), 0.0); acc = 0.0; count = 0; bi = 0; rms2 = 0.0f; aRms = coefFromMs (300.0f, sr);
    }
    void process (const float* L, const float* R, int n) noexcept
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
            const float m2 = 0.5f * (L[i] * L[i] + R[i] * R[i]);
            rms2 = m2 + aRms * (rms2 - m2);
            const float kl = hp[0].process (shelf[0].process (L[i])), kr = hp[1].process (shelf[1].process (R[i]));
            acc += (double) (kl * kl + kr * kr);
            if (++count >= blockLen)
            {
                blocks[bi] = acc / (double) blockLen; bi = (bi + 1) % 30; acc = 0.0; count = 0;
                double sum = 0.0; for (double b : blocks) sum += b;
                lufs = (float) (-0.691 + 10.0 * std::log10 (std::max (1.0e-12, sum / 30.0)));
            }
        }
        peak = pk; rmsDb = 10.0f * std::log10 (std::max (rms2, 1.0e-12f));
    }
    float peak = 0.0f, rmsDb = -120.0f, lufs = -120.0f;
private:
    float sr = 48000.0f, rms2 = 0.0f, aRms = 0.0f; int blockLen = 4800, count = 0, bi = 0; double acc = 0.0, blocks[30] = {};
    Biquad shelf[2], hp[2];
};
} // namespace mj7
