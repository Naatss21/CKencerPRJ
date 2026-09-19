package com.example.ckencer2;

import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {

    // Méthodes natives C++
    public native boolean NativeAudio.loadSound(android.content.res.AssetManager assetManager, String fileName);
    public native void NativeAudio.startAudio();
    public native void NativeAudio.stopAudio();
    public native void NativeAudio.setBpm(int bpm);
    public native void NativeAudio.setLoopEnabled(boolean enabled);

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
        SeekBar bpmSeekBar = findViewById(R.id.bpmSeekBar);
        TextView bpmValueLabel = findViewById(R.id.bpmValueLabel);
        Switch loopSwitch = findViewById(R.id.loopSwitch);

        playButton.setOnClickListener(v -> startAudio());
        stopButton.setOnClickListener(v -> stopAudio());
        chooseSoundButton.setOnClickListener(v ->
                soundPicker.launch(new Intent(this, SoundListActivity.class)));

        // Métronome : BPM initial = valeur de départ de la SeekBar
        int initialBpm = bpmSeekBar.getProgress();
        bpmValueLabel.setText("BPM : " + initialBpm);
        setBpm(initialBpm);

        bpmSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                bpmValueLabel.setText("BPM : " + progress);
                setBpm(progress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        Button openPatternButton = findViewById(R.id.openPatternButton);
        openPatternButton.setOnClickListener(v ->
                startActivity(new Intent(this, PatternActivity.class)));

        loopSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            setLoopEnabled(isChecked);
            if (isChecked) {
                startAudio(); // s'assure que le stream est ouvert pour entendre la boucle
            }
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopAudio();
    }
}