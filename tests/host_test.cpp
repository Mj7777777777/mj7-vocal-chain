// Test d'integration : charge le plugin comme le ferait un hote, sans carte son.
//   cmake -B build -DMJ7_BUILD_TESTS=ON && cmake --build build --target MJ7HostTest
#include "PluginEditor.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  ECHEC: " << msg << std::endl; } } while (0)

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

/** Voix de synthese : melodie en La mineur, sifflantes et silences. */
static juce::AudioBuffer<float> makeVoice (double sr, double seconds, float amp)
{
    const float notes[] = { 220.0f, 261.63f, 329.63f, 293.66f, 246.94f, 220.0f, 196.0f, 220.0f, 261.63f, 349.23f, 329.63f, 220.0f };
    juce::AudioBuffer<float> buf (2, (int) (seconds * sr)); buf.clear();
    juce::Random rnd (3); double phase = 0.0; mj7::Biquad f1, f2, s; f1.setPeak (600, 4, 12, (float) sr); f2.setPeak (2600, 12, 14, (float) sr); s.setBandpass (7000, 1.2f, (float) sr);
    auto* d = buf.getWritePointer (0); int pos = (int) (0.3 * sr), n = 0;
    while (pos < buf.getNumSamples())
    {
        const float f0 = notes[n++ % 12];
        for (int i = 0; i < (int) (0.55 * sr) && pos < buf.getNumSamples(); ++i, ++pos)
        {
            phase += juce::MathConstants<double>::twoPi * f0 / sr; float v = 0.0f;
            for (int h = 1; h <= 12; ++h) v += std::sin ((float) (phase * h)) / (float) h;
            d[pos] = amp * 0.5f * v;
        }
        for (int i = 0; i < (int) (0.12 * sr) && pos < buf.getNumSamples(); ++i, ++pos) d[pos] = amp * s.process (rnd.nextFloat() * 2.0f - 1.0f);
        pos += (int) (0.33 * sr);
    }
    for (int i = 0; i < buf.getNumSamples(); ++i) d[i] = f2.process (f1.process (d[i])) + 0.0001f * (rnd.nextFloat() * 2.0f - 1.0f);
    buf.copyFrom (1, 0, buf, 0, 0, buf.getNumSamples());
    return buf;
}

/** Fait passer un signal dans le plugin par blocs de taille aleatoire (comme FL Studio). */
static juce::AudioBuffer<float> run (MJ7Processor& p, const juce::AudioBuffer<float>& in, juce::Random& rnd, int maxBlock = 1024)
{
    juce::AudioBuffer<float> out (2, in.getNumSamples()); juce::MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples();)
    {
        const int n = juce::jmin (in.getNumSamples() - pos, 1 + rnd.nextInt (maxBlock));
        juce::AudioBuffer<float> block (2, n);
        block.copyFrom (0, 0, in, 0, pos, n); block.copyFrom (1, 0, in, 1, pos, n);
        p.processBlock (block, midi);
        out.copyFrom (0, pos, block, 0, 0, n); out.copyFrom (1, pos, block, 1, 0, n);
        pos += n;
    }
    return out;
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    juce::Random rnd (11);

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::cout << "== " << sr << " Hz ==" << std::endl;
        MJ7Processor p; p.setRateAndBufferSizeDetails (sr, 512); p.prepareToPlay (sr, 512);
        const auto voice = makeVoice (sr, 3.0, 0.2f);

        // chaque prereglage : rien d'infini, plafond du limiteur respecte
        for (int i = 0; i < (int) mj7::factoryPresets().size(); ++i)
        {
            for (const auto& [pid, value] : mj7::factoryPresets()[(size_t) i].values)
                CHECK (value >= mj7::paramDef (pid).min && value <= mj7::paramDef (pid).max, "valeur de preset hors limites");
            p.loadFactoryPreset (i);
            const auto out = run (p, voice, rnd);
            const float peak = out.getMagnitude (0, out.getNumSamples()), rms = out.getRMSLevel (0, 0, out.getNumSamples());
            std::cout << "  " << juce::String (juce::CharPointer_UTF8 (mj7::factoryPresets()[(size_t) i].name)).paddedRight (' ', 32)
                      << " crete " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dB, RMS " << juce::String (juce::Decibels::gainToDecibels (rms), 1) << " dB" << std::endl;
            CHECK (allFinite (out), "valeur non finie");
            CHECK (peak <= juce::Decibels::decibelsToGain (-1.0f) * 1.001f, "plafond du limiteur depasse");
            CHECK (rms > 0.001f, "sortie muette");
        }

        // latence declaree = latence mesuree (tous modules coupes, puis en bypass)
        for (int pass = 0; pass < 4; ++pass)
        {
            static const char* passName[4] = { "(mix) ", "(bypass) ", "(tracking) ", "(mix + reduction de bruit) " };
            p.loadFactoryPreset (mj7::kV3PresetStart - 1);            // Neutre
            for (const auto& m : mj7::modules()) p.apvts.getParameter (mj7::paramDef (m.bypass).id)->setValueNotifyingHost (0.0f);
            p.apvts.getParameter ("hpf_freq")->setValueNotifyingHost (0.0f);
            p.apvts.getParameter ("bypass")->setValueNotifyingHost (pass == 1 ? 1.0f : 0.0f);
            p.apvts.getParameter ("mode")->setValueNotifyingHost (pass == 2 ? 1.0f : 0.0f);
            p.apvts.getParameter ("nr_on")->setValueNotifyingHost (pass == 3 ? 1.0f : 0.0f);
            p.apvts.getParameter ("nr_amount")->setValueNotifyingHost (0.0f);
            p.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> imp (2, (int) sr); imp.clear();
            for (int i = 0; i < 64; ++i) { const float w = 0.25f * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 64.0f)); imp.setSample (0, 20000 + i, w); imp.setSample (1, 20000 + i, w); }
            const auto out = run (p, imp, rnd);
            int best = 0; for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, best))) best = i;
            std::cout << "  latence " << passName[pass] << "declaree " << p.getLatencySamples() << ", mesuree " << best - 20032 << std::endl;
            CHECK (std::abs ((best - 20032) - p.getLatencySamples()) <= 1, "latence declaree differente de la latence mesuree");
            p.apvts.getParameter ("bypass")->setValueNotifyingHost (0.0f);
            p.apvts.getParameter ("mode")->setValueNotifyingHost (0.0f); p.apvts.getParameter ("nr_on")->setValueNotifyingHost (0.0f);
        }
        // changement de mode en cours de lecture : la latence declaree a l'hote doit suivre
        {
            p.prepareToPlay (sr, 512); const int mixLat = p.getLatencySamples();
            p.apvts.getParameter ("mode")->setValueNotifyingHost (1.0f);
            const auto out = run (p, voice, rnd); pump (300);
            std::cout << "  latence Mix " << juce::String (1000.0 * mixLat / sr, 1) << " ms -> Tracking " << juce::String (1000.0 * p.getLatencySamples() / sr, 1) << " ms" << std::endl;
            CHECK (p.getLatencySamples() < mixLat / 2 && p.getLatencySamples() <= (int) (0.0051 * sr) && allFinite (out), "le mode Tracking ne reduit pas la latence");
            p.loadFactoryPreset (10);                                    // harmonies empilees, en mode Tracking
            const auto harm = run (p, voice, rnd);
            CHECK (allFinite (harm) && harm.getMagnitude (0, harm.getNumSamples()) <= juce::Decibels::decibelsToGain (-1.0f) * 1.001f, "harmonies en mode Tracking");
            CHECK (p.apvts.getRawParameterValue ("mode")->load() > 0.5f, "changer de style ne doit pas quitter le mode Tracking");
            p.apvts.getParameter ("mode")->setValueNotifyingHost (0.0f); run (p, voice, rnd); pump (300);
            CHECK (p.getLatencySamples() == mixLat, "retour au mode Mix : latence differente");
        }

        // sauvegarde / rechargement de l'etat
        {
            p.loadFactoryPreset (1);
            p.apvts.getParameter ("rev_mix")->setValueNotifyingHost (0.77f);
            juce::MemoryBlock state; p.getStateInformation (state);
            MJ7Processor q; q.setStateInformation (state.getData(), (int) state.getSize());
            bool same = true;
            for (int pid = 0; pid < mj7::kNumParams; ++pid)
                if (std::abs (p.apvts.getParameter (mj7::paramDef (pid).id)->getValue() - q.apvts.getParameter (mj7::paramDef (pid).id)->getValue()) > 1.0e-5f) same = false;
            CHECK (same && q.currentPreset() == 1, "l'etat recharge differe de l'etat sauvegarde");
        }
    }

    // --- bouton ANALYSER, de bout en bout ---
    {
        const double sr = 48000.0;
        MJ7Processor p; p.setRateAndBufferSizeDetails (sr, 512); p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (0);
        const float gainBefore = p.apvts.getRawParameterValue ("in_gain")->load();
        p.startAnalysis();
        CHECK (p.analysisState() == MJ7Processor::waiting, "l'analyse devrait attendre la voix");
        run (p, makeVoice (sr, 13.0, 0.05f), rnd);
        CHECK (p.analysisState() == MJ7Processor::computing, "la capture devrait etre terminee");
        for (int i = 0; i < 100 && p.analysisState() != MJ7Processor::done && p.analysisState() != MJ7Processor::failed; ++i) pump (100);
        CHECK (p.analysisState() == MJ7Processor::done, "l'analyse n'a pas abouti");
        std::cout << "== resume de l'analyse ==" << std::endl;
        for (const auto& line : p.summary) std::cout << "  " << line << std::endl;
        const float gainFull = p.apvts.getRawParameterValue ("in_gain")->load();
        CHECK (gainFull > gainBefore + 6.0f, "le gain d'entree aurait du monter");
        CHECK ((int) p.apvts.getRawParameterValue ("tune_key")->load() == 9, "tonalite attendue : La");

        p.apvts.getParameter ("intensity")->setValueNotifyingHost (0.5f); pump (300);
        const float gainHalf = p.apvts.getRawParameterValue ("in_gain")->load();
        CHECK (std::abs (gainHalf - 0.5f * (gainBefore + gainFull)) < 0.2f, "le bouton Intensite ne dose pas les corrections");
        p.apvts.getParameter ("intensity")->setValueNotifyingHost (1.0f); pump (300);

        // projet recharge : l'analyse et le bouton Intensite doivent rester actifs
        {
            juce::MemoryBlock saved; p.getStateInformation (saved);
            MJ7Processor q; q.setStateInformation (saved.getData(), (int) saved.getSize()); q.setRateAndBufferSizeDetails (sr, 512); q.prepareToPlay (sr, 512);
            CHECK (q.hasAnalysis() && q.analysisState() == MJ7Processor::done && q.summary.size() == p.summary.size(), "l'analyse n'est pas restauree avec le projet");
            q.apvts.getParameter ("intensity")->setValueNotifyingHost (0.0f); pump (300);
            CHECK (std::abs (q.apvts.getRawParameterValue ("in_gain")->load() - gainBefore) < 0.2f, "Intensite inactif apres rechargement");
            q.apvts.getParameter ("nr_on")->setValueNotifyingHost (1.0f); q.apvts.getParameter ("nr_amount")->setValueNotifyingHost (0.7f);
            q.apvts.getParameter ("intensity")->setValueNotifyingHost (1.0f); pump (300); q.prepareToPlay (sr, 512);
            const auto den = run (q, makeVoice (sr, 4.0, 0.05f), rnd);
            CHECK (allFinite (den) && den.getRMSLevel (0, 0, den.getNumSamples()) > 0.003f, "reduction de bruit avec profil appris : sortie anormale");
        }

        const auto out = run (p, makeVoice (sr, 4.0, 0.05f), rnd);
        std::cout << "  apres analyse : RMS de sortie " << juce::String (juce::Decibels::gainToDecibels (out.getRMSLevel (0, 0, out.getNumSamples())), 1) << " dB" << std::endl;
        CHECK (allFinite (out), "valeur non finie apres analyse");

        // captures d'ecran de l'interface
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto snap = [&] (const juce::String& name)
        {
            pump (400);
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::File f = outDir.getChildFile (name); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        };
        run (p, makeVoice (sr, 1.0, 0.05f), rnd);
        snap ("ui_traite.png");
        auto* view = ed->getChildComponent (0);
        auto select = [view] (int index)                      // index 0 = Essentiel, puis les modules dans l'ordre
        {
            mj7ui::ChainTile* found = nullptr; int tile = 0;
            for (auto* c : view->getChildren())
                if (auto* t = dynamic_cast<mj7ui::ChainTile*> (c))
                    if (tile++ == index) found = t;
            if (found != nullptr && found->onSelect) found->onSelect();
        };
        select (4); snap ("ui_module.png");                    // Harmonie
        select (5); snap ("ui_eq.png");                        // EQ + spectre

        p.undoAnalysis(); pump (200);
        CHECK (std::abs (p.apvts.getRawParameterValue ("in_gain")->load() - gainBefore) < 0.05f, "Annuler l'analyse ne restaure pas les reglages");
        select (0);
        snap ("ui_depart.png");
    }

    std::cout << (failures == 0 ? "\nTOUS LES TESTS PASSENT" : "\nECHECS : " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
