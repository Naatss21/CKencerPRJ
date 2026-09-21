#include <jni.h>
#include <oboe/Oboe.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <cstring>
#include <vector>
#include <atomic>
#include <cmath>
#include <algorithm>


// ---------------------------------------------------------------------------
// Callback audio : joue un sample chargé en mémoire (au lieu d'une sinusoïde)
// ---------------------------------------------------------------------------
class SamplePlayer : public oboe::AudioStreamCallback {
public:
    static constexpr int32_t kMaxSteps = 32;

    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *stream,
            void *audioData,
            int32_t numFrames) override {

        auto *outputBuffer = static_cast<float *>(audioData);
        int32_t channels = stream->getChannelCount();

        for (int32_t frame = 0; frame < numFrames; frame++) {
            if (patternModeEnabled && framesPerStep > 0) {
                // Mode séquenceur pas à pas : on avance case par case
                currentStepFrameCounter++;
                if (currentStepFrameCounter >= framesPerStep) {
                    currentStepFrameCounter = 0;
                    int32_t total = numSteps.load();
                    if (total > 0) {
                        int32_t next = (currentStepIndex.load() + 1) % total;
                        currentStepIndex = next;
                        if (next >= 0 && next < kMaxSteps && stepActive[next].load()) {
                            position = 0; // la case est active : on rejoue le son depuis le début
                        }
                    }
                }
            } else if (loopEnabled && framesPerBeat > 0 && channels > 0) {
                // Mode boucle simple à la vitesse du BPM
                int32_t framesPlayed = position / channels;
                if (framesPlayed >= framesPerBeat) {
                    position = 0;
                }
            }

            for (int32_t ch = 0; ch < channels; ch++) {
                if (position < (int32_t) samples.size()) {
                    outputBuffer[frame * channels + ch] = samples[position];
                    position++;
                } else {
                    outputBuffer[frame * channels + ch] = 0.0f;
                }
            }
        }

        return oboe::DataCallbackResult::Continue;
    }

    std::vector<float> samples;          // audio en float [-1, 1], entrelacé par canal
    std::atomic<int32_t> position{0};
    int32_t sampleRate = 48000;
    int32_t channelCount = 1;

    // --- Boucle simple (métronome) ---
    std::atomic<bool> loopEnabled{false};
    std::atomic<int32_t> framesPerBeat{0};

    // --- Séquenceur pas à pas ("pattern") ---
    std::atomic<bool> patternModeEnabled{false};
    std::atomic<int32_t> stepsPerGroup{3};        // 3 = ternaire, 4 = binaire
    std::atomic<int32_t> numSteps{6};             // nombre total de cases (ex: 2 groupes de 3 = 6)
    std::atomic<int32_t> framesPerStep{0};
    std::atomic<int32_t> currentStepFrameCounter{0};
    std::atomic<int32_t> currentStepIndex{-1};
    std::atomic<bool> stepActive[kMaxSteps];      // false par défaut (objet statique = zero-init)
};
static oboe::AudioStream *stream = nullptr;
static SamplePlayer callback;
static std::atomic<int32_t> currentBpm{120};

// Recalcule le nombre de frames par battement à partir du BPM et du sample rate
// du son actuellement chargé. À appeler après un changement de BPM ou un nouveau
// chargement de son. PAS LE BON COMMENTAIRE
static void updateFramesPerBeat() {
    int32_t bpm = currentBpm.load();
    if (bpm > 0 && callback.sampleRate > 0) {
        callback.framesPerBeat = (int32_t) std::round(callback.sampleRate * 60.0 / bpm);
    }
    int32_t spg = callback.stepsPerGroup.load();
    if (spg > 0 && callback.framesPerBeat > 0) {
        callback.framesPerStep = callback.framesPerBeat / spg;
    }
}
// ---------------------------------------------------------------------------
// Parseur WAV minimaliste (PCM 16 bits et 32 bits, mono ou stéréo)
// ---------------------------------------------------------------------------
struct WavHeader {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint16_t formatTag = 0;   // 1 = PCM entier, 3 = IEEE float (WAVE_FORMAT_IEEE_FLOAT)
};

static bool parseWav(const uint8_t *data, size_t size, WavHeader &header,
                     const uint8_t *&pcmData, size_t &pcmSize) {
    if (size < 44) return false;
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return false;

    size_t pos = 12;
    bool foundFmt = false, foundData = false;

    while (pos + 8 <= size) {
        char chunkId[4];
        memcpy(chunkId, data + pos, 4);
        uint32_t chunkSize;
        memcpy(&chunkSize, data + pos + 4, 4);

        if (memcmp(chunkId, "fmt ", 4) == 0 && pos + 8 + 16 <= size) {
            memcpy(&header.formatTag, data + pos + 8 + 0, 2);
            memcpy(&header.channels, data + pos + 8 + 2, 2);
            memcpy(&header.sampleRate, data + pos + 8 + 4, 4);
            memcpy(&header.bitsPerSample, data + pos + 8 + 14, 2);
            foundFmt = true;
        } else if (memcmp(chunkId, "data", 4) == 0) {
            pcmData = data + pos + 8;
            pcmSize = std::min((size_t) chunkSize, size - (pos + 8));
            foundData = true;
        }

        pos += 8 + chunkSize + (chunkSize % 2); // chunks alignés sur 2 octets
        if (foundFmt && foundData) break;
    }

    return foundFmt && foundData;
}

// ---------------------------------------------------------------------------
// JNI : chargement d'un fichier depuis assets/sounds/
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_ckencer2_NativeAudio_loadSound(JNIEnv *env, jclass /* clazz */,
                                                 jobject assetManager, jstring fileName) {
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    if (!mgr) return JNI_FALSE;

    const char *path = env->GetStringUTFChars(fileName, nullptr);
    AAsset *asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
    env->ReleaseStringUTFChars(fileName, path);
    if (!asset) return JNI_FALSE;

    off_t assetLength = AAsset_getLength(asset);
    const auto *assetBuffer = static_cast<const uint8_t *>(AAsset_getBuffer(asset));

    WavHeader header;
    const uint8_t *pcmData = nullptr;
    size_t pcmSize = 0;
    bool ok = assetBuffer != nullptr &&
              parseWav(assetBuffer, (size_t) assetLength, header, pcmData, pcmSize);

    bool isPcm16 = header.formatTag == 1 && header.bitsPerSample == 16;
    bool isPcm32 = header.formatTag == 1 && header.bitsPerSample == 32;
    bool isFloat32 = header.formatTag == 3 && header.bitsPerSample == 32;

    if (ok && header.channels > 0 && (isPcm16 || isPcm32 || isFloat32)) {
        std::vector<float> floatSamples;

        if (isPcm16) {
            size_t sampleCount = pcmSize / 2;
            floatSamples.resize(sampleCount);
            const auto *pcm16 = reinterpret_cast<const int16_t *>(pcmData);
            for (size_t i = 0; i < sampleCount; i++) {
                floatSamples[i] = pcm16[i] / 32768.0f;
            }

        } else if (isPcm32) {
                size_t sampleCount = pcmSize / 4;
                floatSamples.resize(sampleCount);
                const auto *pcm32i = reinterpret_cast<const int32_t *>(pcmData);
                for (size_t i = 0; i < sampleCount; i++) {
                    floatSamples[i] = pcm32i[i] / 2147483648.0f; // 2^31
                }
        } else { // isFloat32 : déjà normalisé en [-1, 1], simple copie
            size_t sampleCount = pcmSize / 4;
            floatSamples.resize(sampleCount);
            const auto *pcm32f = reinterpret_cast<const float *>(pcmData);
            std::memcpy(floatSamples.data(), pcm32f, sampleCount * sizeof(float));
        }

        // On coupe un éventuel stream en cours avant de remplacer le sample chargé
        if (stream) {
            stream->requestStop();
            stream->close();
            stream = nullptr;
        }

        callback.samples = std::move(floatSamples);
        callback.position = 0;
        callback.sampleRate = (int32_t) header.sampleRate;
        callback.channelCount = header.channels;
        updateFramesPerBeat(); // le sample rate a pu changer, on remet à jour l'intervalle
    } else {
        ok = false; // format non supporté (PCM 16/32 bits entiers et float 32 bits sont gérés)
    }

    AAsset_close(asset);
    return ok ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------------------
// JNI : lecture / arrêt
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_startAudio(JNIEnv *env, jclass /* clazz */) {
    if (callback.samples.empty()) {
        return; // aucun son chargé, rien à jouer
    }

    if (stream) {
        // Un stream est déjà ouvert : on relance juste la lecture depuis le début.
        callback.position = 0;
        return;
    }

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setFormat(oboe::AudioFormat::Float);
    builder.setChannelCount(callback.channelCount);
    builder.setSampleRate(callback.sampleRate);
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
    builder.setCallback(&callback);

    callback.position = 0;
    builder.openStream(&stream);
    if (stream) {
        stream->requestStart();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_stopAudio(JNIEnv *env, jclass /* clazz */) {
    if (stream) {
        stream->requestStop();
        stream->close();
        stream = nullptr;
    }
}

// ---------------------------------------------------------------------------
// JNI : métronome (BPM 30 à 200)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setBpm(JNIEnv *env, jclass /* clazz */, jint bpm) {
    currentBpm = bpm;
    updateFramesPerBeat();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setLoopEnabled(JNIEnv *env,jclass /* clazz */,
                                                      jboolean enabled) {
    callback.loopEnabled = enabled;
    if (enabled) {
        callback.position = 0; // redémarre le sample dès le prochain callback audio
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setPatternModeEnabled(JNIEnv *env, jclass /* clazz */,
                                                            jboolean enabled) {
    callback.patternModeEnabled = enabled;
    if (enabled) {
        callback.currentStepIndex = -1;
        callback.currentStepFrameCounter = 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setPatternSubdivision(JNIEnv *env, jclass /* clazz */,
                                                            jint stepsPerGroup) {
    if (stepsPerGroup > 0) {
        callback.stepsPerGroup = stepsPerGroup;
        updateFramesPerBeat(); // recalcule framesPerStep avec la nouvelle subdivision
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setPatternLength(JNIEnv *env, jclass /* clazz */,
                                                       jint steps) {
    if (steps > 0 && steps <= SamplePlayer::kMaxSteps) {
        callback.numSteps = steps;
        callback.currentStepIndex = -1;
        callback.currentStepFrameCounter = 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setStepActive(JNIEnv *env, jclass /* clazz */,
                                                    jint index, jboolean active) {
    if (index >= 0 && index < SamplePlayer::kMaxSteps) {
        callback.stepActive[index] = active;
    }
}


// =====================================================================================
// MOTEUR MULTI-PISTES (séquenceur) — vient s'ajouter au moteur mono-piste existant
// ci-dessus. Les deux coexistent pour l'instant ; l'interface (écran séquenceur)
// sera branchée sur celui-ci à l'étape suivante.
// =====================================================================================

static constexpr int32_t kSequencerMaxTracks = 4;
static constexpr int32_t kSequencerMaxSteps = 32;
static constexpr int32_t kEngineSampleRate = 44100; // toutes les pistes doivent être à ce sample rate

struct Track {
    std::vector<float> samples;                          // audio entrelacé, mono ou stéréo
    std::atomic<int32_t> position{0};
    std::atomic<int32_t> channelCount{1};
    std::atomic<bool> assigned{false};                    // un son a-t-il été chargé sur cette piste ?
    std::atomic<bool> stepActive[kSequencerMaxSteps];     // grille de cette piste (false par défaut)
};



// ---------------------------------------------------------------------------
// Métronome : clic synthétisé (pas besoin de fichier .wav), avec un clic
// "accentué" un peu plus aigu sur le premier temps de chaque mesure.
// ---------------------------------------------------------------------------
static std::vector<float> generateClick(float frequencyHz, float durationSec, float amplitude) {
    constexpr float kPi = 3.14159265358979323846f;
    int32_t sampleCount = (int32_t) (kEngineSampleRate * durationSec);
    std::vector<float> buf(sampleCount);
    for (int32_t i = 0; i < sampleCount; i++) {
        float t = (float) i / (float) kEngineSampleRate;
        float envelope = std::exp(-t * 45.0f); // décroissance rapide -> effet "tak" sec
        buf[i] = amplitude * envelope * std::sin(2.0f * kPi * frequencyHz * t);
    }
    return buf;
}

static const std::vector<float> kMetronomeNormalClick = generateClick(1500.0f, 0.03f, 0.5f);
static const std::vector<float> kMetronomeAccentClick = generateClick(2200.0f, 0.03f, 0.6f);



class MultiTrackEngine : public oboe::AudioStreamCallback {
public:
    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *outStream,
            void *audioData,
            int32_t numFrames) override {

        auto *outputBuffer = static_cast<float *>(audioData);
        int32_t outChannels = outStream->getChannelCount();

        for (int32_t frame = 0; frame < numFrames; frame++) {
            // Avance du séquenceur : un pas toutes les "framesPerStep" frames écoulées.
            if (isPlaying && framesPerStep > 0) {
                stepFrameCounter++;
                if (stepFrameCounter >= framesPerStep) {
                    stepFrameCounter = 0;
                    int32_t total = numSteps.load();
                    if (total > 0) {
                        int32_t next = (currentStepIndex.load() + 1) % total;
                        currentStepIndex = next;
                        for (int32_t t = 0; t < kSequencerMaxTracks; t++) {
                            if (tracks[t].assigned && next < kSequencerMaxSteps &&
                                tracks[t].stepActive[next].load()) {
                                tracks[t].position = 0; // ce pas est actif : on rejoue cette piste
                            }
                        }
                    }
                }
            }

        }
    }

    // Avance du métronome : un clic toutes les "metronomeFramesPerBeat" frames,
    // indépendamment du séquenceur (une pulsation par noire).
    if (metronomeEnabled && metronomeFramesPerBeat > 0) {
        metronomeFrameCounter++;
        if (metronomeFrameCounter >= metronomeFramesPerBeat) {
            metronomeFrameCounter = 0;
            int32_t beatsPerMeasure = metronomeBeatsPerMeasure.load();
            if (beatsPerMeasure < 1) beatsPerMeasure = 1;
            int32_t nextBeat = (metronomeCurrentBeatInMeasure.load() + 1) % beatsPerMeasure;
            metronomeCurrentBeatInMeasure = nextBeat;
            metronomeAccent = (nextBeat == 0); // 1er temps de la mesure = clic accentué
            metronomePosition = 0; // redéclenche le clic
        }
    }

    // Mixage : on additionne toutes les pistes actives sur ce frame.
    float left = 0.0f, right = 0.0f;
    for (int32_t t = 0; t < kSequencerMaxTracks; t++) {
        Track &track = tracks[t];
        if (!track.assigned) continue;

        int32_t pos = track.position.load();
        if (pos >= (int32_t) track.samples.size()) continue; // piste terminée : silence

        int32_t trackChannels = track.channelCount.load();
        if (trackChannels >= 2 && pos + 1 < (int32_t) track.samples.size()) {
            left += track.samples[pos];
            right += track.samples[pos + 1];
            track.position = pos + 2;
        } else {
            float value = track.samples[pos];
            left += value;
            right += value;
            track.position = pos + 1;
        }
    }

    // On ajoute le clic du métronome au mixage s'il est en cours de lecture.
    if (metronomeEnabled) {
        const std::vector<float> &clickBuffer =
                metronomeAccent.load() ? kMetronomeAccentClick : kMetronomeNormalClick;
        int32_t clickPos = metronomePosition.load();
        if (clickPos < (int32_t) clickBuffer.size()) {
            float v = clickBuffer[clickPos];
            left += v;
            right += v;
            metronomePosition = clickPos + 1;
        }
    }

    // Léger écrêtage pour éviter la saturation quand plusieurs pistes s'additionnent.

            // Léger écrêtage pour éviter la saturation quand plusieurs pistes s'additionnent.
            left = std::max(-1.0f, std::min(1.0f, left));
            right = std::max(-1.0f, std::min(1.0f, right));

            for (int32_t ch = 0; ch < outChannels; ch++) {
                outputBuffer[frame * outChannels + ch] = (ch == 0) ? left : right;
            }

        return oboe::DataCallbackResult::Continue;
    }

    Track tracks[kSequencerMaxTracks];
    std::atomic<bool> isPlaying{false};
    std::atomic<int32_t> numSteps{5};
    std::atomic<int32_t> framesPerStep{0};
    std::atomic<int32_t> stepFrameCounter{0};
    std::atomic<int32_t> currentStepIndex{-1};

    // --- Métronome ---
    std::atomic<bool> metronomeEnabled{false};
    std::atomic<int32_t> metronomeFramesPerBeat{0};
    std::atomic<int32_t> metronomeFrameCounter{0};
    std::atomic<int32_t> metronomeBeatsPerMeasure{4};
    std::atomic<int32_t> metronomeCurrentBeatInMeasure{-1};
    std::atomic<int32_t> metronomePosition{0};
    std::atomic<bool> metronomeAccent{false};
};

static MultiTrackEngine engine;
static std::atomic<int32_t> sequencerBpm{120};
static std::atomic<int32_t> sequencerStepsPerGroup{4}; // 4 = binaire, 3 = ternaire (provisoire, remplacé par la signature à l'étape suivante)

static void updateSequencerTiming() {
    int32_t bpm = sequencerBpm.load();
    int32_t spg = sequencerStepsPerGroup.load();
    int32_t framesPerBeat = 0;
    if (bpm > 0) {
        framesPerBeat = (int32_t) std::round(kEngineSampleRate * 60.0 / bpm);
        engine.metronomeFramesPerBeat = framesPerBeat;
    }
    if (bpm > 0 && spg > 0) {
        engine.framesPerStep = framesPerBeat / spg;
    }
}

// ---------------------------------------------------------------------------
// JNI : chargement d'un son sur une piste précise (0 à kSequencerMaxTracks - 1)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_ckencer2_NativeAudio_loadTrackSound(JNIEnv *env, jclass /* clazz */,
                                                     jint trackIndex,
                                                     jobject assetManager, jstring fileName) {
    if (trackIndex < 0 || trackIndex >= kSequencerMaxTracks) return JNI_FALSE;

    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    if (!mgr) return JNI_FALSE;

    const char *path = env->GetStringUTFChars(fileName, nullptr);
    AAsset *asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
    env->ReleaseStringUTFChars(fileName, path);
    if (!asset) return JNI_FALSE;

    off_t assetLength = AAsset_getLength(asset);
    const auto *assetBuffer = static_cast<const uint8_t *>(AAsset_getBuffer(asset));

    WavHeader header;
    const uint8_t *pcmData = nullptr;
    size_t pcmSize = 0;
    bool ok = assetBuffer != nullptr &&
              parseWav(assetBuffer, (size_t) assetLength, header, pcmData, pcmSize);

    bool isPcm16 = header.formatTag == 1 && header.bitsPerSample == 16;
    bool isPcm32 = header.formatTag == 1 && header.bitsPerSample == 32;
    bool isFloat32 = header.formatTag == 3 && header.bitsPerSample == 32;

    if (ok && header.channels > 0 && (isPcm16 || isPcm32 || isFloat32)) {
        std::vector<float> floatSamples;

        if (isPcm16) {
            size_t sampleCount = pcmSize / 2;
            floatSamples.resize(sampleCount);
            const auto *pcm16 = reinterpret_cast<const int16_t *>(pcmData);
            for (size_t i = 0; i < sampleCount; i++) {
                floatSamples[i] = pcm16[i] / 32768.0f;
            }
        } else if (isPcm32) {
            size_t sampleCount = pcmSize / 4;
            floatSamples.resize(sampleCount);
            const auto *pcm32i = reinterpret_cast<const int32_t *>(pcmData);
            for (size_t i = 0; i < sampleCount; i++) {
                floatSamples[i] = pcm32i[i] / 2147483648.0f;
            }
        } else { // isFloat32
            size_t sampleCount = pcmSize / 4;
            floatSamples.resize(sampleCount);
            const auto *pcm32f = reinterpret_cast<const float *>(pcmData);
            std::memcpy(floatSamples.data(), pcm32f, sampleCount * sizeof(float));
        }

        Track &track = engine.tracks[trackIndex];
        track.samples = std::move(floatSamples);
        track.channelCount = header.channels;
        track.position = (int32_t) track.samples.size(); // silencieuse tant qu'aucun pas ne la déclenche
        track.assigned = true;
        // NB : on suppose que le fichier est déjà à kEngineSampleRate (44100 Hz), comme tes assets
        // actuels. Un fichier à un autre sample rate jouera légèrement décalé en hauteur/vitesse
        // (le ré-échantillonnage par piste n'est pas géré à cette étape).
    } else {
        ok = false;
    }

    AAsset_close(asset);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setTrackStepActive(JNIEnv *env, jclass /* clazz */,
                                                         jint trackIndex, jint stepIndex,
                                                         jboolean active) {
    if (trackIndex < 0 || trackIndex >= kSequencerMaxTracks) return;
    if (stepIndex < 0 || stepIndex >= kSequencerMaxSteps) return;
    engine.tracks[trackIndex].stepActive[stepIndex] = active;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerBpm(JNIEnv *env, jclass /* clazz */, jint bpm) {
    sequencerBpm = bpm;
    updateSequencerTiming();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerSubdivision(JNIEnv *env, jclass /* clazz */,
                                                              jint stepsPerGroup) {
    if (stepsPerGroup > 0) {
        sequencerStepsPerGroup = stepsPerGroup;
        updateSequencerTiming();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerLength(JNIEnv *env, jclass /* clazz */,
                                                         jint steps) {
    if (steps > 0 && steps <= kSequencerMaxSteps) {
        engine.numSteps = steps;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerPlaying(JNIEnv *env, jclass /* clazz */,
                                                          jboolean playing) {
    engine.isPlaying = playing;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_restartSequencer(JNIEnv *env, jclass /* clazz */) {
    engine.currentStepIndex = -1;
    engine.stepFrameCounter = engine.framesPerStep.load(); // déclenche le pas 0 dès le prochain frame
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_startSequencerStream(JNIEnv *env, jclass /* clazz */) {
    if (stream) return; // déjà ouvert (par ce moteur ou par l'ancien, un seul flux à la fois)

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setFormat(oboe::AudioFormat::Float);
    builder.setChannelCount(2);
    builder.setSampleRate(kEngineSampleRate);
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
    builder.setCallback(&engine);

    builder.openStream(&stream);
    if (stream) {
        stream->requestStart();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_stopSequencerStream(JNIEnv *env, jclass /* clazz */) {
    if (stream) {
        stream->requestStop();
        stream->close();
        stream = nullptr;
    }
    engine.isPlaying = false;
}


// ---------------------------------------------------------------------------
// JNI : métronome (activation + signature rythmique)
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setMetronomeEnabled(JNIEnv *env, jclass /* clazz */,
                                                          jboolean enabled) {
    engine.metronomeEnabled = enabled;
    if (enabled) {
        // Déclenche le premier clic (accentué) dès le prochain frame audio.
        engine.metronomeFrameCounter = engine.metronomeFramesPerBeat.load();
        engine.metronomeCurrentBeatInMeasure = -1;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setMetronomeTimeSignature(JNIEnv *env, jclass /* clazz */,
                                                                jint numerator, jint denominator) {
    // "Une pulsation par noire" : pour une signature en /8, une noire = 2 croches,
    // donc le nombre de clics par mesure est la moitié du numérateur.
    int32_t beatsPerMeasure = (denominator == 8) ? std::max(1, numerator / 2) : std::max(1, numerator);
    engine.metronomeBeatsPerMeasure = beatsPerMeasure;
    engine.metronomeCurrentBeatInMeasure = -1; // le prochain clic retombera sur le 1er temps (accentué)
}