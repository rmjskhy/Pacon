plugins {
    id("com.android.application")
}

android {
    namespace = "com.pacon.bletool"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.pacon.bletool"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
