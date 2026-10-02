plugins {
    id("com.android.application")
    kotlin("android")
    id("org.jetbrains.kotlin.plugin.compose")
}
android {
    namespace = "dev.cloudplay.app"
    compileSdk = 36
    defaultConfig {
        applicationId = "dev.cloudplay.app"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
    }
    buildFeatures { compose = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    lint {
        warningsAsErrors = true
        // Dependency updates are reviewed deliberately; pinned versions keep builds reproducible.
        disable += "GradleDependency"
    }
}
kotlin { jvmToolchain(17) }
dependencies {
    implementation(project(":core"))
    implementation(project(":networking"))
    implementation(project(":streaming"))
    implementation(project(":input"))
    implementation(project(":ui"))
    implementation(platform("androidx.compose:compose-bom:2025.05.01"))
    implementation("androidx.compose.foundation:foundation-layout")
    implementation("androidx.activity:activity-compose:1.10.1")
}
