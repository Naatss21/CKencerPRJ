package com.example.ckencer2;

import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("ckencer2");
    }

    // Méthodes natives C++
    public native boolean loadSound(android.content.res.AssetManager assetManager, String fileName);
    public native void startAudio();
    public native void stopAudio();

    private TextView selectedSoundLabel;

    private final ActivityResultLauncher<Intent> soundPicker =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
                if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                    String fileName = result.getData().getStringExtra(SoundListActivity.EXTRA_SOUND_FILE);
                    if (fileName != null) {
                        boolean ok = loadSound(getAssets(), fileName);
                        if (ok) {
                            selectedSoundLabel.setText(fileName);
                        } else {
                            Toast.makeText(this, "Impossible de charger ce fichier (WAV PCM 16 bits attendu)", Toast.LENGTH_LONG).show();
                        }
                    }
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        selectedSoundLabel = findViewById(R.id.selectedSoundLabel);

        Button playButton = findViewById(R.id.playBouton);
        Button stopButton = findViewById(R.id.pauseBouton);
        Button chooseSoundButton = findViewById(R.id.chooseSoundButton);

        playButton.setOnClickListener(v -> startAudio());
        stopButton.setOnClickListener(v -> stopAudio());
        chooseSoundButton.setOnClickListener(v ->
                soundPicker.launch(new Intent(this, SoundListActivity.class)));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopAudio();
    }
}