#include <jni.h>
#include <oboe/Oboe.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <cstring>
#include <vector>
#include <atomic>
#include <cmath>
#include <algorithm>


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
            left = std::max(-1.0f, std::min(1.0f, left));
            right = std::max(-1.0f, std::min(1.0f, right));

            for (int32_t ch = 0; ch < outChannels; ch++) {
                outputBuffer[frame * outChannels + ch] = (ch == 0) ? left : right;
            }
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