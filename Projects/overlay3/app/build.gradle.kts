plugins {

    alias(libs.plugins.android.application)

}



android {

    namespace = "com.example.addr2"

    compileSdk = 34 // Using a stable SDK version



    defaultConfig {

        applicationId = "com.example.addr2"

        minSdk = 29 // OpenXR applications generally target higher API levels

        targetSdk = 34

        versionCode = 1

        versionName = "1.0"



        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"



// Configure the external native build to use CMake

        externalNativeBuild {

            cmake {

// Pass arguments to CMake. Using c++_shared is standard.

                arguments += "-DANDROID_STL=c++_shared"

            }

        }
        ndk {
            // This prevents the build error by only building for ARM CPUs
            abiFilters.addAll(listOf("armeabi-v7a", "arm64-v8a"))
        }

    }



    buildTypes {

        release {

            isMinifyEnabled = false

            proguardFiles(

                getDefaultProguardFile("proguard-android-optimize.txt"),

                "proguard-rules.pro"

            )

        }

    }



    compileOptions {

        sourceCompatibility = JavaVersion.VERSION_1_8

        targetCompatibility = JavaVersion.VERSION_1_8

    }



// Point to the CMakeLists.txt file

    externalNativeBuild {

        cmake {

            path = file("src/main/cpp/CMakeLists.txt")

            version = "3.22.1"

        }

    }



// This is a CRITICAL step for packaging pre-compiled .so files.

// This block tells Gradle to include the libopenxr_loader.so files

// from your specified directory into the final APK.

    sourceSets {

        getByName("main") {

            jniLibs.srcDirs("src/main/cpp/openxr/libs")

        }

    }



// Packaging options to prevent conflicts with duplicate libraries.

    packagingOptions {

        jniLibs {

// This ensures that only the ABIs you are building for are included.

            useLegacyPackaging = false

        }

        resources {

            excludes += "/META-INF/{AL2.0,LGPL2.1}"

        }

    }

}



dependencies {

    implementation(libs.appcompat)

    implementation(libs.material)

    implementation(libs.constraintlayout)

    testImplementation(libs.junit)

    androidTestImplementation(libs.ext.junit)

    androidTestImplementation(libs.espresso.core)

}