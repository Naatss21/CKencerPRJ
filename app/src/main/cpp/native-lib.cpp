#include <jni.h>
#include <oboe/Oboe.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <cstring>
#include <vector>
#include <atomic>
#include <cmath>


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