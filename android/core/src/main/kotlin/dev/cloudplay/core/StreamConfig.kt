package dev.cloudplay.core

data class StreamConfig(
    val width: Int = 1920,
    val height: Int = 1080,
    val fps: Int = 60,
    val bitrateMbps: Int = 12,
) {
    init {
        require(width == 1920 && height == 1080 && fps == 60)
        require(bitrateMbps in 6..20)
    }
}
