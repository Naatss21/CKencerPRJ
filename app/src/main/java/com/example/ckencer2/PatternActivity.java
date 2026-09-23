package com.example.ckencer2;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import android.widget.Toast;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

public class PatternActivity extends AppCompatActivity {

    private static final int MAX_STEPS = 32;

    private final LinearLayout[] containers = new LinearLayout[3];
    private final boolean[][] stepStates = new boolean[3][MAX_STEPS];
    private int currentSubdivision = 4; // 4 = Binaire (doubles croches), 3 = Ternaire (triolets)

    private boolean metronomeOn = false;
    private ImageButton metronomeButton;
    private ImageButton playPauseButton;
    private boolean sequencerPlaying = false;

    private int activeTrackForSoundPicker = -1;
    private String[] loadedTrackPath = new String[]{"sounds/KICK_1.wav", "sounds/SNARE_1.wav", "sounds/SHAKER_3.wav"};
    private boolean track1SoundLoaded = false;
    private boolean track2SoundLoaded = false;
    private boolean track3SoundLoaded = false;

    private TextView beatIndicatorLabel;
    private TextView tvBpm;
    private final Handler beatPollHandler = new Handler(Looper.getMainLooper());
    private final Runnable beatPollRunnable = new Runnable() {
        @Override
        public void run() {
            int beatIndex = NativeAudio.getMetronomeBeatIndex();
            int beatsPerMeasure = NativeAudio.getMetronomeBeatsPerMeasure();
            beatIndicatorLabel.setText((beatIndex + 1) + "/" + beatsPerMeasure);
            beatPollHandler.postDelayed(this, 50);
        }
    };

    private final ActivityResultLauncher<Intent> soundPicker =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
                if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                    String fileName = result.getData().getStringExtra(SoundListActivity.EXTRA_SOUND_FILE);
                    if (fileName != null && activeTrackForSoundPicker >= 0) {
                        boolean ok = NativeAudio.loadTrackSound(activeTrackForSoundPicker, getAssets(), fileName);
                        if (ok) {
                            loadedTrackPath[activeTrackForSoundPicker] = fileName;
                            if (activeTrackForSoundPicker == 0) track1SoundLoaded = true;
                            if (activeTrackForSoundPicker == 1) track2SoundLoaded = true;
                            if (activeTrackForSoundPicker == 2) track3SoundLoaded = true;

                            String shortName = fileName.substring(fileName.lastIndexOf('/') + 1);
                            Button targetButton = null;
                            if (activeTrackForSoundPicker == 0) targetButton = findViewById(R.id.btn_select_sound_1);
                            if (activeTrackForSoundPicker == 1) targetButton = findViewById(R.id.btn_select_sound_2);
                            if (activeTrackForSoundPicker == 2) targetButton = findViewById(R.id.btn_select_sound_3);

                            if (targetButton != null) {
                                targetButton.setText(shortName);
                                targetButton.setTextSize(9);
                            }
                        } else {
                            Toast.makeText(this, "Impossible de charger ce fichier", Toast.LENGTH_SHORT).show();
                        }
                    }
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_paterrn);

        containers[0] = findViewById(R.id.container_track_1);
        containers[1] = findViewById(R.id.container_track_2);
        containers[2] = findViewById(R.id.container_track_3);

        playPauseButton = findViewById(R.id.btn_play_pause);
        ImageButton stopButton = findViewById(R.id.btn_stop);
        metronomeButton = findViewById(R.id.btn_metronome);
        beatIndicatorLabel = findViewById(R.id.beatIndicatorLabel);
        tvBpm = findViewById(R.id.tv_bpm);

        Button btnBpmMinus = findViewById(R.id.btn_bpm_minus);
        Button btnBpmPlus = findViewById(R.id.btn_bpm_plus);

        Button btnSelectSound1 = findViewById(R.id.btn_select_sound_1);
        Button btnSelectSound2 = findViewById(R.id.btn_select_sound_2);
        Button btnSelectSound3 = findViewById(R.id.btn_select_sound_3);

        btnSelectSound1.setOnClickListener(v -> {
            activeTrackForSoundPicker = 0;
            soundPicker.launch(new Intent(this, SoundListActivity.class));
        });
        btnSelectSound2.setOnClickListener(v -> {
            activeTrackForSoundPicker = 1;
            soundPicker.launch(new Intent(this, SoundListActivity.class));
        });
        btnSelectSound3.setOnClickListener(v -> {
            activeTrackForSoundPicker = 2;
            soundPicker.launch(new Intent(this, SoundListActivity.class));
        });

        playPauseButton.setOnClickListener(v -> {
            if (!sequencerPlaying) {
                if (!track1SoundLoaded) {
                    NativeAudio.loadTrackSound(0, getAssets(), "sounds/KICK_1.wav");
                    track1SoundLoaded = true;
                    btnSelectSound1.setText("KICK_1.wav");
                    btnSelectSound1.setTextSize(9);
                }
                if (!track2SoundLoaded) {
                    NativeAudio.loadTrackSound(1, getAssets(), "sounds/SNARE_1.wav");
                    track2SoundLoaded = true;
                    btnSelectSound2.setText("SNARE_1.wav");
                    btnSelectSound2.setTextSize(9);
                }
                if (!track3SoundLoaded) {
                    NativeAudio.loadTrackSound(2, getAssets(), "sounds/SHAKER_3.wav");
                    track3SoundLoaded = true;
                    btnSelectSound3.setText("SHAKER_3.wav");
                    btnSelectSound3.setTextSize(9);
                }

                String currentText = tvBpm.getText().toString().replace(" bpm", "");
                int currentBpm = Integer.parseInt(currentText);
                NativeAudio.setSequencerBpm(currentBpm);
                
                NativeAudio.startSequencerStream();
                NativeAudio.setSequencerPlaying(true);
                sequencerPlaying = true;
                playPauseButton.setImageResource(R.drawable.ic_pause_bars);
            } else {
                NativeAudio.setSequencerPlaying(false);
                sequencerPlaying = false;
                playPauseButton.setImageResource(R.drawable.ic_play_triangle);
            }
        });

        stopButton.setOnClickListener(v -> {
            NativeAudio.setSequencerPlaying(false);
            NativeAudio.stopSequencerStream();
            NativeAudio.restartSequencer();
            sequencerPlaying = false;
            playPauseButton.setImageResource(R.drawable.ic_play_triangle);
            metronomeOn = false;
            NativeAudio.setMetronomeEnabled(false);
            metronomeButton.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFFFF3B30));
        });

        buildGrid();

        beatPollHandler.post(beatPollRunnable);
        NativeAudio.setMetronomeTimeSignature(4, 4);

        // Dialogue pour changer le BPM au clic sur le texte "90 bpm"
        tvBpm.setOnClickListener(v -> {
            androidx.appcompat.app.AlertDialog.Builder builder = new androidx.appcompat.app.AlertDialog.Builder(this);
            builder.setTitle("Modifier le BPM (40 - 200)");

            final android.widget.EditText input = new android.widget.EditText(this);
            input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
            String currentText = tvBpm.getText().toString().replace(" bpm", "");
            input.setText(currentText);
            input.setSelection(input.getText().length());
            
            builder.setView(input);

            builder.setPositiveButton("Valider", (dialog, which) -> {
                String value = input.getText().toString();
                if (!value.isEmpty()) {
                    try {
                        int newBpm = Integer.parseInt(value);
                        if (newBpm >= 40 && newBpm <= 200) {
                            NativeAudio.setSequencerBpm(newBpm);
                            tvBpm.setText(newBpm + " bpm");
                        } else {
                            Toast.makeText(this, "Le BPM doit être compris entre 40 et 200", Toast.LENGTH_SHORT).show();
                        }
                    } catch (NumberFormatException e) {
                        Toast.makeText(this, "Valeur invalide", Toast.LENGTH_SHORT).show();
                    }
                }
            });
            builder.setNegativeButton("Annuler", (dialog, which) -> dialog.cancel());

            builder.show();
        });

        // Configuration de la répétition et de l'accélération automatique pour les boutons + et -
        setupAutoRepeatBpmButton(btnBpmMinus, -1);
        setupAutoRepeatBpmButton(btnBpmPlus, 1);

        Button btnSavePattern = findViewById(R.id.btn_save_pattern);
        Button btnLoadPattern = findViewById(R.id.btn_load_pattern);

        btnSavePattern.setOnClickListener(v -> savePatternToFile());
        btnLoadPattern.setOnClickListener(v -> loadPatternFromFile());

        metronomeButton.setOnClickListener(v -> {
            metronomeOn = !metronomeOn;
            NativeAudio.setMetronomeEnabled(metronomeOn);
            metronomeButton.setBackgroundTintList(android.content.res.ColorStateList.valueOf(metronomeOn ? 0xFF34C759 : 0xFFFF3B30));
        });

        metronomeButton.setOnLongClickListener(v -> {
            PopupMenu menu = new PopupMenu(this, v);
            menu.getMenu().add("4/4");
            menu.getMenu().add("2/4");
            menu.getMenu().add("3/4");
            menu.getMenu().add("6/8");
            menu.getMenu().add("12/8");
            menu.setOnMenuItemClickListener(item -> {
                String label = item.getTitle().toString();
                String[] parts = label.split("/");
                int numerator = Integer.parseInt(parts[0]);
                int denominator = Integer.parseInt(parts[1]);
                NativeAudio.setMetronomeTimeSignature(numerator, denominator);
                
                // Si la signature choisie est en /8 (comme 6/8 ou 12/8), c'est une mesure composée (ternaire par nature) -> subdivision = 3
                // Si c'est en /4 (4/4, 2/4, 3/4), c'est du binaire -> subdivision = 4
                if (denominator == 8) {
                    currentSubdivision = 3;
                } else {
                    currentSubdivision = 4;
                }
                
                // Reconstruire immédiatement la grille selon le type de signature choisi
                buildGrid();
                return true;
            });
            menu.show();
            return true;
        });
    }

    private void buildGrid() {
        // En binaire, 4 divisions par temps, on affiche 4 temps = 16 pas
        // En ternaire, 3 divisions par temps, on affiche 4 temps = 12 pas
        int totalSteps = (currentSubdivision == 4) ? 16 : 12;
        
        NativeAudio.setSequencerSubdivision(currentSubdivision);
        NativeAudio.setSequencerLength(totalSteps);

        int marginPx = (int) (2 * getResources().getDisplayMetrics().density);
        int groupGapPx = marginPx * 4;
        int cellSizePx = (int) (45 * getResources().getDisplayMetrics().density);

        // --- GÉNÉRATION DES MARQUEURS DE TEMPS ---
        LinearLayout timeMarkersContainer = findViewById(R.id.container_time_markers);
        if (timeMarkersContainer != null) {
            timeMarkersContainer.removeAllViews();
            int beats = 4; // Toujours 4 temps principaux
            for (int b = 0; b < beats; b++) {
                TextView timeText = new TextView(this);
                timeText.setText("Tps " + (b + 1));
                timeText.setGravity(android.view.Gravity.CENTER);
                timeText.setTextSize(12);
                timeText.setTypeface(null, android.graphics.Typeface.BOLD);
                timeText.setTextColor(0xFF000000);

                // La largeur s'adapte dynamiquement (4 pas en binaire, 3 pas en ternaire)
                int width = (cellSizePx * currentSubdivision) + (marginPx * 2 * currentSubdivision) + (b < beats - 1 ? groupGapPx - marginPx * 2 : 0);
                LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                        width, LinearLayout.LayoutParams.MATCH_PARENT);
                timeText.setLayoutParams(params);
                timeMarkersContainer.addView(timeText);
            }
        }

        // --- GÉNÉRATION DES PISTES ---
        for (int track = 0; track < 3; track++) {
            LinearLayout container = containers[track];
            if (container == null) continue;
            container.removeAllViews();

            java.util.Arrays.fill(stepStates[track], false);

            for (int i = 0; i < totalSteps; i++) {
                NativeAudio.setTrackStepActive(track, i, false);

                Button cell = new Button(this);
                cell.setText("");
                cell.setBackgroundColor(0xFFFFFFFF);

                LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                        cellSizePx, LinearLayout.LayoutParams.MATCH_PARENT);

                boolean endOfGroup = (i + 1) % currentSubdivision == 0 && i != totalSteps - 1;
                params.setMargins(marginPx, marginPx, endOfGroup ? groupGapPx : marginPx, marginPx);
                cell.setLayoutParams(params);

                final int trackIndex = track;
                final int stepIndex = i;
                final Button cellRef = cell;

                cell.setOnClickListener(v -> {
                    stepStates[trackIndex][stepIndex] = !stepStates[trackIndex][stepIndex];
                    cellRef.setBackgroundColor(stepStates[trackIndex][stepIndex] ? 0xFFCEEF34 : 0xFFFFFFFF);
                    NativeAudio.setTrackStepActive(trackIndex, stepIndex, stepStates[trackIndex][stepIndex]);
                });
                container.addView(cell);
            }
        }
    }

    private void setupAutoRepeatBpmButton(Button button, final int direction) {
        final Handler handler = new Handler(Looper.getMainLooper());
        final Runnable runnable = new Runnable() {
            private int executionCount = 0;

            @Override
            public void run() {
                try {
                    String currentText = tvBpm.getText().toString().replace(" bpm", "");
                    int currentBpm = Integer.parseInt(currentText);
                    int nextBpm = currentBpm + direction;

                    if (nextBpm >= 40 && nextBpm <= 200) {
                        NativeAudio.setSequencerBpm(nextBpm);
                        tvBpm.setText(nextBpm + " bpm");
                        executionCount++;

                        // Calcul de l'intervalle dynamique (plus on reste appuyé, plus ça accélère)
                        long nextDelay = 250; // Délai initial (250ms)
                        if (executionCount > 15) {
                            nextDelay = 30;  // Très rapide après 15 répétitions
                        } else if (executionCount > 6) {
                            nextDelay = 80;  // Vitesse intermédiaire
                        }

                        handler.postDelayed(this, nextDelay);
                    }
                } catch (NumberFormatException ignored) {}
            }
        };

        button.setOnTouchListener((v, event) -> {
            switch (event.getAction()) {
                case android.view.MotionEvent.ACTION_DOWN:
                    handler.removeCallbacks(runnable);
                    // Réinitialiser le compteur d'exécutions au début du clic
                    try {
                        // Exécuter un premier coup immédiatement au clic simple
                        String currentText = tvBpm.getText().toString().replace(" bpm", "");
                        int currentBpm = Integer.parseInt(currentText);
                        int nextBpm = currentBpm + direction;
                        if (nextBpm >= 40 && nextBpm <= 200) {
                            NativeAudio.setSequencerBpm(nextBpm);
                            tvBpm.setText(nextBpm + " bpm");
                        }
                    } catch (NumberFormatException ignored) {}
                    
                    // Lancer la répétition après un léger délai de maintien (400ms)
                    handler.postDelayed(runnable, 400);
                    v.setPressed(true);
                    return true;

                case android.view.MotionEvent.ACTION_UP:
                case android.view.MotionEvent.ACTION_CANCEL:
                    handler.removeCallbacks(runnable);
                    v.setPressed(false);
                    return true;
            }
            return false;
        });
    }

    private void savePatternToFile() {
        androidx.appcompat.app.AlertDialog.Builder builder = new androidx.appcompat.app.AlertDialog.Builder(this);
        builder.setTitle("Nommer le pattern");

        final android.widget.EditText input = new android.widget.EditText(this);
        input.setHint("mon_rythme");
        builder.setView(input);

        builder.setPositiveButton("Sauvegarder", (dialog, which) -> {
            String filename = input.getText().toString().trim();
            if (filename.isEmpty()) {
                Toast.makeText(this, "Le nom ne peut pas être vide", Toast.LENGTH_SHORT).show();
                return;
            }
            if (!filename.endsWith(".pbt")) {
                filename += ".pbt";
            }

            try {
                org.json.JSONObject root = new org.json.JSONObject();
                root.put("version", 1);
                
                String currentText = tvBpm.getText().toString().replace(" bpm", "");
                root.put("bpm", Integer.parseInt(currentText));
                root.put("subdivision", currentSubdivision);
                
                int totalSteps = (currentSubdivision == 4) ? 16 : 12;
                root.put("totalSteps", totalSteps);

                org.json.JSONArray tracksArray = new org.json.JSONArray();
                for (int t = 0; t < 3; t++) {
                    org.json.JSONObject trackObj = new org.json.JSONObject();
                    trackObj.put("trackIndex", t);
                    trackObj.put("soundFile", loadedTrackPath[t]);

                    org.json.JSONArray stepsArray = new org.json.JSONArray();
                    for (int s = 0; s < totalSteps; s++) {
                        stepsArray.put(stepStates[t][s]);
                    }
                    trackObj.put("steps", stepsArray);
                    tracksArray.put(trackObj);
                }
                root.put("tracks", tracksArray);

                java.io.File dir = new java.io.File(getExternalFilesDir(null), "SqNat");
                if (!dir.exists()) {
                    dir.mkdirs();
                }

                java.io.File file = new java.io.File(dir, filename);
                java.io.FileWriter writer = new java.io.FileWriter(file);
                writer.write(root.toString(2));
                writer.close();

                Toast.makeText(this, "Sauvegardé sous " + filename + " !", Toast.LENGTH_SHORT).show();
            } catch (Exception e) {
                Toast.makeText(this, "Erreur de sauvegarde: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        });
        builder.setNegativeButton("Annuler", (dialog, which) -> dialog.cancel());
        builder.show();
    }

    private void loadPatternFromFile() {
        java.io.File dir = new java.io.File(getExternalFilesDir(null), "SqNat");
        if (!dir.exists() || dir.listFiles() == null || dir.listFiles().length == 0) {
            Toast.makeText(this, "Aucun pattern trouvé dans le dossier SqNat", Toast.LENGTH_SHORT).show();
            return;
        }

        final java.io.File[] files = dir.listFiles((d, name) -> name.endsWith(".pbt"));
        if (files == null || files.length == 0) {
            Toast.makeText(this, "Aucun fichier .pbt trouvé", Toast.LENGTH_SHORT).show();
            return;
        }

        String[] fileNames = new String[files.length];
        for (int i = 0; i < files.length; i++) {
            fileNames[i] = files[i].getName();
        }

        androidx.appcompat.app.AlertDialog.Builder builder = new androidx.appcompat.app.AlertDialog.Builder(this);
        builder.setTitle("Sélectionner un pattern à charger");
        builder.setItems(fileNames, (dialog, which) -> {
            java.io.File selectedFile = files[which];
            executeLoad(selectedFile);
        });
        builder.show();
    }

    private void executeLoad(java.io.File file) {
        try {
            java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(file));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line);
            }
            reader.close();

            org.json.JSONObject root = new org.json.JSONObject(sb.toString());
            int bpm = root.getInt("bpm");
            tvBpm.setText(bpm + " bpm");
            NativeAudio.setSequencerBpm(bpm);

            // Charger la subdivision si présente dans le fichier (défaut à 4 sinon)
            if (root.has("subdivision")) {
                currentSubdivision = root.getInt("subdivision");
            } else {
                currentSubdivision = 4;
            }

            int totalSteps = (currentSubdivision == 4) ? 16 : 12;

            org.json.JSONArray tracksArray = root.getJSONArray("tracks");
            for (int i = 0; i < tracksArray.length(); i++) {
                org.json.JSONObject trackObj = tracksArray.getJSONObject(i);
                int trackIndex = trackObj.getInt("trackIndex");
                String soundFile = trackObj.getString("soundFile");

                loadedTrackPath[trackIndex] = soundFile;
                NativeAudio.loadTrackSound(trackIndex, getAssets(), soundFile);

                if (trackIndex == 0) track1SoundLoaded = true;
                if (trackIndex == 1) track2SoundLoaded = true;
                if (trackIndex == 2) track3SoundLoaded = true;

                String shortName = soundFile.substring(soundFile.lastIndexOf('/') + 1);
                Button targetButton = null;
                if (trackIndex == 0) targetButton = findViewById(R.id.btn_select_sound_1);
                if (trackIndex == 1) targetButton = findViewById(R.id.btn_select_sound_2);
                if (trackIndex == 2) targetButton = findViewById(R.id.btn_select_sound_3);
                if (targetButton != null) {
                    targetButton.setText(shortName);
                    targetButton.setTextSize(9);
                }

                // Réinitialiser d'abord toute la ligne d'état pour la sécurité
                java.util.Arrays.fill(stepStates[trackIndex], false);

                org.json.JSONArray stepsArray = trackObj.getJSONArray("steps");
                for (int s = 0; s < stepsArray.length() && s < totalSteps; s++) {
                    boolean active = stepsArray.getBoolean(s);
                    stepStates[trackIndex][s] = active;
                    NativeAudio.setTrackStepActive(trackIndex, s, active);
                }
            }

            // Forcer la reconstruction graphique complète de la grille selon la subdivision chargée
            buildGrid();

            // Restaurer visuellement l'état des cases après reconstruction
            for (int track = 0; track < 3; track++) {
                LinearLayout container = containers[track];
                if (container != null) {
                    for (int s = 0; s < totalSteps; s++) {
                        android.view.View cell = container.getChildAt(s);
                        if (cell instanceof Button) {
                            cell.setBackgroundColor(stepStates[track][s] ? 0xFFCEEF34 : 0xFFFFFFFF);
                        }
                    }
                }
            }

            Toast.makeText(this, "Pattern '" + file.getName() + "' chargé !", Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            Toast.makeText(this, "Erreur de chargement: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        beatPollHandler.removeCallbacks(beatPollRunnable);
        NativeAudio.setSequencerPlaying(false);
        NativeAudio.stopSequencerStream();
        NativeAudio.setMetronomeEnabled(false);
        sequencerPlaying = false;
    }
}
