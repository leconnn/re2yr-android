// Android app: a thin Java shell (SDLActivity) around the native engine.
// The native code is built from the repository's top-level CMakeLists.txt.

plugins {
    id("com.android.application") version "8.13.2"
}

android {
    namespace = "io.github.leconnn.re2yr"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "io.github.leconnn.re2yr"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.0.1"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DRA_BUILD_TESTS=OFF", "-DANDROID_STL=c++_static")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.31.6"
        }
    }

    sourceSets {
        getByName("main") {
            // SDL's Java side (SDLActivity and friends) comes from the SDL submodule.
            java.srcDirs("src/main/java", "../external/SDL/android-project/app/src/main/java")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
