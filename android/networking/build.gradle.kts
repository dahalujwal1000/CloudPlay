plugins {
    kotlin("jvm")
    `java-library`
}
kotlin { jvmToolchain(17) }
dependencies {
    implementation(project(":core"))
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
}
