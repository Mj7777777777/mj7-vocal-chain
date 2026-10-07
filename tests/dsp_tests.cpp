// Tests des traitements (sans JUCE) :  g++ -std=c++20 -O2 -I Source tests/dsp_tests.cpp -o dsp_tests && ./dsp_tests
#include "dsp/Analyzer.h"
#include "dsp/Denoise.h"
#include "dsp/Dynamics.h"
#include "dsp/Effects.h"
#include "dsp/Pitch.h"
#include <cstdio>
#include <random>

using namespace mj7;
static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  ECHEC: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static std::mt19937 rng (7);
static float noise() { return std::uniform_real_distribution<float> (-1.0f, 1.0f) (rng); }
static bool finite (const std::vector<float>& v) { for (float s : v) if (! std::isfinite (s)) return false; return true; }
static float peakOf (const std::vector<float>& v, size_t from = 0) { float p = 0; for (size_t i = from; i < v.size(); ++i) p = std::max (p, std::abs (v[i])); return p; }

/** Son voise : 12 harmoniques en 1/h. */
static void voiced (std::vector<float>& out, size_t start, size_t len, float f0, float sr, float amp, double& phase)
{
    for (size_t i = 0; i < len && start + i < out.size(); ++i)
    {
        phase += 2.0 * 3.141592653589793 * f0 / sr;
        float s = 0.0f;
        for (int h = 1; h <= 12; ++h) if (f0 * (float) h < 0.45f * sr) s += std::sin ((float) (phase * h)) / (float) h;
        out[start + i] += amp * 0.5f * s;
    }
}
static float measurePitch (const std::vector<float>& x, float sr, size_t from)
{
    PitchDetector d; d.prepare (sr); std::vector<float> f;
    for (size_t i = from; i < x.size(); ++i) if (d.push (x[i]) && d.last.voiced) f.push_back (d.last.freq);
    return f.empty() ? 0.0f : detail::percentile (f, 0.5f);
}
static float cents (float a, float b) { return 1200.0f * std::log2 (a / b); }

template <typename Fn> static void stress (const char* name, float sr, Fn&& fn)
{
    for (int block : { 1, 7, 64, 480, 2048 })
    {
        std::vector<float> L ((size_t) sr), R ((size_t) sr);
        for (size_t i = 0; i < L.size(); ++i)
        {
            const float a = i < L.size() / 3 ? 0.0f : (i < 2 * L.size() / 3 ? 1.0f : 4.0f);   // silence, plein niveau, surcharge
            L[i] = a * noise(); R[i] = a * noise();
        }
        for (size_t p = 0; p < L.size(); p += (size_t) block)
            fn (L.data() + p, R.data() + p, (int) std::min ((size_t) block, L.size() - p));
        CHECK (finite (L) && finite (R), "%s : valeur non finie (sr %.0f, bloc %d)", name, sr, block);
    }
}

int main()
{
    for (float sr : { 44100.0f, 48000.0f, 96000.0f, 192000.0f })
    {
        std::printf ("== %.0f Hz ==\n", sr);

        { Gate g; g.prepare (sr); g.setParams (-50, 12, 150); stress ("Gate", sr, [&] (float* l, float* r, int n) { g.process (l, r, n); }); }
        { Compressor c; c.prepare (sr); c.setParams (-20, 4, 3, 80, 3, 1); stress ("Comp FET", sr, [&] (float* l, float* r, int n) { c.process (l, r, n); }); }
        { Compressor c; c.prepare (sr); c.setParams (-20, 3, 15, 400, 2, 1, true); stress ("Comp opto", sr, [&] (float* l, float* r, int n) { c.process (l, r, n); }); }
        { DynamicBand d; d.prepare (sr); d.setParams (6500, -30, 8, 1.2f); stress ("De-esser", sr, [&] (float* l, float* r, int n) { d.process (l, r, n); }); }
        { Doubler d; d.prepare (sr); d.setParams (25, 40, 1); stress ("Doubleur", sr, [&] (float* l, float* r, int n) { d.process (l, r, n); }); }
        { PitchCorrector p; p.prepare (sr); p.setParams (true, 0, 1, 0, 1, 12); stress ("Autotune +12", sr, [&] (float* l, float* r, int n) { p.process (l, r, n); }); }
        { PitchCorrector p; p.prepare (sr); p.setParams (true, 0, 1, 0, 1, -12); stress ("Autotune -12", sr, [&] (float* l, float* r, int n) { p.process (l, r, n); }); }
        for (int mode = 0; mode < 5; ++mode)
        {
            Saturator s; s.prepare (sr * 4); s.setParams (mode, 36, 14000, 1);
            stress ("Saturation", sr, [&] (float* l, float* r, int n) { s.process (l, n, 0); s.process (r, n, 1); });
        }
        {
            StereoDelay d; d.prepare (sr); d.setParams (2000, 0.95f, 300, 5000, true, 1); std::vector<float> wl (4096), wr (4096);
            stress ("Delay", sr, [&] (float* l, float* r, int n) { d.process (l, r, wl.data(), wr.data(), n); std::copy_n (wl.data(), n, l); std::copy_n (wr.data(), n, r); });
        }
        for (int type = 0; type < 3; ++type)
        {
            PlateReverb rv; rv.prepare (sr); rv.setParams (type, 150, 10, 16000, 20, 1); std::vector<float> wl (4096), wr (4096);
            stress ("Reverbe", sr, [&] (float* l, float* r, int n) { rv.process (l, r, wl.data(), wr.data(), n); std::copy_n (wl.data(), n, l); std::copy_n (wr.data(), n, r); });
        }
        { LevelMeter m; m.prepare (sr); stress ("Mesure", sr, [&] (float* l, float* r, int n) { m.process (l, r, n); }); CHECK (std::isfinite (m.lufs), "LUFS non fini"); }

        // --- limiteur : plafond respecte et latence exacte ---
        {
            Limiter lim; lim.prepare (sr); lim.setParams (-1.0f, true);
            std::vector<float> L ((size_t) sr), R ((size_t) sr);
            for (size_t i = 0; i < L.size(); ++i) { L[i] = 6.0f * noise() * (i % 4000 < 200 ? 1.0f : 0.1f); R[i] = L[i] * 0.7f; }
            for (size_t p = 0; p < L.size(); p += 256) lim.process (L.data() + p, R.data() + p, (int) std::min ((size_t) 256, L.size() - p));
            CHECK (peakOf (L) <= dbToGain (-1.0f) * 1.0001f, "Limiteur : crete %.4f au-dessus du plafond", peakOf (L));
            lim.reset(); std::vector<float> a (2000, 0.0f), b (2000, 0.0f); a[100] = b[100] = 0.25f;
            lim.process (a.data(), b.data(), 2000);
            size_t pos = 0; for (size_t i = 0; i < a.size(); ++i) if (std::abs (a[i]) > std::abs (a[pos])) pos = i;
            CHECK ((int) pos - 100 == lim.latency(), "Limiteur : latence mesuree %d, declaree %d", (int) pos - 100, lim.latency());
            CHECK (std::abs (a[pos] - 0.25f) < 1.0e-4f, "Limiteur : pas transparent sous le plafond (%.4f)", a[pos]);
        }

        // --- LUFS : sinus 997 Hz plein niveau stereo = -3.01 + 3.01 = ~0 LUFS -> a -20 dBFS : ~ -20 LUFS ---
        {
            LevelMeter m; m.prepare (sr); std::vector<float> s ((size_t) (4 * sr));
            for (size_t i = 0; i < s.size(); ++i) s[i] = 0.1f * std::sin (2.0f * kPi * 997.0f * (float) i / sr);
            m.process (s.data(), s.data(), (int) s.size());
            CHECK (std::abs (m.lufs - (-20.0f)) < 0.3f, "LUFS : %.2f au lieu de -20", m.lufs);
        }

        // --- detection de hauteur ---
        for (float f0 : { 82.4f, 110.0f, 196.0f, 261.6f, 440.0f, 659.3f, 880.0f })
        {
            std::vector<float> x ((size_t) sr, 0.0f); double ph = 0; voiced (x, 0, x.size(), f0, sr, 0.3f, ph);
            const float got = measurePitch (x, sr, (size_t) (0.2f * sr));
            CHECK (got > 0 && std::abs (cents (got, f0)) < 5.0f, "Detection %.1f Hz -> %.2f Hz", f0, got);
        }

        // --- autotune ---
        {
            // 226 Hz est 47 cents au-dessus de La2 (220 Hz) : doit etre ramene a 220 Hz
            std::vector<float> L ((size_t) (2 * sr), 0.0f); double ph = 0; voiced (L, 0, L.size(), 226.0f, sr, 0.3f, ph);
            auto R = L; PitchCorrector p; p.prepare (sr); p.setParams (true, 0, 0, 0, 1, 0);
            for (size_t i = 0; i < L.size(); i += 333) p.process (L.data() + i, R.data() + i, (int) std::min ((size_t) 333, L.size() - i));
            const float got = measurePitch (L, sr, (size_t) (0.5f * sr));
            std::printf ("  autotune 226 Hz -> %.2f Hz (cible 220)\n", got);
            CHECK (std::abs (cents (got, 220.0f)) < 6.0f, "Autotune : %.2f Hz au lieu de 220", got);
            CHECK (peakOf (L) < 0.6f, "Autotune : crete anormale %.3f", peakOf (L));

            // gamme de Do majeur : 311 Hz (Re#) doit aller vers Mi (329.6) ou Re (293.7)
            std::vector<float> a ((size_t) (2 * sr), 0.0f); ph = 0; voiced (a, 0, a.size(), 318.0f, sr, 0.3f, ph);
            auto b = a; p.reset(); p.setParams (true, 0, 1, 0, 1, 0); p.process (a.data(), b.data(), (int) a.size());
            const float got2 = measurePitch (a, sr, (size_t) (0.5f * sr));
            std::printf ("  autotune 318 Hz en Do majeur -> %.2f Hz (cible 329.63)\n", got2);
            CHECK (std::abs (cents (got2, 329.63f)) < 6.0f, "Autotune gamme : %.2f Hz", got2);

            // transposition -12
            std::vector<float> c ((size_t) (2 * sr), 0.0f); ph = 0; voiced (c, 0, c.size(), 220.0f, sr, 0.3f, ph);
            auto d = c; p.reset(); p.setParams (true, 0, 0, 0, 1, -12); p.process (c.data(), d.data(), (int) c.size());
            const float got3 = measurePitch (c, sr, (size_t) (0.5f * sr));
            CHECK (std::abs (cents (got3, 110.0f)) < 8.0f, "Transposition -12 : %.2f Hz au lieu de 110", got3);

            // desactive : simple retard egal a la latence declaree
            std::vector<float> e ((size_t) sr, 0.0f), f ((size_t) sr, 0.0f);
            for (size_t i = 0; i < e.size(); ++i) e[i] = f[i] = 0.3f * std::sin (2.0f * kPi * 300.0f * (float) i / sr);
            auto ref = e; p.reset(); p.setParams (false, 0, 0, 0, 1, 0); p.process (e.data(), f.data(), (int) e.size());
            float err = 0; const size_t lat = (size_t) p.latency();
            for (size_t i = (size_t) (0.5f * sr); i < e.size(); ++i) err = std::max (err, std::abs (e[i] - ref[i - lat]));
            CHECK (err < 2.0e-3f, "Autotune desactive : ecart %.5f par rapport au retard pur", err);
        }

        // --- V2 : formants, harmonies, reduction de bruit, limiteur sans latence ---
        {
            auto tone = [sr] (const std::vector<float>& x, float f, size_t from)     // niveau d'une frequence (Goertzel), en dB
            {
                double re = 0, im = 0; const size_t n = x.size() - from;
                for (size_t i = 0; i < n; ++i) { const double a = 2.0 * 3.141592653589793 * f * (double) i / sr; re += x[from + i] * std::cos (a); im += x[from + i] * std::sin (a); }
                return (float) (20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / (double) n + 1e-12));
            };
            // voix deja juste (220 Hz) : l'autotune doit etre quasi transparent
            std::vector<float> L ((size_t) (2 * sr), 0.0f); double ph = 0; voiced (L, 0, L.size(), 220.0f, sr, 0.3f, ph);
            const auto ref = L; auto R = L; PitchCorrector p; p.prepare (sr); p.setParams (true, 0, 0, 0, 1, 0);
            p.process (L.data(), R.data(), (int) L.size());
            double e = 0, s2 = 0; const size_t lat = (size_t) p.latency();
            for (size_t i = (size_t) sr; i < L.size(); ++i) { const double d = L[i] - ref[i - lat]; e += d * d; s2 += (double) ref[i - lat] * ref[i - lat]; }
            std::printf ("  autotune sur une voix juste : ecart %.1f dB sous le signal\n", -10.0 * std::log10 (e / s2 + 1e-12));
            CHECK (e / s2 < 0.05, "Autotune pas transparent sur une note juste (%.1f dB)", 10.0 * std::log10 (e / s2 + 1e-12));

            // formants : meme hauteur (220 Hz) quel que soit le reglage, et son different
            for (float form : { -7.0f, 7.0f })
            {
                auto a = ref; auto b = ref; p.reset(); p.setParams (true, 0, 0, 0, 1, 0, form); p.process (a.data(), b.data(), (int) a.size());
                const float got = measurePitch (a, sr, (size_t) (0.5f * sr));
                CHECK (std::abs (cents (got, 220.0f)) < 6.0f, "Formants %+.0f : la hauteur a bouge (%.2f Hz)", form, got);
                CHECK (finite (a) && peakOf (a) < 1.0f, "Formants %+.0f : sortie anormale", form);
                const float h8in = tone (ref, 1760.0f, (size_t) sr), h8out = tone (a, 1760.0f, (size_t) sr);
                CHECK (form < 0 ? h8out < h8in - 3.0f : h8out > h8in - 30.0f, "Formants %+.0f : spectre inchange (%.1f -> %.1f dB)", form, h8in, h8out);
            }

            // harmoniseur : La (220) en Do majeur, tierce haute = Do (261.63), quinte haute = Mi (329.63)
            {
                auto a = ref; auto b = ref; p.reset(); p.setParams (true, 0, 1, 0, 1, 0);
                HarmonyParams h; h.on = true; h.stack = true; h.mix = 1.0f; h.interval[0] = 6; h.interval[1] = 7; h.gain[0] = h.gain[1] = 1.0f;
                p.setHarmony (h); p.process (a.data(), b.data(), (int) a.size());
                const float c4 = tone (a, 261.63f, (size_t) sr) - tone (ref, 261.63f, (size_t) sr), e4 = tone (a, 329.63f, (size_t) sr) - tone (ref, 329.63f, (size_t) sr);
                std::printf ("  harmoniseur : tierce +%.0f dB, quinte +%.0f dB par rapport a la voix seule\n", c4, e4);
                CHECK (c4 > 20.0f && e4 > 20.0f, "Harmoniseur : notes attendues absentes (%.1f / %.1f dB)", c4, e4);
                CHECK (finite (a) && finite (b), "Harmoniseur : valeur non finie");
                CHECK (harmonyNote (57, 6, 0, 1) == 60 && harmonyNote (60, 6, 0, 1) == 64 && harmonyNote (60, 4, 0, 1) == 57 && harmonyNote (59, 7, 0, 1) == 65, "Intervalles d'harmonie");
            }
            // mode Tracking : latence 5 ms, autotune toujours juste
            {
                std::vector<float> a ((size_t) (2 * sr), 0.0f); double ph2 = 0; voiced (a, 0, a.size(), 226.0f, sr, 0.3f, ph2); auto b = a;
                PitchCorrector t; t.prepare (sr); t.setLatencySamples ((int) (0.005f * sr)); t.setParams (true, 0, 0, 0, 1, 0);
                for (size_t i = 0; i < a.size(); i += 64) t.process (a.data() + i, b.data() + i, (int) std::min ((size_t) 64, a.size() - i));
                const float got = measurePitch (a, sr, (size_t) (0.5f * sr));
                CHECK (std::abs (cents (got, 220.0f)) < 6.0f && finite (a), "Tracking : %.2f Hz au lieu de 220", got);
            }
            // reduction de bruit : transparente a 0 %, retire le souffle sans toucher la voix
            {
                NoiseReducer nr; nr.prepare (sr);
                std::vector<float> a ((size_t) sr), b ((size_t) sr); for (size_t i = 0; i < a.size(); ++i) a[i] = b[i] = 0.2f * noise();
                const auto in = a; nr.setParams (0.0f, 1.0f);
                for (size_t i = 0; i < a.size(); i += 100) nr.process (a.data() + i, b.data() + i, (int) std::min ((size_t) 100, a.size() - i));
                float err = 0; const size_t l2 = (size_t) nr.latency();
                for (size_t i = l2 + 4096; i < a.size(); ++i) err = std::max (err, std::abs (a[i] - in[i - l2]));
                CHECK (err < 1.0e-3f, "Reduction de bruit a 0 %% : pas transparente (ecart %.5f)", err);

                for (int withProfile = 0; withProfile < 2; ++withProfile)
                {
                    nr.prepare (sr); nr.setParams (0.7f, 1.0f);
                    if (withProfile) nr.loadProfile (std::vector<float> ((size_t) nr.numBins(), 1.0e-6f * (float) nr.fftSize() * 0.5f));   // bruit blanc d'ecart-type 0.001
                    std::vector<float> x ((size_t) (6 * sr)), y; double ph3 = 0;
                    for (auto& v : x) v = 0.001f * 1.7320508f * noise();                                    // souffle a -60 dBFS
                    voiced (x, (size_t) (2 * sr), (size_t) (2 * sr), 220.0f, sr, 0.3f, ph3); y = x; const auto dry = x;
                    for (size_t i = 0; i < x.size(); i += 512) nr.process (x.data() + i, y.data() + i, (int) std::min ((size_t) 512, x.size() - i));
                    auto rms = [] (const std::vector<float>& v, size_t a0, size_t a1) { double s = 0; for (size_t i = a0; i < a1; ++i) s += (double) v[i] * v[i]; return 10.0 * std::log10 (s / (double) (a1 - a0) + 1e-20); };
                    const double hiss = rms (x, (size_t) (5 * sr), x.size()) - rms (dry, (size_t) (5 * sr), dry.size());
                    const double voice = rms (x, (size_t) (2.5f * sr), (size_t) (3.5f * sr)) - rms (dry, (size_t) (2.5f * sr), (size_t) (3.5f * sr));
                    std::printf ("  reduction de bruit (%s) : souffle %.1f dB, voix %.2f dB\n", withProfile ? "profil appris" : "automatique", hiss, voice);
                    CHECK (hiss < (withProfile ? -12.0 : -6.0) && std::abs (voice) < 0.5 && finite (x), "Reduction de bruit : souffle %.1f dB, voix %.2f dB", hiss, voice);
                }
            }
            // limiteur sans anticipation
            {
                Limiter lim; lim.prepare (sr); lim.setLookahead (false); lim.setParams (-1.0f, true);
                std::vector<float> a ((size_t) sr), b ((size_t) sr); for (size_t i = 0; i < a.size(); ++i) { a[i] = 5.0f * noise(); b[i] = a[i]; }
                lim.process (a.data(), b.data(), (int) a.size());
                CHECK (lim.latency() == 0 && peakOf (a) <= dbToGain (-1.0f) * 1.0001f, "Limiteur Tracking : crete %.4f", peakOf (a));
            }
        }

        // --- reverbe : la queue doit s'eteindre a peu pres dans le temps demande ---
        {
            PlateReverb rv; rv.prepare (sr); rv.setParams (0, 10, 2.0f, 12000, 100, 1);
            const int n = (int) (4 * sr); std::vector<float> in ((size_t) n, 0.0f), wl ((size_t) n), wr ((size_t) n); in[0] = 1.0f;
            rv.process (in.data(), in.data(), wl.data(), wr.data(), n);
            auto energy = [&] (float t0, float t1) { double e = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) e += wl[(size_t) i] * wl[(size_t) i]; return 10.0 * std::log10 (e / ((t1 - t0) * sr) + 1e-20); };
            const double drop = energy (0.2f, 0.4f) - energy (1.2f, 1.4f);     // 1 s d'ecart : ~30 dB attendu pour 2 s de RT60
            std::printf ("  reverbe : %.1f dB de chute en 1 s (30 attendus pour 2 s)\n", drop);
            CHECK (drop > 15.0 && drop < 50.0, "Reverbe : decroissance %.1f dB/s", drop);
        }
    }

    // --- analyse de voix sur une voix de synthese en La mineur ---
    for (float sr : { 44100.0f, 48000.0f })
    {
        const float notes[] = { 220.0f, 261.63f, 329.63f, 293.66f, 246.94f, 220.0f, 196.0f, 220.0f, 261.63f, 349.23f, 329.63f, 220.0f };
        std::vector<float> x ((size_t) (12 * sr), 0.0f); double ph = 0; size_t pos = (size_t) (0.3f * sr);
        Biquad f1, f2, sHp; f1.setPeak (600, 4.0f, 12, sr); f2.setPeak (2600, 12.0f, 14, sr); sHp.setBandpass (7000, 1.2f, sr);
        for (float f0 : notes)
        {
            voiced (x, pos, (size_t) (0.55f * sr), f0, sr, 0.05f, ph); pos += (size_t) (0.55f * sr);
            for (size_t i = 0; i < (size_t) (0.12f * sr) && pos + i < x.size(); ++i) x[pos + i] += 0.05f * sHp.process (noise());   // "s"
            pos += (size_t) (0.33f * sr);                                                                                       // silence
        }
        for (auto& s : x) { s = f2.process (f1.process (s)); s += 0.0001f * noise(); }

        StyleTarget t; const auto r = analyseVoice (x.data(), (int) x.size(), sr, t, 4.0f, 3.0f);
        static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        std::printf ("== analyse %.0f Hz ==\n  valide %d | crete %.1f | RMS %.1f | bruit %.1f (connu %d) | gain %.1f\n", sr, r.valid, r.peakDb, r.rmsDb, r.noiseDb, r.noiseKnown, r.inGain);
        std::printf ("  note basse %.1f Hz | mediane %.1f Hz | tonalite %s %s (%.2f) | coupe-bas %.0f Hz\n", r.lowHz, r.medianHz,
                     r.key >= 0 ? names[r.key] : "?", r.scale == 1 ? "majeur" : "mineur", r.keyConfidence, r.hpf);
        std::printf ("  bandes : grave %.1f | bas-medium %.1f | presence %.1f | air %.1f\n", r.bandLow, r.bandLowMid, r.bandPres, r.bandAir);
        for (int i = 0; i < r.numRes; ++i) std::printf ("  resonance %.0f Hz -> -%.1f dB\n", r.resFreq[i], r.resCut[i]);
        std::printf ("  sifflantes %d a %.0f Hz, seuil %.1f | comp1 %.1f | comp2 %.1f | gate %.1f | EQ dyn %.1f / %.1f\n", r.sibilance, r.sibFreq,
                     r.dsThresh, r.c1Thresh, r.c2Thresh, r.gateThresh, r.dq1Thresh, r.dq2Thresh);
        std::printf ("  ton : grave %.1f | presence %.1f | air %.1f | boue %.1f\n", r.toneLow, r.tonePres, r.toneAir, r.eqG[0]);
        CHECK (r.valid, "Analyse non valide");
        CHECK (std::abs (r.rmsDb + r.inGain + 18.0f) < 0.5f || r.peakDb + r.inGain > -3.5f, "Gain d'entree incoherent");
        CHECK ((r.key == 9 && r.scale == 2) || (r.key == 0 && r.scale == 1), "Tonalite : attendu La mineur ou Do majeur");
        CHECK (r.lowHz > 185.0f && r.lowHz < 230.0f, "Note basse %.1f Hz", r.lowHz);
        CHECK (r.sibilance && r.sibFreq > 5500.0f && r.sibFreq < 8500.0f, "Sifflantes non reperees");
        CHECK (r.noiseKnown && r.gateThresh < -40.0f, "Bruit de fond mal mesure");
        bool found = false; for (int i = 0; i < r.numRes; ++i) if (std::abs (cents (r.resFreq[i], 2600.0f)) < 150.0f) found = true;
        CHECK (found, "Resonance a 2600 Hz non reperee");

        std::vector<float> silence ((size_t) (12 * sr), 0.0f);
        CHECK (! analyseVoice (silence.data(), (int) silence.size(), sr, t, 4.0f, 3.0f).valid, "Le silence ne doit pas etre analyse");
    }

    std::printf (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\n%d ECHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
