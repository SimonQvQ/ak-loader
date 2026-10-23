plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "dev.ak.ak_loader"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = "27.1.12297006"

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "dev.ak.ak_loader"
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        // The application version is maintained in pubspec.yaml.
        versionCode = flutter.versionCode
        versionName = flutter.versionName

        // build-waydroid.sh selects x86_64; direct arm64 builds can select arm64-v8a.
        val abiFilter = (project.findProperty("abi") as String?)
            ?.split(",")?.map { it.trim() }?.filter { it.isNotEmpty() }
        if (!abiFilter.isNullOrEmpty()) {
            ndk { abiFilters += abiFilter }
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    buildTypes {
        release {
            // Local/CI builds use that environment's debug key; see docs/BUILDING.md.
            signingConfig = signingConfigs.getByName("debug")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}

dependencies {
    // Xposed/LSPosed 模块编译期 API（运行时由框架提供）
    compileOnly(files("libs/api-82.jar"))
}
