package dev.kiln.bridgesample

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import dev.kiln.bridge.CppException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout

/**
 * The bridge's checks, both ways, in a process with no activity (kiln's main dispatcher runs
 * without one): `adb shell am broadcast -n dev.kiln.bridgesample/.SelfTest` (tools/bridge-test).
 */
class SelfTest : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val pending = goAsync()
        CoroutineScope(Dispatchers.Main).launch {
            try {
                run()
            } finally {
                pending.finish()
            }
        }
    }

    private var checks = 0
    private val failed = mutableListOf<String>()

    private fun check(name: String, ok: Boolean) {
        checks++
        if (!ok) failed += name
    }

    private suspend fun run() {
        val store = NoteStoreCpp
        store.clear()
        // Plain calls into C++, structs and strings both ways.
        val title = "naïve café 🎉 \u0000 end"
        val id = store.add(title, listOf("a", "ü", ""), null)
        val note = store.find(id)
        check("find returns the note", note == Note(id, title, listOf("a", "ü", ""), false, null))
        check("find of an unknown id is null", store.find(id + 100) == null)
        store.add("second", emptyList(), "body")
        check("all", store.all().map { it.title } == listOf(title, "second"))
        // A C++ exception becomes CppException.
        val e = runCatching { store.remove(id + 100) }.exceptionOrNull()
        check("remove throws CppException", e is CppException && e.type.contains("out_of_range") && e.message!!.contains("no note"))
        // A Kotlin exception through C++ comes back as itself.
        val relayed = runCatching { store.relayFailure("from Kotlin") }.exceptionOrNull()
        check("Kotlin exception back through C++", relayed is IllegalStateException && relayed.message == "from Kotlin")
        // Suspend calls into C++: a task on kiln's dispatcher (its Delay needs the main pump).
        val summary = withTimeout(5000) { store.summarize(20) }
        check("summarize", summary.count == 2 && summary.longest?.id == id && summary.mood == Mood.Calm &&
            summary.tags.map { it.tag } == listOf("", "a", "ü"))
        // From a background thread too: resumes the caller.
        val fromIo = withContext(Dispatchers.IO) { store.summarize(1) }
        check("summarize from Dispatchers.IO", fromIo == summary)
        // Cancelling the Kotlin caller cancels the C++ task.
        val before = store.cancelledSummaries()
        val job = CoroutineScope(Dispatchers.Main).launch { store.summarize(60_000) }
        delay(50)
        job.cancel()
        delay(100)
        check("cancelled summarize cancels the C++ task", store.cancelledSummaries() == before + 1)
        // C++ calling Kotlin.
        val platformFailures = withTimeout(10_000) { store.checkPlatform() }
        for (f in platformFailures) failed += "C++: $f"
        checks += 8
        store.clear()
        val line = "bridge self-test: $checks checks, ${failed.size} failed"
        for (f in failed) Log.e("kiln-bridge", "failed: $f")
        Log.i("kiln-bridge", line)
    }
}
