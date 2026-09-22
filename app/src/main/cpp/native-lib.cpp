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
// Callback audio : joue un sample chargé en mémoire
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

                // Mode séquenceur pas à pas :
                // on avance case par case.
                currentStepFrameCounter++;

                if (currentStepFrameCounter >= framesPerStep) {
                    currentStepFrameCounter = 0;

                    int32_t total = numSteps.load();

                    if (total > 0) {
                        int32_t next =
                                (currentStepIndex.load() + 1) % total;

                        currentStepIndex = next;

                        if (next >= 0 &&
                            next < kMaxSteps &&
                            stepActive[next].load()) {

                            // La case est active :
                            // on rejoue le son depuis le début.
                            position = 0;
                        }
                    }
                }

            } else if (loopEnabled &&
                       framesPerBeat > 0 &&
                       channels > 0) {

                // Mode boucle simple à la vitesse du BPM.
                int32_t framesPlayed = position / channels;

                if (framesPlayed >= framesPerBeat) {
                    position = 0;
                }
            }

            for (int32_t ch = 0; ch < channels; ch++) {

                if (position < (int32_t) samples.size()) {
                    outputBuffer[frame * channels + ch] =
                            samples[position];

                    position++;
                } else {
                    outputBuffer[frame * channels + ch] = 0.0f;
                }
            }
        }

        return oboe::DataCallbackResult::Continue;
    }

    std::vector<float> samples;

    std::atomic<int32_t> position{0};

    int32_t sampleRate = 48000;
    int32_t channelCount = 1;

    // --- Boucle simple ---
    std::atomic<bool> loopEnabled{false};
    std::atomic<int32_t> framesPerBeat{0};

    // --- Séquenceur pas à pas ---
    std::atomic<bool> patternModeEnabled{false};
    std::atomic<int32_t> stepsPerGroup{3};
    std::atomic<int32_t> numSteps{6};
    std::atomic<int32_t> framesPerStep{0};
    std::atomic<int32_t> currentStepFrameCounter{0};
    std::atomic<int32_t> currentStepIndex{-1};

    std::atomic<bool> stepActive[kMaxSteps];
};


static oboe::AudioStream *stream = nullptr;
static SamplePlayer callback;

static std::atomic<int32_t> currentBpm{120};


// ---------------------------------------------------------------------------
// Recalcule les frames par battement / par pas
// ---------------------------------------------------------------------------
static void updateFramesPerBeat() {

    int32_t bpm = currentBpm.load();

    if (bpm > 0 && callback.sampleRate > 0) {

        callback.framesPerBeat =
                (int32_t) std::round(
                        callback.sampleRate * 60.0 / bpm
                );
    }

    int32_t spg = callback.stepsPerGroup.load();

    if (spg > 0 &&
        callback.framesPerBeat > 0) {

        callback.framesPerStep =
                callback.framesPerBeat / spg;
    }
}


// ---------------------------------------------------------------------------
// Parseur WAV minimaliste
// PCM 16 bits / PCM 32 bits / Float 32 bits
// Mono ou stéréo
// ---------------------------------------------------------------------------
struct WavHeader {

    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;

    // 1 = PCM entier
    // 3 = IEEE Float
    uint16_t formatTag = 0;
};


static bool parseWav(
        const uint8_t *data,
        size_t size,
        WavHeader &header,
        const uint8_t *&pcmData,
        size_t &pcmSize) {

    if (size < 44) {
        return false;
    }

    if (memcmp(data, "RIFF", 4) != 0 ||
        memcmp(data + 8, "WAVE", 4) != 0) {

        return false;
    }

    size_t pos = 12;

    bool foundFmt = false;
    bool foundData = false;

    while (pos + 8 <= size) {

        char chunkId[4];

        memcpy(chunkId, data + pos, 4);

        uint32_t chunkSize;

        memcpy(
                &chunkSize,
                data + pos + 4,
                4
        );

        if (memcmp(chunkId, "fmt ", 4) == 0 &&
            pos + 8 + 16 <= size) {

            memcpy(
                    &header.formatTag,
                    data + pos + 8 + 0,
                    2
            );

            memcpy(
                    &header.channels,
                    data + pos + 8 + 2,
                    2
            );

            memcpy(
                    &header.sampleRate,
                    data + pos + 8 + 4,
                    4
            );

            memcpy(
                    &header.bitsPerSample,
                    data + pos + 8 + 14,
                    2
            );

            foundFmt = true;

        } else if (memcmp(chunkId, "data", 4) == 0) {

            pcmData = data + pos + 8;

            pcmSize =
                    std::min(
                            (size_t) chunkSize,
                            size - (pos + 8)
                    );

            foundData = true;
        }

        // Les chunks WAV sont alignés sur 2 octets.
        pos += 8 + chunkSize + (chunkSize % 2);

        if (foundFmt && foundData) {
            break;
        }
    }

    return foundFmt && foundData;
}


// ---------------------------------------------------------------------------
// JNI : chargement d'un fichier depuis assets/sounds/
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_ckencer2_NativeAudio_loadSound(
        JNIEnv *env,
        jclass /* clazz */,
        jobject assetManager,
        jstring fileName) {

    AAssetManager *mgr =
            AAssetManager_fromJava(
                    env,
                    assetManager
            );

    if (!mgr) {
        return JNI_FALSE;
    }

    const char *path =
            env->GetStringUTFChars(
                    fileName,
                    nullptr
            );

    AAsset *asset =
            AAssetManager_open(
                    mgr,
                    path,
                    AASSET_MODE_BUFFER
            );

    env->ReleaseStringUTFChars(
            fileName,
            path
    );

    if (!asset) {
        return JNI_FALSE;
    }

    off_t assetLength =
            AAsset_getLength(asset);

    const auto *assetBuffer =
            static_cast<const uint8_t *>(
                    AAsset_getBuffer(asset)
            );

    WavHeader header;

    const uint8_t *pcmData = nullptr;
    size_t pcmSize = 0;

    bool ok =
            assetBuffer != nullptr &&
            parseWav(
                    assetBuffer,
                    (size_t) assetLength,
                    header,
                    pcmData,
                    pcmSize
            );

    bool isPcm16 =
            header.formatTag == 1 &&
            header.bitsPerSample == 16;

    bool isPcm32 =
            header.formatTag == 1 &&
            header.bitsPerSample == 32;

    bool isFloat32 =
            header.formatTag == 3 &&
            header.bitsPerSample == 32;


    if (ok &&
        header.channels > 0 &&
        (isPcm16 || isPcm32 || isFloat32)) {

        std::vector<float> floatSamples;


        // PCM 16 bits
        if (isPcm16) {

            size_t sampleCount =
                    pcmSize / 2;

            floatSamples.resize(
                    sampleCount
            );

            const auto *pcm16 =
                    reinterpret_cast<const int16_t *>(
                            pcmData
                    );

            for (size_t i = 0;
                 i < sampleCount;
                 i++) {

                floatSamples[i] =
                        pcm16[i] / 32768.0f;
            }


            // PCM 32 bits
        } else if (isPcm32) {

            size_t sampleCount =
                    pcmSize / 4;

            floatSamples.resize(
                    sampleCount
            );

            const auto *pcm32i =
                    reinterpret_cast<const int32_t *>(
                            pcmData
                    );

            for (size_t i = 0;
                 i < sampleCount;
                 i++) {

                floatSamples[i] =
                        pcm32i[i] / 2147483648.0f;
            }


            // Float 32 bits
        } else {

            size_t sampleCount =
                    pcmSize / 4;

            floatSamples.resize(
                    sampleCount
            );

            const auto *pcm32f =
                    reinterpret_cast<const float *>(
                            pcmData
                    );

            std::memcpy(
                    floatSamples.data(),
                    pcm32f,
                    sampleCount * sizeof(float)
            );
        }


        // Coupe le stream actuel avant
        // de remplacer le sample.
        if (stream) {

            stream->requestStop();
            stream->close();

            stream = nullptr;
        }


        callback.samples =
                std::move(floatSamples);

        callback.position = 0;

        callback.sampleRate =
                (int32_t) header.sampleRate;

        callback.channelCount =
                header.channels;

        updateFramesPerBeat();

    } else {

        ok = false;
    }


    AAsset_close(asset);

    return ok
           ? JNI_TRUE
           : JNI_FALSE;
}


// ---------------------------------------------------------------------------
// JNI : lecture
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_startAudio(
        JNIEnv *env,
        jclass /* clazz */) {

    if (callback.samples.empty()) {
        return;
    }

    if (stream) {

        callback.position = 0;

        return;
    }


    oboe::AudioStreamBuilder builder;

    builder.setDirection(
            oboe::Direction::Output
    );

    builder.setPerformanceMode(
            oboe::PerformanceMode::LowLatency
    );

    builder.setSharingMode(
            oboe::SharingMode::Exclusive
    );

    builder.setFormat(
            oboe::AudioFormat::Float
    );

    builder.setChannelCount(
            callback.channelCount
    );

    builder.setSampleRate(
            callback.sampleRate
    );

    builder.setSampleRateConversionQuality(
            oboe::SampleRateConversionQuality::Medium
    );

    builder.setCallback(
            &callback
    );


    callback.position = 0;

    builder.openStream(&stream);

    if (stream) {
        stream->requestStart();
    }
}


// ---------------------------------------------------------------------------
// JNI : arrêt
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_stopAudio(
        JNIEnv *env,
        jclass /* clazz */) {

    if (stream) {

        stream->requestStop();
        stream->close();

        stream = nullptr;
    }
}


// ---------------------------------------------------------------------------
// JNI : BPM
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setBpm(
        JNIEnv *env,
        jclass /* clazz */,
        jint bpm) {

    currentBpm = bpm;

    updateFramesPerBeat();
}


// ---------------------------------------------------------------------------
// JNI : boucle simple
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setLoopEnabled(
        JNIEnv *env,
        jclass /* clazz */,
        jboolean enabled) {

    callback.loopEnabled = enabled;

    if (enabled) {
        callback.position = 0;
    }
}


// ---------------------------------------------------------------------------
// JNI : mode pattern
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setPatternModeEnabled(
        JNIEnv *env,
        jclass /* clazz */,
        jboolean enabled) {

    callback.patternModeEnabled = enabled;

    if (enabled) {

        callback.currentStepIndex = -1;
        callback.currentStepFrameCounter = 0;
    }
}


// ---------------------------------------------------------------------------
// JNI : subdivision du pattern
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setPatternSubdivision(
        JNIEnv *env,
        jclass /* clazz */,
        jint stepsPerGroup) {

    if (stepsPerGroup > 0) {

        callback.stepsPerGroup =
                stepsPerGroup;

        updateFramesPerBeat();
    }
}


// ---------------------------------------------------------------------------
// JNI : longueur du pattern
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setPatternLength(
        JNIEnv *env,
        jclass /* clazz */,
        jint steps) {

    if (steps > 0 &&
        steps <= SamplePlayer::kMaxSteps) {

        callback.numSteps = steps;

        callback.currentStepIndex = -1;
        callback.currentStepFrameCounter = 0;
    }
}


// ---------------------------------------------------------------------------
// JNI : activation d'un step
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setStepActive(
        JNIEnv *env,
        jclass /* clazz */,
        jint index,
        jboolean active) {

    if (index >= 0 &&
        index < SamplePlayer::kMaxSteps) {

        callback.stepActive[index] = active;
    }
}


// =====================================================================================
// MOTEUR MULTI-PISTES
// =====================================================================================

static constexpr int32_t kSequencerMaxTracks = 4;

static constexpr int32_t kSequencerMaxSteps = 32;

static constexpr int32_t kEngineSampleRate = 44100;


// ---------------------------------------------------------------------------
// Structure d'une piste
// ---------------------------------------------------------------------------
struct Track {

    std::vector<float> samples;

    std::atomic<int32_t> position{0};

    std::atomic<int32_t> channelCount{1};

    std::atomic<bool> assigned{false};

    std::atomic<bool> stepActive[kSequencerMaxSteps];
};


// ---------------------------------------------------------------------------
// Métronome : clic synthétisé
// ---------------------------------------------------------------------------
static std::vector<float> generateClick(
        float frequencyHz,
        float durationSec,
        float amplitude) {

    constexpr float kPi =
            3.14159265358979323846f;

    int32_t sampleCount =
            (int32_t) (
                    kEngineSampleRate *
                    durationSec
            );

    std::vector<float> buf(
            sampleCount
    );

    for (int32_t i = 0;
         i < sampleCount;
         i++) {

        float t =
                (float) i /
                (float) kEngineSampleRate;

        // Décroissance rapide :
        // donne un clic sec.
        float envelope =
                std::exp(
                        -t * 45.0f
                );

        buf[i] =
                amplitude *
                envelope *
                std::sin(
                        2.0f *
                        kPi *
                        frequencyHz *
                        t
                );
    }

    return buf;
}


// Clic normal
static const std::vector<float>
        kMetronomeNormalClick =
        generateClick(
                1500.0f,
                0.03f,
                0.5f
        );


// Clic accentué
static const std::vector<float>
        kMetronomeAccentClick =
        generateClick(
                2200.0f,
                0.03f,
                0.6f
        );


// ---------------------------------------------------------------------------
// Moteur multipiste
// ---------------------------------------------------------------------------
class MultiTrackEngine
        : public oboe::AudioStreamCallback {

public:

    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *outStream,
            void *audioData,
            int32_t numFrames) override {

        auto *outputBuffer =
                static_cast<float *>(audioData);

        int32_t outChannels =
                outStream->getChannelCount();


        for (int32_t frame = 0;
             frame < numFrames;
             frame++) {

            // =============================================================
            // AVANCEMENT DU SEQUENCEUR + METRONOME SYNCHRONISE
            // =============================================================
            // Quand le séquenceur tourne, le métronome n'a plus sa propre
            // horloge : il se déclenche exactement quand le séquenceur
            // atteint le début d'une nouvelle noire. Impossible d'être
            // décalé puisque c'est la même horloge qui pilote les deux.

            if (isPlaying &&
                framesPerStep > 0) {

                stepFrameCounter++;

                if (stepFrameCounter >= framesPerStep) {

                    stepFrameCounter = 0;

                    int32_t total =
                            numSteps.load();

                    if (total > 0) {

                        int32_t next =
                                (currentStepIndex.load() + 1)
                                % total;

                        currentStepIndex =
                                next;


                        for (int32_t t = 0;
                             t < kSequencerMaxTracks;
                             t++) {

                            if (tracks[t].assigned &&
                                next < kSequencerMaxSteps &&
                                tracks[t].stepActive[next].load()) {

                                // Le step est actif :
                                // on rejoue la piste.
                                tracks[t].position = 0;
                            }
                        }

                        if (metronomeEnabled) {

                            int32_t spg =
                                    std::max(1, stepsPerGroup.load());

                            // Début d'une nouvelle noire ?
                            if (next % spg == 0) {

                                int32_t beatsPerMeasure =
                                        std::max(1, metronomeBeatsPerMeasure.load());

                                int32_t beatIndex =
                                        metronomeBeatCounter.load() % beatsPerMeasure;

                                metronomeAccent =
                                        (beatIndex == 0);

                                metronomeBeatCounter =
                                        metronomeBeatCounter.load() + 1;

                                currentBeatDisplay = beatIndex;

                                metronomePosition = 0;
                            }

                        }
                    }
                }

            } else if (metronomeEnabled &&
                       metronomeFramesPerBeat > 0 && !sequencerHasBeenStarted ) {

                // Le séquenceur n'a encore jamais démarré : le métronome
                // tourne sur sa propre horloge pour caler le tempo.
                // Une fois qu'il a démarré, une pause fige tout, y compris
                // le métronome (cette branche n'est alors plus jamais prise)
                metronomeFrameCounter++;

                if (metronomeFrameCounter >=
                    metronomeFramesPerBeat) {

                    metronomeFrameCounter = 0;

                    int32_t beatsPerMeasure =
                            metronomeBeatsPerMeasure.load();

                    if (beatsPerMeasure < 1) {
                        beatsPerMeasure = 1;
                    }

                    int32_t nextBeat =
                            (
                                    metronomeCurrentBeatInMeasure.load()
                                    + 1
                            )
                            % beatsPerMeasure;

                    metronomeCurrentBeatInMeasure =
                            nextBeat;

                    metronomeAccent =
                            (nextBeat == 0);

                    currentBeatDisplay = nextBeat;

                    metronomePosition = 0;
                }
            }



            // =============================================================
            // MIXAGE DES PISTES
            // =============================================================

            float left = 0.0f;
            float right = 0.0f;


            for (int32_t t = 0;
                 t < kSequencerMaxTracks;
                 t++) {

                Track &track =
                        tracks[t];


                if (!track.assigned) {
                    continue;
                }


                int32_t pos =
                        track.position.load();


                if (pos >=
                    (int32_t) track.samples.size()) {

                    // Piste terminée :
                    // silence.
                    continue;
                }


                int32_t trackChannels =
                        track.channelCount.load();


                // ---------------------------------------------------------
                // Piste stéréo
                // ---------------------------------------------------------
                if (trackChannels >= 2 &&
                    pos + 1 <
                    (int32_t) track.samples.size()) {

                    left +=
                            track.samples[pos];

                    right +=
                            track.samples[pos + 1];

                    track.position =
                            pos + 2;

                } else {

                    // -----------------------------------------------------
                    // Piste mono
                    // -----------------------------------------------------
                    float value =
                            track.samples[pos];

                    left += value;
                    right += value;

                    track.position =
                            pos + 1;
                }
            }


            // =============================================================
            // MIXAGE DU METRONOME
            // =============================================================

            if (metronomeEnabled) {

                const std::vector<float> &clickBuffer =
                        metronomeAccent.load()
                        ? kMetronomeAccentClick
                        : kMetronomeNormalClick;


                int32_t clickPos =
                        metronomePosition.load();


                if (clickPos <
                    (int32_t) clickBuffer.size()) {

                    float v =
                            clickBuffer[clickPos];

                    left += v;
                    right += v;

                    metronomePosition =
                            clickPos + 1;
                }
            }


            // =============================================================
            // ECRETAGE
            // =============================================================

            left =
                    std::max(
                            -1.0f,
                            std::min(
                                    1.0f,
                                    left
                            )
                    );

            right =
                    std::max(
                            -1.0f,
                            std::min(
                                    1.0f,
                                    right
                            )
                    );


            // =============================================================
            // SORTIE AUDIO
            // =============================================================

            for (int32_t ch = 0;
                 ch < outChannels;
                 ch++) {

                outputBuffer[
                        frame * outChannels + ch
                ] =
                        (ch == 0)
                        ? left
                        : right;
            }
        }


        return oboe::DataCallbackResult::Continue;
    }


    // -----------------------------------------------------------------------
    // Pistes
    // -----------------------------------------------------------------------
    Track tracks[kSequencerMaxTracks];


    // -----------------------------------------------------------------------
    // Séquenceur
    // -----------------------------------------------------------------------
    std::atomic<bool> isPlaying{false};

    // true dès le premier Play ; remis à false uniquement par Stop.
    // Sert à distinguer "en pause" (le métronome doit se figer) de
    // "jamais démarré" (le métronome peut tourner seul pour caler le tempo).
    std::atomic<bool> sequencerHasBeenStarted{false};

    std::atomic<int32_t> numSteps{5};

    std::atomic<int32_t> framesPerStep{0};

    std::atomic<int32_t> stepFrameCounter{0};

    std::atomic<int32_t> currentStepIndex{-1};

    std::atomic<int32_t> stepsPerGroup{4}; // nombre de cases par noire (3 = ternaire, 4 = binaire)



    // -----------------------------------------------------------------------
    // Métronome
    // -----------------------------------------------------------------------
    std::atomic<bool> metronomeEnabled{false};

    std::atomic<int32_t> metronomeFramesPerBeat{0};

    std::atomic<int32_t> metronomeFrameCounter{0};

    std::atomic<int32_t> metronomeBeatsPerMeasure{4};

    std::atomic<int32_t> metronomeCurrentBeatInMeasure{-1};

    std::atomic<int32_t> metronomePosition{0};

    std::atomic<bool> metronomeAccent{false};

    // Compteur de temps indépendant de la longueur de la boucle du séquenceur,
    // pour que l'accent respecte vraiment la signature choisie (4/4, 3/4...).
    std::atomic<int32_t> metronomeBeatCounter{0};

    // Temps courant dans la mesure (0-indexé), pour affichage côté Java (1/4, 2/4...).
    std::atomic<int32_t> currentBeatDisplay{0};
};


// ---------------------------------------------------------------------------
// Instance globale du moteur multipiste
// ---------------------------------------------------------------------------
static MultiTrackEngine engine;


// ---------------------------------------------------------------------------
// Paramètres du séquenceur
// ---------------------------------------------------------------------------
static std::atomic<int32_t> sequencerBpm{120};

static std::atomic<int32_t> sequencerStepsPerGroup{4};


// ---------------------------------------------------------------------------
// Mise à jour du timing du séquenceur + métronome
// ---------------------------------------------------------------------------
static void updateSequencerTiming() {

    int32_t bpm =
            sequencerBpm.load();

    int32_t spg =
            sequencerStepsPerGroup.load();


    int32_t framesPerBeat = 0;


    // ---------------------------------------------------------
    // BPM
    // ---------------------------------------------------------
    if (bpm > 0) {

        framesPerBeat =
                (int32_t) std::round(
                        kEngineSampleRate *
                        60.0 /
                        bpm
                );


        // Le métronome joue une pulsation par noire.
        engine.metronomeFramesPerBeat =
                framesPerBeat;
    }


    // ---------------------------------------------------------
    // Séquenceur
    // ---------------------------------------------------------
    if (bpm > 0 &&
        spg > 0) {

        engine.framesPerStep =
                framesPerBeat / spg;
    }
}


// ---------------------------------------------------------------------------
// JNI : chargement d'un son sur une piste
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_ckencer2_NativeAudio_loadTrackSound(
        JNIEnv *env,
        jclass /* clazz */,
        jint trackIndex,
        jobject assetManager,
        jstring fileName) {

    if (trackIndex < 0 ||
        trackIndex >= kSequencerMaxTracks) {

        return JNI_FALSE;
    }


    AAssetManager *mgr =
            AAssetManager_fromJava(
                    env,
                    assetManager
            );

    if (!mgr) {
        return JNI_FALSE;
    }


    const char *path =
            env->GetStringUTFChars(
                    fileName,
                    nullptr
            );


    AAsset *asset =
            AAssetManager_open(
                    mgr,
                    path,
                    AASSET_MODE_BUFFER
            );


    env->ReleaseStringUTFChars(
            fileName,
            path
    );


    if (!asset) {
        return JNI_FALSE;
    }


    off_t assetLength =
            AAsset_getLength(asset);


    const auto *assetBuffer =
            static_cast<const uint8_t *>(
                    AAsset_getBuffer(asset)
            );


    WavHeader header;

    const uint8_t *pcmData = nullptr;

    size_t pcmSize = 0;


    bool ok =
            assetBuffer != nullptr &&
            parseWav(
                    assetBuffer,
                    (size_t) assetLength,
                    header,
                    pcmData,
                    pcmSize
            );


    bool isPcm16 =
            header.formatTag == 1 &&
            header.bitsPerSample == 16;

    bool isPcm32 =
            header.formatTag == 1 &&
            header.bitsPerSample == 32;

    bool isFloat32 =
            header.formatTag == 3 &&
            header.bitsPerSample == 32;


    if (ok &&
        header.channels > 0 &&
        (isPcm16 || isPcm32 || isFloat32)) {

        std::vector<float> floatSamples;


        // ---------------------------------------------------------
        // PCM 16
        // ---------------------------------------------------------
        if (isPcm16) {

            size_t sampleCount =
                    pcmSize / 2;

            floatSamples.resize(
                    sampleCount
            );


            const auto *pcm16 =
                    reinterpret_cast<const int16_t *>(
                            pcmData
                    );


            for (size_t i = 0;
                 i < sampleCount;
                 i++) {

                floatSamples[i] =
                        pcm16[i] /
                        32768.0f;
            }


            // ---------------------------------------------------------
            // PCM 32
            // ---------------------------------------------------------
        } else if (isPcm32) {

            size_t sampleCount =
                    pcmSize / 4;

            floatSamples.resize(
                    sampleCount
            );


            const auto *pcm32i =
                    reinterpret_cast<const int32_t *>(
                            pcmData
                    );


            for (size_t i = 0;
                 i < sampleCount;
                 i++) {

                floatSamples[i] =
                        pcm32i[i] /
                        2147483648.0f;
            }


            // ---------------------------------------------------------
            // Float 32
            // ---------------------------------------------------------
        } else {

            size_t sampleCount =
                    pcmSize / 4;

            floatSamples.resize(
                    sampleCount
            );


            const auto *pcm32f =
                    reinterpret_cast<const float *>(
                            pcmData
                    );


            std::memcpy(
                    floatSamples.data(),
                    pcm32f,
                    sampleCount * sizeof(float)
            );
        }


        // ---------------------------------------------------------
        // Affectation de la piste
        // ---------------------------------------------------------
        Track &track =
                engine.tracks[trackIndex];


        track.samples =
                std::move(floatSamples);


        track.channelCount =
                header.channels;


        // La piste reste silencieuse
        // jusqu'à ce qu'un step la déclenche.
        track.position =
                (int32_t) track.samples.size();


        track.assigned =
                true;


        // Les fichiers doivent être en 44100 Hz.
        // Aucun ré-échantillonnage par piste
        // n'est effectué ici.

    } else {

        ok = false;
    }


    AAsset_close(asset);


    return ok
           ? JNI_TRUE
           : JNI_FALSE;
}


// ---------------------------------------------------------------------------
// JNI : activation d'un step d'une piste
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setTrackStepActive(
        JNIEnv *env,
        jclass /* clazz */,
        jint trackIndex,
        jint stepIndex,
        jboolean active) {

    if (trackIndex < 0 ||
        trackIndex >= kSequencerMaxTracks) {

        return;
    }


    if (stepIndex < 0 ||
        stepIndex >= kSequencerMaxSteps) {

        return;
    }


    engine.tracks[trackIndex]
            .stepActive[stepIndex] =
            active;
}


// ---------------------------------------------------------------------------
// JNI : BPM du séquenceur
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerBpm(
        JNIEnv *env,
        jclass /* clazz */,
        jint bpm) {

    sequencerBpm = bpm;

    updateSequencerTiming();
}


// ---------------------------------------------------------------------------
// JNI : subdivision du séquenceur
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerSubdivision(
        JNIEnv *env,
        jclass /* clazz */,
        jint stepsPerGroup) {

    if (stepsPerGroup > 0) {

        sequencerStepsPerGroup =
                stepsPerGroup;

        engine.stepsPerGroup =
                stepsPerGroup;

        updateSequencerTiming();
    }
}


// ---------------------------------------------------------------------------
// JNI : longueur du séquenceur
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerLength(
        JNIEnv *env,
        jclass /* clazz */,
        jint steps) {

    if (steps > 0 &&
        steps <= kSequencerMaxSteps) {

        engine.numSteps = steps;
    }
}


// ---------------------------------------------------------------------------
// JNI : lecture du séquenceur
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setSequencerPlaying(
        JNIEnv *env,
        jclass /* clazz */,
        jboolean playing) {

    bool newState = (playing == JNI_TRUE);

    // Play = reprend
    // Pause = s'arrête sans modifier les positions
    engine.isPlaying = newState;
    if (newState) {
        engine.sequencerHasBeenStarted = true;
    }
    // On ne touche à rien d'autre : stepFrameCounter reste exactement
    // où il était figé pendant la pause, pour reprendre sans décalage.
}


// ---------------------------------------------------------------------------
// JNI : redémarrage du séquenceur
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_restartSequencer(
        JNIEnv *env,
        jclass /* clazz */) {

    engine.currentStepIndex = -1;

    engine.stepFrameCounter =
            engine.framesPerStep.load();
}


// ---------------------------------------------------------------------------
// JNI : démarrage du stream du séquenceur
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_startSequencerStream(
        JNIEnv *env,
        jclass /* clazz */) {

    if (stream) {
        return;
    }


    oboe::AudioStreamBuilder builder;


    builder.setDirection(
            oboe::Direction::Output
    );


    builder.setPerformanceMode(
            oboe::PerformanceMode::LowLatency
    );


    builder.setSharingMode(
            oboe::SharingMode::Exclusive
    );


    builder.setFormat(
            oboe::AudioFormat::Float
    );


    builder.setChannelCount(2);


    builder.setSampleRate(
            kEngineSampleRate
    );


    builder.setSampleRateConversionQuality(
            oboe::SampleRateConversionQuality::Medium
    );


    builder.setCallback(
            &engine
    );


    builder.openStream(
            &stream
    );


    if (stream) {
        stream->requestStart();
    }
}


// ---------------------------------------------------------------------------
// JNI : arrêt du stream du séquenceur
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_stopSequencerStream(
        JNIEnv *env,
        jclass /* clazz */) {

    engine.isPlaying = false;

    // Remise à zéro de la lecture de toutes les pistes
    for (int32_t t = 0; t < kSequencerMaxTracks; t++) {
        engine.tracks[t].position = 0;
    }

    // Retour au premier step
    engine.currentStepIndex = -1;
    engine.stepFrameCounter = 0;

    // Reset du métronome
    engine.metronomeFrameCounter = 0;
    engine.metronomeCurrentBeatInMeasure = -1;
    engine.metronomePosition = 0;
    engine.metronomeBeatCounter = 0;
    engine.sequencerHasBeenStarted = false;
    engine.currentBeatDisplay = 0;

    if (stream != nullptr) {
        stream->requestStop();
        stream->close();
        stream = nullptr;
    }
}

// ---------------------------------------------------------------------------
// JNI : activation / désactivation du métronome
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setMetronomeEnabled(
        JNIEnv *env,
        jclass /* clazz */,
        jboolean enabled) {

    engine.metronomeEnabled =
            enabled;



    if (enabled && !engine.isPlaying) {

        // Le séquenceur ne tourne pas : pas d'horloge à suivre,
        // on déclenche le premier clic immédiatement.
        engine.metronomeFrameCounter =
                engine.metronomeFramesPerBeat.load();

        engine.metronomeCurrentBeatInMeasure =
                -1;
    }
    // Si le séquenceur tourne déjà, on ne touche à rien : le clic
    // se déclenchera tout seul, calé sur la grille, à la prochaine noire.
}


// ---------------------------------------------------------------------------
// JNI : signature rythmique du métronome
// ---------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ckencer2_NativeAudio_setMetronomeTimeSignature(
        JNIEnv *env,
        jclass /* clazz */,
        jint numerator,
        jint denominator) {

    // Une pulsation par noire.
    //
    // Pour une signature en /8 :
    // une noire = 2 croches.
    //
    // Exemple :
    // 6/8 -> 3 clics par mesure
    // 12/8 -> 6 clics par mesure

    int32_t beatsPerMeasure;


    if (denominator == 8) {

        beatsPerMeasure =
                std::max(
                        1,
                        numerator / 2
                );

    } else {

        beatsPerMeasure =
                std::max(
                        1,
                        numerator
                );
    }


    engine.metronomeBeatsPerMeasure =
            beatsPerMeasure;


    // Le prochain clic sera
    // le premier temps de la mesure.
    engine.metronomeCurrentBeatInMeasure =
            -1;

    engine.metronomeBeatCounter = 0;
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ckencer2_NativeAudio_getMetronomeBeatIndex(
        JNIEnv *env,
        jclass /* clazz */) {

    return engine.currentBeatDisplay.load();
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_ckencer2_NativeAudio_getMetronomeBeatsPerMeasure(
        JNIEnv *env,
        jclass /* clazz */) {

    return engine.metronomeBeatsPerMeasure.load();
}