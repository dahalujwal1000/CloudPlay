plugins {
    kotlin("jvm")
    `java-library`
}
kotlin { jvmToolchain(17) }
dependencies {
    implementation(project(":core"))
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
    compileOnly("org.json:json:20240303")
    testImplementation("org.json:json:20240303")
    testImplementation("junit:junit:4.13.2")
}
