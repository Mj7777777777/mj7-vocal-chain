// MJ7 Vocal Chain - liste unique des parametres, des modules et des prereglages d'usine.
#pragma once
#include "dsp/Analyzer.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace mj7
{
inline juce::String U8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// X (identifiant, nom, type, min, max, defaut, centre de course (0 = lineaire), unite, choix)
// Les identifiants ne doivent JAMAIS changer : les projets FL Studio et l'automation en dependent.
#define MJ7_PARAMS(X) \
    X (in_gain,       "Gain entrée",      tF, -24, 24, 0, 0, "dB", "") \
    X (phase,         "Phase inversée",   tB, 0, 1, 0, 0, "", "") \
    X (intensity,     "Intensité",        tF, 0, 100, 100, 0, "%", "") \
    X (hpf_freq,      "Coupe-bas",        tF, 20, 300, 80, 100, "Hz", "") \
    X (gate_on,       "Gate actif",       tB, 0, 1, 1, 0, "", "") \
    X (gate_thresh,   "Gate seuil",       tF, -80, -20, -60, 0, "dB", "") \
    X (gate_range,    "Gate plage",       tF, 0, 40, 8, 0, "dB", "") \
    X (gate_release,  "Gate release",     tF, 20, 500, 150, 150, "ms", "") \
    X (tune_on,       "Tune actif",       tB, 0, 1, 1, 0, "", "") \
    X (tune_key,      "Tonalité",         tC, 0, 11, 0, 0, "", "Do|Do#|Ré|Ré#|Mi|Fa|Fa#|Sol|Sol#|La|La#|Si") \
    X (tune_scale,    "Gamme",            tC, 0, 4, 1, 0, "", "Chromatique|Majeure|Mineure|Penta majeure|Penta mineure") \
    X (tune_speed,    "Tune vitesse",     tF, 0, 400, 20, 60, "ms", "") \
    X (tune_amount,   "Tune quantité",    tF, 0, 100, 100, 0, "%", "") \
    X (tune_transpose,"Transposition",    tI, -12, 12, 0, 0, "dt", "") \
    X (eq_on,         "EQ actif",         tB, 0, 1, 1, 0, "", "") \
    X (eq1_f,         "EQ 1 fréq",        tF, 60, 8000, 300, 700, "Hz", "") \
    X (eq1_g,         "EQ 1 gain",        tF, -18, 6, 0, 0, "dB", "") \
    X (eq1_q,         "EQ 1 Q",           tF, 0.3, 10, 1.0, 2, "", "") \
    X (eq2_f,         "EQ 2 fréq",        tF, 60, 8000, 500, 700, "Hz", "") \
    X (eq2_g,         "EQ 2 gain",        tF, -18, 6, 0, 0, "dB", "") \
    X (eq2_q,         "EQ 2 Q",           tF, 0.3, 10, 4.0, 2, "", "") \
    X (eq3_f,         "EQ 3 fréq",        tF, 60, 8000, 1000, 700, "Hz", "") \
    X (eq3_g,         "EQ 3 gain",        tF, -18, 6, 0, 0, "dB", "") \
    X (eq3_q,         "EQ 3 Q",           tF, 0.3, 10, 4.0, 2, "", "") \
    X (eq4_f,         "EQ 4 fréq",        tF, 60, 8000, 3000, 700, "Hz", "") \
    X (eq4_g,         "EQ 4 gain",        tF, -18, 6, 0, 0, "dB", "") \
    X (eq4_q,         "EQ 4 Q",           tF, 0.3, 10, 4.0, 2, "", "") \
    X (eq_hicut,      "Coupe-haut",       tF, 2000, 20000, 20000, 8000, "Hz", "") \
    X (ds_on,         "De-esser actif",   tB, 0, 1, 1, 0, "", "") \
    X (ds_freq,       "De-esser fréq",    tF, 3000, 10000, 6500, 0, "Hz", "") \
    X (ds_thresh,     "De-esser seuil",   tF, -60, 0, -30, 0, "dB", "") \
    X (ds_range,      "De-esser plage",   tF, 0, 18, 8, 0, "dB", "") \
    X (c1_on,         "Comp 1 actif",     tB, 0, 1, 1, 0, "", "") \
    X (c1_thresh,     "Comp 1 seuil",     tF, -40, 0, -18, 0, "dB", "") \
    X (c1_ratio,      "Comp 1 ratio",     tF, 2, 20, 4, 6, ":1", "") \
    X (c1_attack,     "Comp 1 attaque",   tF, 0.1, 30, 3, 5, "ms", "") \
    X (c1_release,    "Comp 1 release",   tF, 20, 500, 80, 120, "ms", "") \
    X (c1_makeup,     "Comp 1 gain",      tF, 0, 24, 3, 0, "dB", "") \
    X (c1_mix,        "Comp 1 mix",       tF, 0, 100, 100, 0, "%", "") \
    X (c2_on,         "Comp 2 actif",     tB, 0, 1, 1, 0, "", "") \
    X (c2_thresh,     "Comp 2 seuil",     tF, -40, 0, -20, 0, "dB", "") \
    X (c2_makeup,     "Comp 2 gain",      tF, 0, 24, 2, 0, "dB", "") \
    X (c2_mix,        "Comp 2 mix",       tF, 0, 100, 100, 0, "%", "") \
    X (macro_comp,    "Compress +",       tF, 0, 100, 0, 0, "%", "") \
    X (tone_on,       "Ton actif",        tB, 0, 1, 1, 0, "", "") \
    X (tone_low_f,    "Grave fréq",       tF, 80, 400, 180, 0, "Hz", "") \
    X (tone_low_g,    "Grave gain",       tF, -12, 12, 0, 0, "dB", "") \
    X (tone_mid_f,    "Médium fréq",      tF, 300, 3000, 800, 900, "Hz", "") \
    X (tone_mid_g,    "Médium gain",      tF, -12, 12, 0, 0, "dB", "") \
    X (tone_pres_f,   "Présence fréq",    tF, 1500, 8000, 4000, 0, "Hz", "") \
    X (tone_pres_g,   "Présence gain",    tF, -12, 12, 2, 0, "dB", "") \
    X (tone_air_f,    "Air fréq",         tF, 6000, 16000, 11000, 0, "Hz", "") \
    X (tone_air_g,    "Air gain",         tF, -12, 12, 3, 0, "dB", "") \
    X (sat_on,        "Saturation active",tB, 0, 1, 1, 0, "", "") \
    X (sat_mode,      "Saturation type",  tC, 0, 4, 0, 0, "", "Lampe|Bande|Écrêtage|Fuzz|Bits") \
    X (sat_drive,     "Saturation drive", tF, 0, 36, 6, 0, "dB", "") \
    X (sat_tone,      "Saturation couleur",tF, 1000, 20000, 14000, 6000, "Hz", "") \
    X (sat_mix,       "Saturation mix",   tF, 0, 100, 30, 0, "%", "") \
    X (dq_on,         "EQ dyn actif",     tB, 0, 1, 1, 0, "", "") \
    X (dq1_f,         "EQ dyn 1 fréq",    tF, 1500, 6000, 3200, 0, "Hz", "") \
    X (dq1_thresh,    "EQ dyn 1 seuil",   tF, -60, 0, -26, 0, "dB", "") \
    X (dq1_range,     "EQ dyn 1 plage",   tF, 0, 12, 4, 0, "dB", "") \
    X (dq2_f,         "EQ dyn 2 fréq",    tF, 5000, 12000, 8000, 0, "Hz", "") \
    X (dq2_thresh,    "EQ dyn 2 seuil",   tF, -60, 0, -32, 0, "dB", "") \
    X (dq2_range,     "EQ dyn 2 plage",   tF, 0, 12, 4, 0, "dB", "") \
    X (flt_on,        "Filtre actif",     tB, 0, 1, 0, 0, "", "") \
    X (flt_hp,        "Filtre passe-haut",tF, 20, 2000, 20, 300, "Hz", "") \
    X (flt_lp,        "Filtre passe-bas", tF, 500, 20000, 20000, 4000, "Hz", "") \
    X (flt_res,       "Filtre résonance", tF, 0.5, 8, 0.7, 2, "", "") \
    X (par_on,        "Punch actif",      tB, 0, 1, 1, 0, "", "") \
    X (par_mix,       "Punch mix",        tF, 0, 100, 15, 0, "%", "") \
    X (dbl_on,        "Doubleur actif",   tB, 0, 1, 1, 0, "", "") \
    X (dbl_detune,    "Doubleur désaccord",tF, 0, 25, 9, 0, "ct", "") \
    X (dbl_delay,     "Doubleur retard",  tF, 0, 40, 14, 0, "ms", "") \
    X (dbl_mix,       "Doubleur mix",     tF, 0, 100, 20, 0, "%", "") \
    X (dly_on,        "Delay actif",      tB, 0, 1, 1, 0, "", "") \
    X (dly_sync,      "Delay synchro",    tC, 0, 5, 2, 0, "", "Libre|1/2|1/4|1/8 pointé|1/8|1/16") \
    X (dly_time,      "Delay temps",      tF, 10, 2000, 350, 400, "ms", "") \
    X (dly_fb,        "Delay feedback",   tF, 0, 95, 30, 0, "%", "") \
    X (dly_hp,        "Delay coupe-bas",  tF, 20, 1000, 300, 250, "Hz", "") \
    X (dly_lp,        "Delay coupe-haut", tF, 1000, 20000, 5000, 5000, "Hz", "") \
    X (dly_ping,      "Delay ping-pong",  tB, 0, 1, 0, 0, "", "") \
    X (dly_always,    "Delay continu",    tB, 0, 1, 1, 0, "", "") \
    X (dly_throw,     "Throw delay",      tB, 0, 1, 0, 0, "", "") \
    X (dly_mix,       "Delay mix",        tF, 0, 100, 12, 0, "%", "") \
    X (rev_on,        "Réverbe active",   tB, 0, 1, 1, 0, "", "") \
    X (rev_type,      "Réverbe type",     tC, 0, 2, 0, 0, "", "Plate|Hall|Room") \
    X (rev_predelay,  "Réverbe pré-délai",tF, 0, 150, 30, 0, "ms", "") \
    X (rev_decay,     "Réverbe durée",    tF, 0.3, 10, 1.8, 2, "s", "") \
    X (rev_damp,      "Réverbe amorti",   tF, 1000, 16000, 7000, 5000, "Hz", "") \
    X (rev_hp,        "Réverbe coupe-bas",tF, 20, 800, 250, 200, "Hz", "") \
    X (rev_always,    "Réverbe continue", tB, 0, 1, 1, 0, "", "") \
    X (rev_throw,     "Throw réverbe",    tB, 0, 1, 0, 0, "", "") \
    X (rev_mix,       "Réverbe mix",      tF, 0, 100, 14, 0, "%", "") \
    X (duck_on,       "Ducking actif",    tB, 0, 1, 1, 0, "", "") \
    X (duck_amt,      "Ducking quantité", tF, 0, 18, 6, 0, "dB", "") \
    X (duck_rel,      "Ducking release",  tF, 50, 1000, 250, 300, "ms", "") \
    X (out_gain,      "Gain sortie",      tF, -24, 24, 0, 0, "dB", "") \
    X (lim_on,        "Limiteur actif",   tB, 0, 1, 1, 0, "", "") \
    X (lim_ceiling,   "Limiteur plafond", tF, -12, 0, -1, 0, "dB", "") \
    X (mode,          "Mode",             tC, 0, 1, 0, 0, "", "Mix|Tracking") \
    X (nr_on,         "Réduction bruit active", tB, 0, 1, 0, 0, "", "") \
    X (nr_amount,     "Réduction bruit quantité", tF, 0, 100, 50, 0, "%", "") \
    X (tune_formant,  "Formants",         tF, -12, 12, 0, 0, "dt", "") \
    X (harm_on,       "Harmonie active",  tB, 0, 1, 0, 0, "", "") \
    X (h1_int,        "Harmonie voix 1",  tC, 0, 8, 6, 0, "", "Off|Octave basse|Sixte basse|Quarte basse|Tierce basse|Unisson|Tierce haute|Quinte haute|Octave haute") \
    X (h2_int,        "Harmonie voix 2",  tC, 0, 8, 7, 0, "", "Off|Octave basse|Sixte basse|Quarte basse|Tierce basse|Unisson|Tierce haute|Quinte haute|Octave haute") \
    X (h3_int,        "Harmonie voix 3",  tC, 0, 8, 1, 0, "", "Off|Octave basse|Sixte basse|Quarte basse|Tierce basse|Unisson|Tierce haute|Quinte haute|Octave haute") \
    X (h4_int,        "Harmonie voix 4",  tC, 0, 8, 0, 0, "", "Off|Octave basse|Sixte basse|Quarte basse|Tierce basse|Unisson|Tierce haute|Quinte haute|Octave haute") \
    X (harm_width,    "Harmonie largeur", tF, 0, 100, 80, 0, "%", "") \
    X (harm_formant,  "Harmonie formants",tF, -12, 12, 0, 0, "dt", "") \
    X (harm_stack,    "Harmonie stack",   tB, 0, 1, 0, 0, "", "") \
    X (harm_mix,      "Harmonie mix",     tF, 0, 100, 60, 0, "%", "") \
    X (bypass,        "Bypass",           tB, 0, 1, 0, 0, "", "")

enum PID
{
#define X(id, name, type, mn, mx, def, centre, unit, choices) id,
    MJ7_PARAMS (X)
#undef X
    kNumParams
};

enum class PType { tF, tI, tB, tC };

struct ParamDef { const char* id; const char* name; PType type; float min, max, def, centre; const char* unit; const char* choices; };

inline const ParamDef& paramDef (int pid)
{
    static const ParamDef defs[] = {
#define X(id, name, type, mn, mx, def, centre, unit, choices) { #id, name, PType::type, (float) (mn), (float) (mx), (float) (def), (float) (centre), unit, choices },
        MJ7_PARAMS (X)
#undef X
    };
    return defs[pid];
}

// ----------------------------------------------------------------------------------------------
enum Family { Correction = 0, Dynamique, Couleur, Espace, Neutre };

struct ModuleDef
{
    const char* name;          // nom court (tuile)
    const char* title;         // nom complet (panneau)
    const char* help;          // une phrase d'explication
    Family family;
    int bypass;                // PID du bouton marche/arret, ou -1
    int meter;                 // index de l'indicateur de reduction de gain, ou -1
    std::vector<int> params;
};

enum Meter { mGate = 0, mDeEss, mComp1, mComp2, mDynEq, mPunch, mDuck, mLimit, mNoise, kNumMeters };

inline const std::vector<ModuleDef>& modules()
{
    static const std::vector<ModuleDef> m = {
        { "Entrée", "Entrée et nettoyage", "Niveau d'entrée, coupe-bas et gate doux qui atténue le bruit entre les phrases.", Correction, gate_on, mGate,
          { in_gain, hpf_freq, gate_thresh, gate_range, gate_release, phase } },
        { "Bruit", "Réduction de bruit", "Retire le souffle et le bruit de fond constant. ANALYSER apprend le bruit dans les silences. Mode Mix seulement (ajoute 21 ms).", Correction, nr_on, mNoise,
          { nr_amount } },
        { "Tune", "Autotune", "Recale la voix sur la gamme. Vitesse 0 ms = effet dur. Formants : timbre plus grave ou plus aigu, sans changer la note.", Correction, tune_on, -1,
          { tune_key, tune_scale, tune_speed, tune_amount, tune_transpose, tune_formant } },
        { "Harmonie", "Harmoniseur", "Jusqu'à 4 voix calées sur la gamme de l'autotune. Stack : voix figées sur la note, rendu vocodeur.", Couleur, harm_on, -1,
          { h1_int, h2_int, h3_int, h4_int, harm_width, harm_formant, harm_mix, harm_stack } },
        { "EQ", "EQ soustractif", "Enlève ce qui gêne avant la compression : boue, résonances, dureté.", Correction, eq_on, -1,
          { eq1_f, eq1_g, eq1_q, eq2_f, eq2_g, eq2_q, eq3_f, eq3_g, eq3_q, eq4_f, eq4_g, eq4_q, eq_hicut } },
        { "De-ess", "De-esser", "Calme les sifflantes (s, ch, t) seulement quand elles dépassent le seuil.", Dynamique, ds_on, mDeEss,
          { ds_freq, ds_thresh, ds_range } },
        { "Comp 1", "Compresseur 1 (rapide)", "Attrape les crêtes. Visez 3 à 6 dB de réduction.", Dynamique, c1_on, mComp1,
          { c1_thresh, c1_ratio, c1_attack, c1_release, c1_makeup, c1_mix } },
        { "Comp 2", "Compresseur 2 (lent)", "Lisse et densifie la voix. Visez 2 à 3 dB de réduction.", Dynamique, c2_on, mComp2,
          { c2_thresh, c2_makeup, c2_mix, macro_comp } },
        { "Ton", "EQ tonal", "Donne la couleur : corps, médiums, présence et air.", Couleur, tone_on, -1,
          { tone_low_f, tone_low_g, tone_mid_f, tone_mid_g, tone_pres_f, tone_pres_g, tone_air_f, tone_air_g } },
        { "Satur.", "Saturation", "Ajoute des harmoniques : chaleur à faible dose, distorsion à forte dose.", Couleur, sat_on, -1,
          { sat_mode, sat_drive, sat_tone, sat_mix } },
        { "EQ dyn", "EQ dynamique", "Rattrape la dureté (2 à 5 kHz) et les aigus agressifs créés par l'EQ et la saturation.", Dynamique, dq_on, mDynEq,
          { dq1_f, dq1_thresh, dq1_range, dq2_f, dq2_thresh, dq2_range } },
        { "Filtre", "Filtre créatif", "Passe-haut et passe-bas résonants. Effet téléphone : 400 Hz et 3 500 Hz.", Couleur, flt_on, -1,
          { flt_hp, flt_lp, flt_res } },
        { "Punch", "Compression parallèle", "Mélange une copie très compressée pour épaissir sans écraser.", Dynamique, par_on, mPunch,
          { par_mix } },
        { "Double", "Doubleur", "Deux copies légèrement désaccordées à gauche et à droite pour élargir.", Espace, dbl_on, -1,
          { dbl_detune, dbl_delay, dbl_mix } },
        { "Delay", "Delay", "Écho calé sur le tempo. Désactivez « continu » pour n'envoyer que sur le bouton THROW.", Espace, dly_on, -1,
          { dly_sync, dly_time, dly_fb, dly_hp, dly_lp, dly_mix, dly_ping, dly_always } },
        { "Réverbe", "Réverbe", "Espace autour de la voix. Désactivez « continue » pour n'envoyer que sur le bouton THROW.", Espace, rev_on, -1,
          { rev_type, rev_predelay, rev_decay, rev_damp, rev_hp, rev_mix, rev_always } },
        { "Duck", "Ducking des effets", "Baisse le delay et la réverbe pendant que la voix chante, pour rester intelligible.", Espace, duck_on, mDuck,
          { duck_amt, duck_rel } },
        { "Sortie", "Sortie", "Niveau final et limiteur de sécurité.", Neutre, lim_on, mLimit,
          { out_gain, lim_ceiling } },
    };
    return m;
}

// ----------------------------------------------------------------------------------------------
// Premier preset de la serie V3 (Afro, Amapiano, Pop, House) : sert a afficher un titre dans la liste.
inline constexpr int kV3PresetStart = 12;

struct FactoryPreset
{
    const char* name;
    StyleTarget target;                               // cible utilisee par le bouton ANALYSER
    std::vector<std::pair<int, float>> values;        // ecarts par rapport aux valeurs par defaut
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    //                                            grave  bas-méd  prés.   air   GR1  GR2
    static const std::vector<FactoryPreset> p = {
        { "Lead mélodique aérien", { 2.0f, 3.0f, -7.0f, -13.0f, 5.0f, 2.5f },
          { { tune_speed, 25 }, { tone_air_g, 5 }, { tone_pres_g, 2 }, { sat_mode, 0 }, { sat_drive, 5 }, { sat_mix, 20 },
            { dbl_mix, 22 }, { dly_sync, 2 }, { dly_fb, 25 }, { dly_mix, 10 }, { rev_type, 0 }, { rev_decay, 2.2f }, { rev_mix, 18 } } },
        { "Lead trap autotune dur", { 3.0f, 4.0f, -6.0f, -15.0f, 6.0f, 3.0f },
          { { tune_speed, 0 }, { c1_ratio, 6 }, { sat_mode, 1 }, { sat_drive, 10 }, { sat_mix, 40 }, { tone_air_g, 3 },
            { dly_sync, 2 }, { dly_fb, 40 }, { dly_mix, 14 }, { rev_type, 1 }, { rev_predelay, 40 }, { rev_decay, 3.5f }, { rev_mix, 20 } } },
        { "Lead sombre pitché", { 4.0f, 5.0f, -10.0f, -21.0f, 6.0f, 3.0f },
          { { tune_speed, 0 }, { tune_transpose, -2 }, { tune_formant, -2 }, { flt_on, 1 }, { flt_lp, 6500 }, { sat_mode, 0 }, { sat_drive, 14 }, { sat_mix, 50 },
            { tone_air_g, -2 }, { tone_pres_g, 0 }, { tone_low_g, 2 }, { rev_type, 1 }, { rev_decay, 4.5f }, { rev_damp, 4500 }, { rev_mix, 24 },
            { dly_sync, 2 }, { dly_fb, 45 }, { dly_lp, 3000 }, { dly_mix, 14 } } },
        { "Rap sec frontal", { 4.0f, 4.0f, -6.0f, -17.0f, 6.0f, 3.0f },
          { { tune_on, 0 }, { c1_ratio, 6 }, { sat_mode, 1 }, { sat_drive, 4 }, { sat_mix, 20 }, { par_mix, 25 }, { dbl_on, 0 },
            { dly_sync, 0 }, { dly_time, 110 }, { dly_fb, 5 }, { dly_mix, 6 }, { rev_type, 2 }, { rev_decay, 0.6f }, { rev_mix, 5 } } },
        { "Autotune saturé mélancolique", { 3.0f, 4.0f, -8.0f, -17.0f, 6.0f, 3.0f },
          { { tune_speed, 0 }, { sat_mode, 0 }, { sat_drive, 16 }, { sat_mix, 55 }, { tone_air_g, 1 },
            { dly_sync, 3 }, { dly_mix, 10 }, { rev_type, 0 }, { rev_decay, 2.8f }, { rev_mix, 22 } } },
        { "Agressif écrêté", { 2.0f, 3.0f, -5.0f, -16.0f, 7.0f, 3.0f },
          { { tune_on, 0 }, { c1_ratio, 8 }, { sat_mode, 2 }, { sat_drive, 22 }, { sat_mix, 70 }, { tone_mid_f, 1500 }, { tone_mid_g, 3 },
            { par_mix, 30 }, { dbl_on, 0 }, { dly_on, 0 }, { rev_type, 2 }, { rev_decay, 0.5f }, { rev_mix, 4 } } },
        { "Adlibs larges", { 0.0f, 2.0f, -6.0f, -16.0f, 6.0f, 3.0f },
          { { tune_speed, 0 }, { hpf_freq, 150 }, { flt_on, 1 }, { flt_hp, 400 }, { flt_lp, 3500 }, { dbl_detune, 14 }, { dbl_mix, 55 },
            { dly_sync, 4 }, { dly_ping, 1 }, { dly_fb, 45 }, { dly_mix, 28 }, { rev_type, 1 }, { rev_decay, 3.0f }, { rev_mix, 30 } } },
        { "Backs et doubles", { 0.0f, 2.0f, -7.0f, -15.0f, 7.0f, 3.0f },
          { { tune_speed, 10 }, { hpf_freq, 140 }, { c1_ratio, 6 }, { tone_low_g, -3 }, { tone_air_g, 2 }, { dbl_detune, 12 }, { dbl_mix, 60 },
            { dly_on, 0 }, { rev_type, 0 }, { rev_mix, 16 } } },
        { "Lead + throws de delay", { 3.0f, 4.0f, -7.0f, -15.0f, 5.0f, 2.5f },
          { { tune_speed, 10 }, { dly_always, 0 }, { dly_sync, 2 }, { dly_fb, 50 }, { dly_mix, 45 }, { rev_decay, 2.0f }, { rev_mix, 14 } } },
        { "Harmonies empilées", { 3.0f, 4.0f, -7.0f, -15.0f, 6.0f, 3.0f },
          { { tune_speed, 0 }, { harm_on, 1 }, { harm_stack, 1 }, { h1_int, 6 }, { h2_int, 7 }, { h3_int, 1 }, { h4_int, 4 }, { harm_mix, 70 },
            { sat_drive, 8 }, { sat_mix, 30 }, { dbl_mix, 10 }, { dly_mix, 8 }, { rev_type, 0 }, { rev_decay, 2.6f }, { rev_mix, 20 } } },
        { "Lead + chœur naturel", { 3.0f, 4.0f, -7.0f, -14.0f, 5.0f, 2.5f },
          { { tune_speed, 20 }, { harm_on, 1 }, { harm_stack, 0 }, { h1_int, 6 }, { h2_int, 4 }, { h3_int, 0 }, { h4_int, 0 }, { harm_mix, 40 },
            { tone_air_g, 4 }, { rev_decay, 2.2f }, { rev_mix, 18 } } },
        { "Neutre (point de départ)", { 3.0f, 4.0f, -7.0f, -16.0f, 5.0f, 2.5f }, {} },

        // --- V3 : styles Afro, Amapiano, Pop, House (ajoutes a la fin pour garder les anciens projets intacts) ---
        { "Afro lead chaud", { 3.5f, 4.0f, -7.0f, -14.5f, 5.0f, 2.5f },
          { { tune_speed, 15 }, { tune_amount, 90 }, { hpf_freq, 100 }, { tone_low_g, 1.5f }, { tone_mid_f, 1200 }, { tone_mid_g, 1 },
            { tone_pres_g, 2 }, { tone_air_g, 4 }, { sat_mode, 1 }, { sat_drive, 6 }, { sat_mix, 25 }, { dbl_detune, 8 }, { dbl_mix, 16 },
            { dly_sync, 3 }, { dly_fb, 30 }, { dly_lp, 6000 }, { dly_mix, 12 }, { rev_type, 0 }, { rev_decay, 1.6f }, { rev_predelay, 25 }, { rev_mix, 16 } } },
        { "Afro autotune + chœur", { 3.0f, 4.0f, -6.5f, -14.0f, 6.0f, 3.0f },
          { { tune_speed, 0 }, { hpf_freq, 110 }, { c1_ratio, 5 }, { sat_mode, 1 }, { sat_drive, 9 }, { sat_mix, 35 }, { tone_air_g, 4 },
            { harm_on, 1 }, { harm_stack, 0 }, { h1_int, 6 }, { h2_int, 1 }, { h3_int, 0 }, { h4_int, 0 }, { harm_width, 90 }, { harm_mix, 45 },
            { dly_sync, 4 }, { dly_ping, 1 }, { dly_fb, 35 }, { dly_mix, 12 }, { rev_type, 1 }, { rev_decay, 2.4f }, { rev_mix, 18 } } },
        { "Amapiano lead aérien", { 2.0f, 3.0f, -7.0f, -13.0f, 5.0f, 2.5f },
          { { tune_speed, 20 }, { tune_amount, 85 }, { hpf_freq, 130 }, { tone_low_g, -2 }, { tone_pres_g, 2 }, { tone_air_g, 5 },
            { sat_mode, 0 }, { sat_drive, 5 }, { sat_mix, 20 }, { dbl_mix, 18 },
            { dly_sync, 2 }, { dly_ping, 1 }, { dly_fb, 42 }, { dly_hp, 400 }, { dly_lp, 5000 }, { dly_mix, 18 },
            { rev_type, 1 }, { rev_decay, 3.2f }, { rev_predelay, 45 }, { rev_hp, 350 }, { rev_mix, 24 }, { duck_amt, 8 } } },
        { "Amapiano chant de groupe", { 1.5f, 3.0f, -7.0f, -14.0f, 6.0f, 3.0f },
          { { tune_speed, 5 }, { hpf_freq, 140 }, { tone_low_g, -2 }, { tone_air_g, 3 },
            { harm_on, 1 }, { harm_stack, 0 }, { h1_int, 1 }, { h2_int, 5 }, { h3_int, 6 }, { h4_int, 0 }, { harm_width, 100 }, { harm_mix, 60 },
            { dbl_detune, 14 }, { dbl_mix, 35 }, { dly_sync, 2 }, { dly_fb, 30 }, { dly_mix, 12 }, { rev_type, 1 }, { rev_decay, 2.8f }, { rev_mix, 24 } } },
        { "Pop lead brillant", { 3.0f, 3.5f, -6.0f, -12.5f, 5.0f, 2.5f },
          { { tune_speed, 30 }, { tune_amount, 75 }, { c1_ratio, 4 }, { ds_range, 10 }, { tone_pres_g, 3 }, { tone_air_g, 6 },
            { sat_mode, 0 }, { sat_drive, 4 }, { sat_mix, 18 }, { dbl_mix, 14 },
            { dly_sync, 4 }, { dly_fb, 22 }, { dly_mix, 8 }, { rev_type, 0 }, { rev_decay, 1.8f }, { rev_mix, 14 } } },
        { "Pop intime / R&B doux", { 4.0f, 4.0f, -8.0f, -14.0f, 4.5f, 2.5f },
          { { tune_speed, 40 }, { tune_amount, 60 }, { c1_ratio, 3 }, { c1_attack, 6 }, { tone_low_g, 2 }, { tone_pres_g, 1 }, { tone_air_g, 4 },
            { sat_mode, 1 }, { sat_drive, 5 }, { sat_mix, 22 }, { dbl_mix, 10 },
            { dly_sync, 2 }, { dly_fb, 28 }, { dly_mix, 10 }, { rev_type, 0 }, { rev_decay, 2.4f }, { rev_mix, 20 } } },
        { "House diva", { 2.0f, 3.0f, -6.0f, -12.0f, 5.5f, 3.0f },
          { { tune_speed, 15 }, { hpf_freq, 130 }, { c1_ratio, 5 }, { tone_low_g, -2 }, { tone_pres_g, 3 }, { tone_air_g, 6 },
            { sat_mode, 0 }, { sat_drive, 7 }, { sat_mix, 25 }, { dbl_mix, 20 },
            { dly_sync, 3 }, { dly_ping, 1 }, { dly_fb, 45 }, { dly_hp, 400 }, { dly_mix, 18 },
            { rev_type, 1 }, { rev_decay, 3.6f }, { rev_predelay, 50 }, { rev_hp, 350 }, { rev_mix, 26 }, { duck_amt, 8 } } },
        { "House vocal filtré", { 0.0f, 2.0f, -6.0f, -16.0f, 6.0f, 3.0f },
          { { tune_speed, 0 }, { hpf_freq, 150 }, { flt_on, 1 }, { flt_hp, 300 }, { flt_lp, 6000 }, { flt_res, 1.5f },
            { sat_mode, 1 }, { sat_drive, 10 }, { sat_mix, 35 }, { dbl_mix, 25 },
            { dly_sync, 5 }, { dly_ping, 1 }, { dly_fb, 40 }, { dly_mix, 22 }, { rev_type, 1 }, { rev_decay, 2.6f }, { rev_mix, 22 } } },
    };
    return p;
}
} // namespace mj7
