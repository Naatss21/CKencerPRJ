package com.example.ckencer2;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import androidx.appcompat.app.AppCompatActivity;

public class PatternActivity extends AppCompatActivity {

    private static final int MAX_STEPS = 32; // doit correspondre à SamplePlayer::kMaxSteps côté C++

    private LinearLayout stepsContainer;
    private final boolean[] stepStates = new boolean[MAX_STEPS];

    private int stepsPerGroup = 4;   // 4 = binaire (par défaut)
    private final int groupCount = 2; // nombre de groupes affichés (2 groupes de 4 = 8 cases)
    private boolean metronomeOn = false;
    private Button metronomeButton;
    private boolean sequencerPlaying = false;
    private boolean trackSoundLoaded = false;

    private TextView beatIndicatorLabel;
    private final Handler beatPollHandler = new Handler(Looper.getMainLooper());
    private final Runnable beatPollRunnable = new Runnable() {
        @Override
        public void run() {
            int beatIndex = NativeAudio.getMetronomeBeatIndex();       // 0-indexé
            int beatsPerMeasure = NativeAudio.getMetronomeBeatsPerMeasure();
            beatIndicatorLabel.setText((beatIndex + 1) + "/" + beatsPerMeasure);
            beatPollHandler.postDelayed(this, 50); // rafraîchit 20x/seconde
        }
    };
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
            if (!sequencerPlaying) {
                if (!trackSoundLoaded) {
                    NativeAudio.setSequencerBpm(90);
                    NativeAudio.loadTrackSound(0, getAssets(), "sounds/SHAKER_3.wav");
                    trackSoundLoaded = true;
                }
                NativeAudio.startSequencerStream(); // no-op si déjà ouvert (cas d'une reprise après pause)
                NativeAudio.setSequencerPlaying(true);
                sequencerPlaying = true;
                playButton.setText("Pause");
            } else {
                // Pause : on fige la position sans fermer le flux ni la réinitialiser.
                NativeAudio.setSequencerPlaying(false);
                sequencerPlaying = false;
                playButton.setText("Play");
            }
        });

        stopButton.setOnClickListener(v -> {
            NativeAudio.setSequencerPlaying(false);
            NativeAudio.stopSequencerStream();
            NativeAudio.restartSequencer(); // remet la position à la case 0 pour le prochain Play
            sequencerPlaying = false;
            playButton.setText("Play");
            metronomeOn = false;
            NativeAudio.setMetronomeEnabled(false);
            metronomeButton.setText("Métro OFF");
        });

        setSubdivision(stepsPerGroup); // construit la grille initiale (binaire, 8 cases)
        metronomeButton = findViewById(R.id.metronomeButton);
        beatIndicatorLabel = findViewById(R.id.beatIndicatorLabel);
        beatPollHandler.post(beatPollRunnable);
        NativeAudio.setMetronomeTimeSignature(4, 4); // signature par défaut au démarrage
        metronomeButton.setOnClickListener(v -> {
            metronomeOn = !metronomeOn;
            NativeAudio.setMetronomeEnabled(metronomeOn);
            metronomeButton.setText(metronomeOn ? "Métro ON" : "Métro OFF");
        });

        metronomeButton.setOnLongClickListener(v -> {
            PopupMenu menu = new PopupMenu(this, v);
            menu.getMenu().add("4/4");
            menu.getMenu().add("2/4");
            menu.getMenu().add("3/4");
            menu.getMenu().add("6/8");
            menu.getMenu().add("12/8");
            menu.setOnMenuItemClickListener(item -> {
                String[] parts = item.getTitle().toString().split("/");
                int numerator = Integer.parseInt(parts[0]);
                int denominator = Integer.parseInt(parts[1]);
                NativeAudio.setMetronomeTimeSignature(numerator, denominator);
                return true;
            });
            menu.show();
            return true; // consomme l'événement (pas de clic simple déclenché en plus)
        });
    }

    private void setSubdivision(int newStepsPerGroup) {
        stepsPerGroup = newStepsPerGroup;
        int totalSteps = stepsPerGroup * groupCount;

        java.util.Arrays.fill(stepStates, false);
        NativeAudio.setSequencerSubdivision(stepsPerGroup);
        NativeAudio.setSequencerLength(totalSteps);

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
                NativeAudio.setTrackStepActive(0, index, stepStates[index]); // piste 0 pour l'instant
            });
            stepsContainer.addView(cell);
        }
    }

        @Override
        protected void onDestroy() {
            super.onDestroy();
            beatPollHandler.removeCallbacks(beatPollRunnable);
            NativeAudio.setSequencerPlaying(false);
            NativeAudio.stopSequencerStream();
            NativeAudio.setMetronomeEnabled(false);
            sequencerPlaying = false;}

}