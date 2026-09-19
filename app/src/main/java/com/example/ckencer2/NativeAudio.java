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

    public static native boolean loadSound(AssetManager assetManager, String fileName);
    public static native void startAudio();
    public static native void stopAudio();

    // Métronome / boucle simple
    public static native void setBpm(int bpm);
    public static native void setLoopEnabled(boolean enabled);

    // Séquenceur pas à pas ("pattern")
    public static native void setPatternModeEnabled(boolean enabled);
    public static native void setPatternSubdivision(int stepsPerGroup); // 3 = ternaire, 4 = binaire
    public static native void setPatternLength(int steps);
    public static native void setStepActive(int index, boolean active);
}