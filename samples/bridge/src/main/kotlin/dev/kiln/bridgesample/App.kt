package dev.kiln.bridgesample

import android.app.Application
import android.os.Build
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import java.util.concurrent.atomic.AtomicInteger

/** Platform for C++: registered as the process starts, before any activity. */
class KotlinPlatform : Platform {
    private val cancelled = AtomicInteger()

    override fun deviceName(): String = "${Build.MANUFACTURER} ${Build.MODEL}"
    override fun scale(values: List<Double>, by: Float): List<Double> = values.map { it * by }
    override fun fail(message: String) = throw IllegalStateException(message)
    override suspend fun fetch(text: String, delayMs: Long): ByteArray {
        try {
            delay(delayMs)
        } catch (e: CancellationException) {
            cancelled.incrementAndGet()
            throw e
        }
        return text.toByteArray()
    }
    override fun cancelledFetches(): Int = cancelled.get()
    override suspend fun ping() {}
}

class App : Application() {
    override fun onCreate() {
        super.onCreate()
        PlatformCpp.register(KotlinPlatform())
        if (NoteStoreCpp.all().isEmpty()) {
            NoteStoreCpp.add("Written in Kotlin", listOf("kotlin", "bridge"), "Added by App.onCreate through NoteStoreCpp.")
            NoteStoreCpp.add("Kept in C++", listOf("c++"), null)
        }
    }
}
