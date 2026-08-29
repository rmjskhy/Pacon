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
        versionCode = 2
        versionName = "0.2"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
