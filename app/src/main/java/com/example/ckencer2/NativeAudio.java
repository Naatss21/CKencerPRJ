package com.example.ckencer2;

import android.content.res.AssetManager;

/**
 * Point d'entrée unique vers le moteur audio natif (C++/Oboe).
 * Toutes les Activities (MainActivity, PatternActivity, ...) passent par ici
 * pour parler au même moteur audio partagé au niveau du processus.
 */
public final class NativeAudio {

    static {
        System.loadLibrary("ckencer2");
    }

    private NativeAudio() {}

    // --- Ancien moteur mono-piste (Play/Stop, boucle BPM, pattern simple) ---
    public static native boolean loadSound(AssetManager assetManager, String fileName);
    public static native void startAudio();
    public static native void stopAudio();
    public static native void setBpm(int bpm);
    public static native void setLoopEnabled(boolean enabled);
    public static native void setPatternModeEnabled(boolean enabled);
    public static native void setPatternSubdivision(int stepsPerGroup);
    public static native void setPatternLength(int steps);
    public static native void setStepActive(int index, boolean active);

    // --- Nouveau moteur multi-pistes (séquenceur) ---
    public static native boolean loadTrackSound(int trackIndex, AssetManager assetManager, String fileName);
    public static native void setTrackStepActive(int trackIndex, int stepIndex, boolean active);
    public static native void setSequencerBpm(int bpm);
    public static native void setSequencerSubdivision(int stepsPerGroup);
    public static native void setSequencerLength(int steps);
    public static native void setSequencerPlaying(boolean playing);
    public static native void restartSequencer();
    public static native void startSequencerStream();
    public static native void stopSequencerStream();
}