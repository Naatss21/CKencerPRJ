package com.example.ckencer2;


import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.foundation.layout.Column

class MainActivity : ComponentActivity() {

    companion object {
        init {
            System.loadLibrary("ckencer2")
        }
    }

    external fun startAudio()
    external fun stopAudio()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            val isPlaying = remember { mutableStateOf(false) }
            Column {
                Button(onClick = {
                if (isPlaying.value) {
                    stopAudio()
                } else {
                    startAudio()
                }
                isPlaying.value = !isPlaying.value
                }) {
                    Text(if (isPlaying.value) "Stop" else "Play")
                }
            }
        }
    }
}