plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.example.ckencer2"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.example.ckencer2"
        minSdk = 27
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

    }

    buildTypes {
        release {
            optimization {
                enable = false
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true
        prefab = true
        compose = true
    }
    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.8" // Ou selon ta version de Kotlin
    }

}

dependencies {
    implementation(libs.appcompat)
    implementation(libs.constraintlayout)
    implementation(libs.material)
    testImplementation(libs.junit)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(libs.ext.junit)
    implementation("com.google.oboe:oboe:1.9.0")
}