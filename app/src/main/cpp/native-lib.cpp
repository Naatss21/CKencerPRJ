#include <jni.h>
#include <oboe/Oboe.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <cstring>
#include <vector>
#include <atomic>

// ---------------------------------------------------------------------------
// Callback audio : joue un sample chargé en mémoire (au lieu d'une sinusoïde)
// ---------------------------------------------------------------------------
class SamplePlayer : public oboe::AudioStreamCallback {
public:
    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *stream,
            void *audioData,
            int32_t numFrames) override {

        auto *outputBuffer = static_cast<float *>(audioData);
        int32_t channels = stream->getChannelCount();
        int32_t samplesToWrite = numFrames * channels;

        int32_t written = 0;
        while (written < samplesToWrite && position < (int32_t) samples.size()) {
            outputBuffer[written] = samples[position];
            position++;
            written++;
        }
        // Fin du sample atteinte : on complète avec du silence (le stream reste ouvert)
        while (written < samplesToWrite) {
            outputBuffer[written] = 0.0f;
            written++;
        }

        return oboe::DataCallbackResult::Continue;
    }

    std::vector<float> samples;          // audio en float [-1, 1], entrelacé par canal
    std::atomic<int32_t> position{0};
    int32_t sampleRate = 48000;
    int32_t channelCount = 1;
};

static oboe::AudioStream *stream = nullptr;
static SamplePlayer callback;

// ---------------------------------------------------------------------------
// Parseur WAV minimaliste (PCM 16 bits, mono ou stéréo)
// ---------------------------------------------------------------------------
struct WavHeader {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
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
Java_com_example_ckencer2_MainActivity_loadSound(JNIEnv *env, jobject /* this */,
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

    if (ok && header.bitsPerSample == 16 && header.channels > 0) {
        size_t sampleCount = pcmSize / 2;
        std::vector<float> floatSamples(sampleCount);
        const auto *pcm16 = reinterpret_cast<const int16_t *>(pcmData);
        for (size_t i = 0; i < sampleCount; i++) {
            floatSamples[i] = pcm16[i] / 32768.0f;
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
    } else {
        ok = false; // format non supporté (seul le PCM 16 bits est géré ici)
    }

    AAsset_close(asset);
    return ok ? JNI_TRUE : JNI_FALSE;
}

// ---------------------------------------------------------------------------
// JNI : lecture / arrêt
// ---------------------------------------------------------------------------
extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_MainActivity_startAudio(JNIEnv *env, jobject /* this */) {
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
Java_com_example_ckencer2_MainActivity_stopAudio(JNIEnv *env, jobject /* this */) {
    if (stream) {
        stream->requestStop();
        stream->close();
        stream = nullptr;
    }
}