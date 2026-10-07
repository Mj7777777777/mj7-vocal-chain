#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace mj7;

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int pid = 0; pid < kNumParams; ++pid)
    {
        const auto& d = paramDef (pid);
        const juce::ParameterID id { d.id, 1 };
        const auto name = U8 (d.name), unit = U8 (d.unit);
        switch (d.type)
        {
            case PType::tF:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.centre > 0.0f) range.setSkewForCentre (d.centre);
                const int decimals = (d.max - d.min) >= 100.0f ? 0 : ((d.max - d.min) >= 20.0f ? 1 : 2);
                layout.add (std::make_unique<juce::AudioParameterFloat> (id, name, range, d.def,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([decimals, unit] (float value, int)
                        {
                            const auto num = decimals == 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, decimals);
                            return unit.isEmpty() ? num : num + " " + unit;
                        })
                        .withValueFromStringFunction ([] (const juce::String& s) { return s.replaceCharacter (',', '.').getFloatValue(); })));
                break;
            }
            case PType::tI:
                layout.add (std::make_unique<juce::AudioParameterInt> (id, name, (int) d.min, (int) d.max, (int) d.def,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction ([unit] (int value, int)
                    { return (value > 0 ? "+" : "") + juce::String (value) + " " + unit; })));
                break;
            case PType::tB:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, name, d.def > 0.5f));
                break;
            case PType::tC:
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, name,
                    juce::StringArray::fromTokens (U8 (d.choices), "|", ""), (int) d.def));
                break;
        }
    }
    return layout;
}

MJ7Processor::MJ7Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("MJ7 analyse"),
      apvts (*this, nullptr, "MJ7VocalChain", createLayout())
{
    for (int pid = 0; pid < kNumParams; ++pid) raw[pid] = apvts.getRawParameterValue (paramDef (pid).id);
    for (auto& m : meter) m.store (0.0f);
    apvts.addParameterListener ("intensity", this);
    startTimerHz (15);
}

MJ7Processor::~MJ7Processor()
{
    stopTimer();
    stopThread (5000);
    apvts.removeParameterListener ("intensity", this);
}

bool MJ7Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && in.size() <= out.size();
}

juce::AudioProcessorEditor* MJ7Processor::createEditor() { return new MJ7Editor (*this); }

// ================================================================================================
void MJ7Processor::prepareToPlay (double sampleRate, int)
{
    stopThread (5000);
    if (state.load() != done) state.store (idle);
    sr = (float) sampleRate;

    inGainSm.setTime (30.0f, sr); outGainSm.setTime (30.0f, sr); dlyWetSm.setTime (30.0f, sr); revWetSm.setTime (30.0f, sr);
    inGainSm.reset (dbToGain (v (in_gain))); outGainSm.reset (dbToGain (v (out_gain)));
    dlyWetSm.reset (0.0f); revWetSm.reset (0.0f);

    for (auto* f : { &hpf, &eq[0], &eq[1], &eq[2], &eq[3], &hicut, &toneLow, &toneMid, &tonePres, &toneAir, &dcBlock, &fltHp, &fltLp }) f->reset();
    dcBlock.l.setHighpass (12.0f, 0.707f, sr); dcBlock.sync();

    gate.prepare (sr); tune.prepare (sr); denoise.prepare (sr); satDirect.prepare (sr);
    deEss.prepare (sr); dyn1.prepare (sr); dyn2.prepare (sr);
    comp1.prepare (sr); comp2.prepare (sr); punch.prepare (sr);
    oversampler.reset(); oversampler.initProcessing ((size_t) chunk);
    osLatency = juce::roundToInt (oversampler.getLatencyInSamples());
    sat.prepare (sr * 4.0f);
    satBypassL.prepare (osLatency + 1); satBypassR.prepare (osLatency + 1); satBypassL.setDelay (osLatency); satBypassR.setDelay (osLatency);
    doubler.prepare (sr); delay.prepare (sr); reverb.prepare (sr);
    limiter.prepare (sr); outMeter.prepare (sr);
    duckEnv = 0.0f;

    const int maxLatency = denoise.latency() + (int) (0.02f * sr) + osLatency + limiter.latency() + 16;
    bypassL.prepare (maxLatency); bypassR.prepare (maxLatency);
    applyConfig (true);
    setLatencySamples (latency);
    pushNoiseProfile();

    const int needed = (int) (analysisSeconds() * sr);
    if (needed != captureLen) { capture.assign ((size_t) needed, 0.0f); captureLen = needed; captureValid = false; capturePos.store (0); }
}

void MJ7Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() == 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 20.0 && *bpm < 400.0) tempo = *bpm;

    const bool stereoBuffer = buffer.getNumChannels() > 1;
    float* L = buffer.getWritePointer (0);
    float* R = stereoBuffer ? buffer.getWritePointer (1) : nullptr;
    if (stereoBuffer && getTotalNumInputChannels() < 2) std::memcpy (R, L, sizeof (float) * (size_t) n);   // entree mono -> deux canaux

    for (int pos = 0; pos < n; pos += chunk)
    {
        const int len = std::min (chunk, n - pos);
        if (stereoBuffer) processChunk (L + pos, R + pos, len);
        else { std::memcpy (tmpR, L + pos, sizeof (float) * (size_t) len); processChunk (L + pos, tmpR, len); }
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
}

void MJ7Processor::processChunk (float* L, float* R, int n)
{
    // --- capture pour le bouton ANALYSER (signal brut, avant tout traitement) ---
    const int st = state.load (std::memory_order_relaxed);
    if (st == waiting)
    {
        float pk = 0.0f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (L[i]));
        if (pk > 0.003f) state.store (listening);
        else if ((waited += n) > (int) (20.0f * sr)) state.store (failed);
    }
    if (state.load (std::memory_order_relaxed) == listening)
    {
        const int p = capturePos.load (std::memory_order_relaxed), take = std::min (n, captureLen - p);
        for (int i = 0; i < take; ++i) capture[(size_t) (p + i)] = 0.5f * (L[i] + R[i]);
        capturePos.store (p + take);
        if (p + take >= captureLen) state.store (computing);
    }

    applyConfig (false);

    // --- bypass : simple retard egal a la latence declaree ---
    const bool bypassed = on (bypass);
    if (bypassed != wasBypassed) { bypassL.reset(); bypassR.reset(); wasBypassed = bypassed; }
    if (bypassed)
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) { L[i] = bypassL.process (L[i]); R[i] = bypassR.process (R[i]); pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i]))); }
        inPeak.store (std::max (inPeak.load(), pk)); outPeak.store (std::max (outPeak.load(), pk));
        for (auto& m : meter) m.store (0.0f);
        return;
    }

    // --- 1. entree ---
    {
        const float target = dbToGain (v (in_gain)) * (on (phase) ? -1.0f : 1.0f);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = inGainSm.next (target);
            L[i] *= g; R[i] *= g; pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        inPeak.store (std::max (inPeak.load (std::memory_order_relaxed), pk));
        int sp = scopeInPos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { scopeIn[sp] = 0.5f * (L[i] + R[i]); sp = (sp + 1) & (scopeSize - 1); }
        scopeInPos.store (sp, std::memory_order_relaxed);
    }

    // --- 1 bis. reduction de bruit (mode Mix seulement) ---
    if (nrNow) { denoise.setParams (v (nr_amount) * 0.01f, dbToGain (v (in_gain))); denoise.process (L, R, n); meter[mNoise].store (denoise.lastReductionDb); }
    else meter[mNoise].store (0.0f);

    // --- 2. nettoyage ---
    hpf.l.setHighpass (v (hpf_freq), 0.707f, sr); hpf.sync(); hpf.process (L, R, n);
    if (on (gate_on)) { gate.setParams (v (gate_thresh), v (gate_range), v (gate_release)); gate.process (L, R, n); meter[mGate].store (gate.lastGr); }
    else meter[mGate].store (0.0f);

    // --- 3. autotune (toujours traverse : latence constante) ---
    tune.setParams (on (tune_on), (int) v (tune_key), (int) v (tune_scale), v (tune_speed), v (tune_amount) * 0.01f, v (tune_transpose), v (tune_formant));
    {
        HarmonyParams h; h.on = on (harm_on); h.stack = on (harm_stack);
        h.width = v (harm_width) * 0.01f; h.formant = v (harm_formant); h.mix = v (harm_mix) * 0.01f;
        static const int ids[4] = { h1_int, h2_int, h3_int, h4_int };
        for (int i = 0; i < 4; ++i) { h.interval[i] = (int) v (ids[i]); h.gain[i] = 0.7f; }
        tune.setHarmony (h);
    }
    tune.process (L, R, n);

    // --- 4. EQ soustractif ---
    if (on (eq_on))
    {
        static const int ids[4][3] = { { eq1_f, eq1_g, eq1_q }, { eq2_f, eq2_g, eq2_q }, { eq3_f, eq3_g, eq3_q }, { eq4_f, eq4_g, eq4_q } };
        for (int b = 0; b < 4; ++b)
            if (std::abs (v (ids[b][1])) > 0.05f) { eq[b].l.setPeak (v (ids[b][0]), v (ids[b][2]), v (ids[b][1]), sr); eq[b].sync(); eq[b].process (L, R, n); }
        if (v (eq_hicut) < 19500.0f) { hicut.l.setLowpass (v (eq_hicut), 0.707f, sr); hicut.sync(); hicut.process (L, R, n); }
    }

    // --- 5. de-esser ---
    if (on (ds_on)) { deEss.setParams (v (ds_freq), v (ds_thresh), v (ds_range), 1.2f); deEss.process (L, R, n); meter[mDeEss].store (deEss.lastGr); }
    else meter[mDeEss].store (0.0f);

    // --- 6. compresseurs ---
    const float macro = v (macro_comp);
    if (on (c1_on))
    {
        comp1.setParams (v (c1_thresh) - 0.15f * macro, v (c1_ratio), v (c1_attack), v (c1_release), v (c1_makeup), v (c1_mix) * 0.01f);
        comp1.process (L, R, n); meter[mComp1].store (comp1.lastGr);
    }
    else meter[mComp1].store (0.0f);
    if (on (c2_on))
    {
        comp2.setParams (v (c2_thresh) - 0.10f * macro, 3.0f, 15.0f, 350.0f, v (c2_makeup) + 0.04f * macro, v (c2_mix) * 0.01f, true);
        comp2.process (L, R, n); meter[mComp2].store (comp2.lastGr);
    }
    else meter[mComp2].store (0.0f);

    // --- 7. EQ tonal ---
    if (on (tone_on))
    {
        toneLow.l.setLowShelf (v (tone_low_f), v (tone_low_g), sr);    toneLow.sync();  toneLow.process (L, R, n);
        toneMid.l.setPeak (v (tone_mid_f), 1.0f, v (tone_mid_g), sr);   toneMid.sync();  toneMid.process (L, R, n);
        tonePres.l.setPeak (v (tone_pres_f), 0.9f, v (tone_pres_g), sr); tonePres.sync(); tonePres.process (L, R, n);
        toneAir.l.setHighShelf (v (tone_air_f), v (tone_air_g), sr);   toneAir.sync();  toneAir.process (L, R, n);
    }

    // --- 8. saturation (surechantillonnee x4), sinon retard equivalent ---
    if (on (sat_on) && trackingNow)                 // mode Tracking : sans surechantillonnage, donc sans latence
    {
        satDirect.setParams ((int) v (sat_mode), v (sat_drive), v (sat_tone), v (sat_mix) * 0.01f);
        satDirect.process (L, n, 0); satDirect.process (R, n, 1);
        dcBlock.process (L, R, n);
    }
    else if (on (sat_on))
    {
        sat.setParams ((int) v (sat_mode), v (sat_drive), v (sat_tone), v (sat_mix) * 0.01f);
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        auto up = oversampler.processSamplesUp (block);
        sat.process (up.getChannelPointer (0), (int) up.getNumSamples(), 0);
        sat.process (up.getChannelPointer (1), (int) up.getNumSamples(), 1);
        oversampler.processSamplesDown (block);
        dcBlock.process (L, R, n);
    }
    else if (! trackingNow)
        for (int i = 0; i < n; ++i) { L[i] = satBypassL.process (L[i]); R[i] = satBypassR.process (R[i]); }

    // --- 9. EQ dynamique ---
    if (on (dq_on))
    {
        dyn1.setParams (v (dq1_f), v (dq1_thresh), v (dq1_range), 1.6f); dyn1.process (L, R, n);
        dyn2.setParams (v (dq2_f), v (dq2_thresh), v (dq2_range), 1.2f); dyn2.process (L, R, n);
        meter[mDynEq].store (std::min (dyn1.lastGr, dyn2.lastGr));
    }
    else meter[mDynEq].store (0.0f);

    // --- 10. filtre creatif ---
    if (on (flt_on))
    {
        fltHp.l.setHighpass (v (flt_hp), v (flt_res), sr); fltHp.sync(); fltHp.process (L, R, n);
        fltLp.l.setLowpass (v (flt_lp), v (flt_res), sr);  fltLp.sync(); fltLp.process (L, R, n);
    }

    // --- 11. compression parallele ---
    if (on (par_on))
    {
        std::memcpy (tmpL, L, sizeof (float) * (size_t) n); std::memcpy (wetR, R, sizeof (float) * (size_t) n);
        punch.setParams (-30.0f, 10.0f, 1.0f, 90.0f, 8.0f, 1.0f); punch.process (tmpL, wetR, n);
        const float m = 0.7f * v (par_mix) * 0.01f;
        for (int i = 0; i < n; ++i) { L[i] += m * tmpL[i]; R[i] += m * wetR[i]; }
        meter[mPunch].store (punch.lastGr);
    }
    else meter[mPunch].store (0.0f);

    // --- 12. doubleur ---
    doubler.setParams (v (dbl_detune), v (dbl_delay), on (dbl_on) ? v (dbl_mix) * 0.01f : 0.0f);
    doubler.process (L, R, n);

    // --- 13. delay et reverbe en parallele, attenues par la voix (ducking) ---
    std::fill_n (fxL, n, 0.0f); std::fill_n (fxR, n, 0.0f);
    const bool dlyOn = on (dly_on), revOn = on (rev_on);
    if (dlyOn)
    {
        static const float beats[6] = { 0.0f, 2.0f, 1.0f, 0.75f, 0.5f, 0.25f };
        const int sync = std::clamp ((int) v (dly_sync), 0, 5);
        const float ms = sync == 0 ? v (dly_time) : (float) (beats[sync] * 60000.0 / tempo);
        delay.setParams (ms, v (dly_fb) * 0.01f, v (dly_hp), v (dly_lp), on (dly_ping), (on (dly_always) || on (dly_throw)) ? 1.0f : 0.0f);
        delay.process (L, R, wetL, wetR, n);
        const float target = v (dly_mix) * 0.01f;
        for (int i = 0; i < n; ++i) { const float g = dlyWetSm.next (target); fxL[i] += g * wetL[i]; fxR[i] += g * wetR[i]; }
    }
    else if (dlyWasOn) delay.reset();
    dlyWasOn = dlyOn;
    if (revOn)
    {
        reverb.setParams ((int) v (rev_type), v (rev_predelay), v (rev_decay), v (rev_damp), v (rev_hp), (on (rev_always) || on (rev_throw)) ? 1.0f : 0.0f);
        reverb.process (L, R, wetL, wetR, n);
        const float target = v (rev_mix) * 0.01f;
        for (int i = 0; i < n; ++i) { const float g = revWetSm.next (target); fxL[i] += g * wetL[i]; fxR[i] += g * wetR[i]; }
    }
    else if (revWasOn) reverb.reset();
    revWasOn = revOn;
    {
        const bool duck = on (duck_on); const float amt = v (duck_amt), aRel = coefFromMs (v (duck_rel), sr), aAtt = coefFromMs (8.0f, sr);
        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::abs (L[i]), std::abs (R[i]));
            duckEnv = pk + (pk > duckEnv ? aAtt : aRel) * (duckEnv - pk);
            const float gr = duck ? -amt * clampf ((gainToDb (duckEnv) + 40.0f) / 20.0f, 0.0f, 1.0f) : 0.0f;
            const float g = dbToGain (gr);
            L[i] += g * fxL[i]; R[i] += g * fxR[i];
            worst = std::min (worst, gr);
        }
        meter[mDuck].store (worst);
    }

    // --- 14. sortie ---
    {
        const float target = dbToGain (v (out_gain));
        for (int i = 0; i < n; ++i) { const float g = outGainSm.next (target); L[i] *= g; R[i] *= g; }
    }
    limiter.setParams (v (lim_ceiling), on (lim_on)); limiter.process (L, R, n); meter[mLimit].store (limiter.lastGr);

    outMeter.process (L, R, n);
    outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), outMeter.peak));
    outLufs.store (outMeter.lufs); outRms.store (outMeter.rmsDb);
    int sp = scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) { scope[sp] = 0.5f * (L[i] + R[i]); sp = (sp + 1) & (scopeSize - 1); }
    scopePos.store (sp, std::memory_order_relaxed);
}

void MJ7Processor::readScope (float* dest, int n, bool input) const noexcept
{
    const int sp = (input ? scopeInPos : scopePos).load (std::memory_order_relaxed);
    const float* src = input ? scopeIn : scope;
    for (int i = 0; i < n; ++i) dest[i] = src[(sp - n + i) & (scopeSize - 1)];
}

/** Applique le mode (Mix / Tracking) et l'etat de la reduction de bruit, et recalcule la latence. */
void MJ7Processor::applyConfig (bool force)
{
    const bool tracking = v (mode) > 0.5f, nrActive = on (nr_on) && ! tracking;
    if (! force && tracking == trackingNow && nrActive == nrNow) return;
    if (force || nrActive != nrNow) denoise.reset();
    trackingNow = tracking; nrNow = nrActive;
    tune.setLatencySamples ((int) std::round ((tracking ? 0.005f : 0.016f) * sr));
    limiter.setLookahead (! tracking);
    satBypassL.reset(); satBypassR.reset();
    latency = (nrActive ? denoise.latency() : 0) + tune.latency() + (tracking ? 0 : osLatency) + limiter.latency();
    bypassL.setDelay (latency); bypassR.setDelay (latency); bypassL.reset(); bypassR.reset();
    latencyNow.store (latency);
}

/** Convertit le profil de bruit appris par l'analyse vers les bandes du reducteur de bruit. */
void MJ7Processor::pushNoiseProfile()
{
    std::vector<float> binsPower;
    if (! noiseDensity.empty() && noiseBinHz > 0.0f)
    {
        const int nb = denoise.numBins(), N = denoise.fftSize();
        binsPower.resize ((size_t) nb);
        for (int k = 0; k < nb; ++k)
        {
            const float pos = (float) k * sr / (float) N / noiseBinHz;
            const int i0 = std::clamp ((int) pos, 0, (int) noiseDensity.size() - 1), i1 = std::min (i0 + 1, (int) noiseDensity.size() - 1);
            const float d = noiseDensity[(size_t) i0] + (pos - (float) i0) * (noiseDensity[(size_t) i1] - noiseDensity[(size_t) i0]);
            binsPower[(size_t) k] = std::max (0.0f, d) * sr * (float) N * 0.25f;
        }
    }
    denoise.loadProfile (binsPower);
}

// ================================================================================================
//  Analyse
// ================================================================================================
void MJ7Processor::startAnalysis()
{
    if (state.load() == computing) return;
    stopThread (5000);
    if (hasAnalysis()) undoAnalysis();                 // repart des reglages du style, pas d'une analyse precedente
    const auto& preset = factoryPresets()[(size_t) std::clamp (presetIndex, 0, (int) factoryPresets().size() - 1)];
    analysisTarget = preset.target; analysisRatio = v (c1_ratio); analysisMakeup = v (c1_makeup);
    captureValid = false; waited = 0; capturePos.store (0); summary.clear();
    state.store (waiting);
}

void MJ7Processor::run()
{
    result = analyseVoice (capture.data(), captureLen, sr, analysisTarget, analysisRatio, analysisMakeup);
    resultReady.store (true);
}

void MJ7Processor::timerCallback()
{
    if (state.load() == computing && ! isThreadRunning() && ! resultReady.load()) { captureValid = true; startThread(); }
    if (resultReady.exchange (false)) applyAnalysis();
    if (intensityDirty.exchange (false) && hasAnalysis()) applyIntensity();
    if (latencyNow.load() != getLatencySamples()) setLatencySamples (latencyNow.load());
    if (state.load() == failed && summary.isEmpty())
        summary.add (U8 ("Aucune voix détectée. Lancez la lecture de la piste (ou chantez), puis appuyez de nouveau."));
}

void MJ7Processor::parameterChanged (const juce::String&, float) { intensityDirty.store (true); }

void MJ7Processor::setParam (int pid, float realValue)
{
    if (auto* p = apvts.getParameter (paramDef (pid).id))
    {
        const float norm = p->convertTo0to1 (realValue);
        if (std::abs (norm - p->getValue()) < 1.0e-6f) return;
        p->beginChangeGesture(); p->setValueNotifyingHost (norm); p->endChangeGesture();
    }
}

void MJ7Processor::applyAnalysis()
{
    static const char* noteNames[12] = { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    const auto& r = result;
    adjustments.clear(); summary.clear();
    if (! r.valid)
    {
        captureValid = false; state.store (failed);
        summary.add (U8 ("Voix trop faible ou trop courte pour être analysée. Vérifiez le niveau de la piste et recommencez."));
        return;
    }

    auto add = [this] (int pid, float target) { adjustments.push_back ({ pid, v (pid), target }); };
    add (in_gain, r.inGain); add (hpf_freq, r.hpf); add (gate_thresh, r.gateThresh);
    static const int eqIds[4][3] = { { eq1_f, eq1_g, eq1_q }, { eq2_f, eq2_g, eq2_q }, { eq3_f, eq3_g, eq3_q }, { eq4_f, eq4_g, eq4_q } };
    for (int b = 0; b < 4; ++b)
    {
        if (r.eqG[b] >= -0.05f) continue;                       // bande inutile : on laisse le reglage du style
        setParam (eqIds[b][0], r.eqF[b]); setParam (eqIds[b][2], r.eqQ[b]);
        adjustments.push_back ({ eqIds[b][1], 0.0f, r.eqG[b] });
    }
    if (r.sibilance) setParam (ds_freq, r.dsFreq);
    add (ds_thresh, r.dsThresh); add (c1_thresh, r.c1Thresh); add (c2_thresh, r.c2Thresh);
    add (tone_low_g, clampf (v (tone_low_g) + r.toneLow, -12.0f, 12.0f));
    add (tone_pres_g, clampf (v (tone_pres_g) + r.tonePres, -12.0f, 12.0f));
    add (tone_air_g, clampf (v (tone_air_g) + r.toneAir, -12.0f, 12.0f));
    add (dq1_thresh, r.dq1Thresh); add (dq2_thresh, r.dq2Thresh);

    noiseDensity = r.noiseDensity; noiseBinHz = r.noiseBinHz; pushNoiseProfile();

    const bool keyOk = r.key >= 0 && r.keyConfidence >= 0.6f;
    if (keyOk)
    {
        prevKey = (int) v (tune_key); prevScale = (int) v (tune_scale);
        setParam (tune_key, (float) r.key); setParam (tune_scale, (float) r.scale);
    }
    applyIntensity();

    auto db = [] (float x) { return (x > 0.0f ? "+" : "") + juce::String (x, 1) + " dB"; };
    summary.add (U8 ("Niveau : crête ") + juce::String (r.peakDb, 1) + " dB, moyenne " + juce::String (r.rmsDb, 1) + U8 (" dB → gain d'entrée ") + db (r.inGain));
    summary.add (r.noiseKnown ? U8 ("Bruit de fond : ") + juce::String (juce::roundToInt (r.noiseDb)) + U8 (" dB → gate à ") + juce::String (juce::roundToInt (r.gateThresh)) + " dB" + (r.noiseDensity.empty() ? juce::String() : U8 (", profil appris"))
                              : U8 ("Bruit de fond : pas de silence dans l'extrait, gate laissé très bas"));
    if (r.lowHz > 0.0f)
        summary.add (U8 ("Tessiture : note la plus grave ") + juce::String (juce::roundToInt (r.lowHz)) + U8 (" Hz → coupe-bas ") + juce::String (juce::roundToInt (r.hpf)) + " Hz");
    if (r.key >= 0)
        summary.add (U8 ("Tonalité probable : ") + U8 (noteNames[r.key]) + (r.scale == 1 ? " majeur" : " mineur")
                     + (keyOk ? U8 (" (appliquée à l'autotune, à vérifier)") : U8 (" (incertaine, non appliquée)")));
    else
        summary.add (U8 ("Tonalité : pas assez de notes tenues pour la deviner, réglez-la à la main"));
    if (r.numRes > 0)
    {
        juce::String s = U8 ("Résonances atténuées : ");
        for (int i = 0; i < r.numRes; ++i) s << (i > 0 ? ", " : "") << juce::roundToInt (r.resFreq[i]) << " Hz (-" << juce::String (r.resCut[i], 1) << " dB)";
        summary.add (s);
    }
    else summary.add (U8 ("Résonances : rien de gênant repéré"));
    summary.add (r.sibilance ? U8 ("Sifflantes : centrées vers ") + juce::String (r.sibFreq / 1000.0f, 1) + U8 (" kHz → de-esser calé dessus")
                             : U8 ("Sifflantes : peu marquées, de-esser laissé discret"));
    summary.add (U8 ("Couleur : grave ") + db (r.toneLow) + U8 (", présence ") + db (r.tonePres) + ", air " + db (r.toneAir));
    summary.add (U8 ("Compression : seuils à ") + juce::String (r.c1Thresh, 1) + " dB et " + juce::String (r.c2Thresh, 1) + " dB");
    state.store (done);
}

void MJ7Processor::applyIntensity()
{
    const float t = v (intensity) * 0.01f;
    for (const auto& a : adjustments) setParam (a.pid, a.base + t * (a.target - a.base));
}

void MJ7Processor::undoAnalysis()
{
    for (const auto& a : adjustments) setParam (a.pid, a.base);
    if (prevKey >= 0) { setParam (tune_key, (float) prevKey); setParam (tune_scale, (float) prevScale); }
    prevKey = prevScale = -1;
    adjustments.clear(); summary.clear();
    noiseDensity.clear(); noiseBinHz = 0.0f; pushNoiseProfile();
    if (state.load() == done || state.load() == failed) state.store (idle);
}

// ================================================================================================
//  Prereglages et etat
// ================================================================================================
void MJ7Processor::loadFactoryPreset (int index)
{
    const auto& presets = factoryPresets();
    index = std::clamp (index, 0, (int) presets.size() - 1);
    const bool reanalyse = captureValid && state.load() == done;   // on garde l'analyse : elle sera refaite pour le nouveau style
    adjustments.clear(); summary.clear(); prevKey = prevScale = -1;

    std::vector<float> values ((size_t) kNumParams);
    for (int pid = 0; pid < kNumParams; ++pid) values[(size_t) pid] = paramDef (pid).def;
    for (const auto& [pid, value] : presets[(size_t) index].values) values[(size_t) pid] = value;
    for (int pid = 0; pid < kNumParams; ++pid)
        if (pid != bypass && pid != intensity && pid != mode && pid != nr_on && pid != nr_amount) setParam (pid, values[(size_t) pid]);   // reglages de session, pas de style
    presetIndex = index;

    if (reanalyse)
    {
        analysisTarget = presets[(size_t) index].target; analysisRatio = values[c1_ratio]; analysisMakeup = values[c1_makeup];
        state.store (computing);
    }
    else if (state.load() == done || state.load() == failed) state.store (idle);
}

void MJ7Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = apvts.copyState();
    writeExtras (tree);
    if (auto xml = tree.createXml()) copyXmlToBinary (*xml, dest);
}

void MJ7Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            presetIndex = std::clamp ((int) tree.getProperty ("presetIndex", 0), 0, (int) factoryPresets().size() - 1);
            adjustments.clear(); summary.clear(); prevKey = prevScale = -1;
            if (state.load() == done || state.load() == failed) state.store (idle);
            apvts.replaceState (tree);
            readExtras (tree);
        }
}

/** Ce qui n'est pas un parametre : preset choisi, analyse en cours (pour que INTENSITE reste actif), profil de bruit. */
void MJ7Processor::writeExtras (juce::ValueTree& tree) const
{
    tree.setProperty ("presetIndex", presetIndex, nullptr);
    juce::String adj;
    for (const auto& a : adjustments) adj << paramDef (a.pid).id << "=" << a.base << "," << a.target << ";";
    tree.setProperty ("analysis", adj, nullptr);
    tree.setProperty ("analysisSummary", summary.joinIntoString ("\n"), nullptr);
    tree.setProperty ("prevKey", prevKey, nullptr); tree.setProperty ("prevScale", prevScale, nullptr);
    tree.setProperty ("noiseBinHz", noiseBinHz, nullptr);
    tree.setProperty ("noiseDensity", noiseDensity.empty() ? juce::String()
                      : juce::MemoryBlock (noiseDensity.data(), noiseDensity.size() * sizeof (float)).toBase64Encoding(), nullptr);
}

void MJ7Processor::readExtras (const juce::ValueTree& tree)
{
    for (const auto& item : juce::StringArray::fromTokens (tree.getProperty ("analysis").toString(), ";", ""))
    {
        const auto id = item.upToFirstOccurrenceOf ("=", false, false);
        const auto vals = juce::StringArray::fromTokens (item.fromFirstOccurrenceOf ("=", false, false), ",", "");
        for (int pid = 0; pid < kNumParams && vals.size() == 2; ++pid)
            if (id == paramDef (pid).id) { adjustments.push_back ({ pid, vals[0].getFloatValue(), vals[1].getFloatValue() }); break; }
    }
    if (! adjustments.empty())
    {
        summary = juce::StringArray::fromLines (tree.getProperty ("analysisSummary").toString());
        summary.removeEmptyStrings();
        prevKey = tree.getProperty ("prevKey", -1); prevScale = tree.getProperty ("prevScale", -1);
        state.store (done);
    }
    noiseDensity.clear(); noiseBinHz = (float) tree.getProperty ("noiseBinHz", 0.0f);
    juce::MemoryBlock block;
    if (noiseBinHz > 0.0f && block.fromBase64Encoding (tree.getProperty ("noiseDensity").toString()) && block.getSize() >= sizeof (float))
    {
        noiseDensity.resize (block.getSize() / sizeof (float));
        std::memcpy (noiseDensity.data(), block.getData(), noiseDensity.size() * sizeof (float));
    }
    pushNoiseProfile();
}

juce::File MJ7Processor::presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MJ7 Vocal Chain").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

bool MJ7Processor::savePresetFile (const juce::File& file)
{
    auto tree = apvts.copyState();
    writeExtras (tree);
    if (auto xml = tree.createXml()) return xml->writeTo (file);
    return false;
}

bool MJ7Processor::loadPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            juce::MemoryBlock block; copyXmlToBinary (*xml, block);
            setStateInformation (block.getData(), (int) block.getSize());
            return true;
        }
    return false;
}

void MJ7Processor::toggleAB()
{
    abState[abIndex] = apvts.copyState();
    abIndex ^= 1;
    if (abState[abIndex].isValid())
    {
        adjustments.clear(); prevKey = prevScale = -1;
        apvts.replaceState (abState[abIndex].createCopy());
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MJ7Processor(); }
