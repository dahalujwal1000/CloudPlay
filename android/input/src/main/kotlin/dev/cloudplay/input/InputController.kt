package dev.cloudplay.input

// Release held controls during disconnect. Wire encoding belongs outside the UI.
interface InputController {
    fun releaseAll()
}
