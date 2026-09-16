#include <jni.h>
#include <oboe/Oboe.h>
#include <cmath>

class SineGenerator : public oboe::AudioStreamCallback {
public:
    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *stream,
            void *audioData,
            int32_t numFrames) override {

        auto *outputBuffer = static_cast<float *>(audioData);
        for (int i = 0; i < numFrames; i++) {
            float sample = sinf(phase) * 0.2f; // volume réduit pour ne pas saturer
            outputBuffer[i] = sample;
            phase += 2.0f * (float) M_PI * frequency / sampleRate;
            if (phase > 2.0f * (float) M_PI) phase -= 2.0f * (float) M_PI;
        }
        return oboe::DataCallbackResult::Continue;
    }

    float frequency = 440.0f; // La (A4)
    float phase = 0.0f;
    int sampleRate = 48000;
};

static oboe::AudioStream *stream = nullptr;
static SineGenerator callback;

extern "C" JNIEXPORT void JNICALL
Java_com_example_ckencer2_MainActivity_startAudio(JNIEnv *env, jobject /* this */) {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setFormat(oboe::AudioFormat::Float);
    builder.setChannelCount(1);
    builder.setCallback(&callback);

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