package com.example.ckencer2;

import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;

import androidx.appcompat.app.AppCompatActivity;

public class PatternActivity extends AppCompatActivity {

    private static final int MAX_STEPS = 32; // doit correspondre à SamplePlayer::kMaxSteps côté C++

    private LinearLayout stepsContainer;
    private final boolean[] stepStates = new boolean[MAX_STEPS];

    private int stepsPerGroup = 3;   // 3 = ternaire (par défaut, comme dans l'exemple), 4 = binaire
    private final int groupCount = 2; // nombre de groupes affichés (2 groupes de 3 = 6 cases)

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_paterrn);

        stepsContainer = findViewById(R.id.patternStepsContainer);
        Button binaryButton = findViewById(R.id.patternBinaryButton);
        Button ternaryButton = findViewById(R.id.patternTernaryButton);
        Button playButton = findViewById(R.id.patternPlayButton);
        Button stopButton = findViewById(R.id.patternStopButton);

        binaryButton.setOnClickListener(v -> setSubdivision(4));
        ternaryButton.setOnClickListener(v -> setSubdivision(3));

        playButton.setOnClickListener(v -> {
            NativeAudio.setPatternModeEnabled(true);
            NativeAudio.startAudio();
        });

        stopButton.setOnClickListener(v -> {
            NativeAudio.setPatternModeEnabled(false);
            NativeAudio.stopAudio();
        });

        setSubdivision(stepsPerGroup); // construit la grille initiale (ternaire, 6 cases)
    }

    private void setSubdivision(int newStepsPerGroup) {
        stepsPerGroup = newStepsPerGroup;
        int totalSteps = stepsPerGroup * groupCount;

        java.util.Arrays.fill(stepStates, false);
        NativeAudio.setPatternSubdivision(stepsPerGroup);
        NativeAudio.setPatternLength(totalSteps);

        buildGrid(totalSteps);
    }

    private void buildGrid(int totalSteps) {
        stepsContainer.removeAllViews();

        int marginPx = (int) (2 * getResources().getDisplayMetrics().density);
        int groupGapPx = marginPx * 4;

        for (int i = 0; i < totalSteps; i++) {
            Button cell = new Button(this);
            cell.setText("");
            cell.setBackgroundColor(0xFFFFFFFF); // blanc = case inactive

            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);

            boolean endOfGroup = (i + 1) % stepsPerGroup == 0 && i != totalSteps - 1;
            params.setMargins(marginPx, marginPx, endOfGroup ? groupGapPx : marginPx, marginPx);
            cell.setLayoutParams(params);

            final int index = i;
            final Button cellRef = cell;
            cell.setOnClickListener(v -> {
                stepStates[index] = !stepStates[index];
                cellRef.setBackgroundColor(stepStates[index] ? 0xFFCEEF34 : 0xFFFFFFFF);
                NativeAudio.setStepActive(index, stepStates[index]);
            });

            stepsContainer.addView(cell);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        NativeAudio.setPatternModeEnabled(false);
        NativeAudio.stopAudio();
    }
}