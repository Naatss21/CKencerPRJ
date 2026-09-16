package com.example.ckencer2;

import android.os.Bundle;
import android.widget.Button;
import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("ckencer2");
    }

    // Méthodes natives C++
    public native void startAudio();
    public native void stopAudio();

    private boolean isPlaying = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        Button toggleButton = findViewById(R.id.playBouton);
        toggleButton.setOnClickListener(v -> {
            if (isPlaying) {
                stopAudio();
                toggleButton.setText("Start");
                isPlaying = false;
            } else {
                startAudio();
                toggleButton.setText("Stop");
                isPlaying = true;
            }
        });
    }
}