plugins {
    id("com.android.application") version "8.11.1" apply false
    id("com.android.library") version "8.11.1" apply false
    kotlin("android") version "2.1.21" apply false
    kotlin("jvm") version "2.1.21" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.1.21" apply false
    id("com.diffplug.spotless") version "7.0.4"
}

spotless {
    kotlin {
        target("**/src/**/*.kt")
        ktlint("1.5.0")
    }
    kotlinGradle {
        target("**/*.gradle.kts")
        targetExclude("**/build/**", "**/.gradle/**")
        ktlint("1.5.0")
    }
}
