package com.example.ckencer2;

import android.content.Intent;
import android.os.Bundle;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import androidx.appcompat.app.AppCompatActivity;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Liste les fichiers .wav présents dans assets/sounds/ et renvoie le nom
 * du fichier choisi à MainActivity.
 */
public class SoundListActivity extends AppCompatActivity {

    public static final String EXTRA_SOUND_FILE = "sound_file";
    private static final String SOUNDS_DIR = "sounds";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_sound_list);

        List<String> soundFiles = new ArrayList<>();
        try {
            String[] files = getAssets().list(SOUNDS_DIR);
            if (files != null) {
                for (String f : files) {
                    if (f.toLowerCase().endsWith(".wav")) {
                        soundFiles.add(f);
                    }
                }
            }
        } catch (IOException e) {
            // Le dossier n'existe pas ou est vide : la liste reste vide.
        }

        ListView listView = findViewById(R.id.soundListView);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_list_item_1, soundFiles);
        listView.setAdapter(adapter);

        listView.setOnItemClickListener((parent, view, position, id) -> {
            String chosenFile = soundFiles.get(position);
            Intent result = new Intent();
            result.putExtra(EXTRA_SOUND_FILE, SOUNDS_DIR + "/" + chosenFile);
            setResult(RESULT_OK, result);
            finish();
        });
    }
}