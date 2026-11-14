package dev.kiln.bridgesample

import dev.kiln.bridge.CalledFromCpp
import dev.kiln.bridge.CppStruct
import dev.kiln.bridge.ImplementedInCpp

// What crosses between Kotlin and C++ here. tools/gen-jvm writes both sides from this file:
// NoteStoreCpp and PlatformCpp in Kotlin, kiln_bridge.h in C++.

@CppStruct
enum class Mood { Calm, Busy }

@CppStruct
data class Note(val id: Long, val title: String, val tags: List<String>, val pinned: Boolean, val body: String?)

@CppStruct
data class TagCount(val tag: String, val count: Int)

@CppStruct
data class Summary(val count: Int, val longest: Note?, val mood: Mood, val tags: List<TagCount>)

/** Notes kept in C++ (main.cpp), which its UI shows. */
@ImplementedInCpp
interface NoteStore {
    fun add(title: String, tags: List<String>, body: String?): Long
    fun find(id: Long): Note?
    fun all(): List<Note>
    /** Throws (std::out_of_range) for an unknown id. */
    fun remove(id: Long)
    fun clear()
    /** Waits delayMs on kiln's main dispatcher, then summarizes. */
    suspend fun summarize(delayMs: Long): Summary
    /** Summaries cancelled while they waited. */
    fun cancelledSummaries(): Int
    /** Calls Platform.fail: Kotlin's exception comes back through C++. */
    fun relayFailure(message: String)
    /** C++ calling Platform: the checks that failed. */
    suspend fun checkPlatform(): List<String>
}

/** Implemented in Kotlin (App.kt) and called from C++. */
@CalledFromCpp
interface Platform {
    fun deviceName(): String
    fun scale(values: List<Double>, by: Float): List<Double>
    fun fail(message: String)
    /** The text's UTF-8 bytes, after delayMs. */
    suspend fun fetch(text: String, delayMs: Long): ByteArray
    /** Fetches cancelled while they waited. */
    fun cancelledFetches(): Int
    suspend fun ping()
}
