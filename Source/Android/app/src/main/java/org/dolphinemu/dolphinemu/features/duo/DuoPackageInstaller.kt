// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

import android.content.Context
import android.net.Uri
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * Installs companion packages: either a `.zip` holding `manifest.json` (plus optional images),
 * or a bare `manifest.json`. Packages are unpacked into a staging folder, validated by the same
 * native parser the runtime uses, and only then moved into `Duo/Companions/<id>/`.
 */
object DuoPackageInstaller {
    private const val MAX_TOTAL_BYTES = 64L * 1024 * 1024
    private const val MAX_FILE_BYTES = 32L * 1024 * 1024
    private const val MAX_ENTRIES = 1024
    private const val MANIFEST = "manifest.json"

    data class Details(
        val id: String,
        val name: String,
        val version: String,
        val author: String,
        val description: String,
        val games: List<String>,
        val writesMemory: Boolean,
        val pressesButtons: Boolean
    )

    /** A validated package waiting in a staging folder for the user to confirm. */
    class Prepared internal constructor(val details: Details, internal val stagingDir: File)

    class InstallException(message: String) : Exception(message)

    /** Reads and validates a package. Blocking; call off the main thread. */
    @Throws(InstallException::class)
    fun prepare(context: Context, uri: Uri): Prepared {
        val companions = File(DuoNative.getCompanionsDirectory())
        val staging = File(companions.parentFile, "Staging/${System.nanoTime()}")
        staging.deleteRecursively()
        if (!staging.mkdirs()) throw InstallException("Could not create $staging")

        try {
            val bytes = context.contentResolver.openInputStream(uri)?.use { readLimited(it) }
                ?: throw InstallException("Could not open the file.")

            if (looksLikeJson(bytes)) {
                File(staging, MANIFEST).writeBytes(bytes)
            } else {
                unzip(bytes, staging)
            }

            val root = findPackageRoot(staging)
                ?: throw InstallException("No manifest.json found in the package.")
            val info = JSONObject(DuoNative.inspectManifest(File(root, MANIFEST).readText()))
            val errors = info.optJSONArray("errors")
            if (errors != null && errors.length() > 0) {
                val lines = (0 until minOf(errors.length(), 12)).joinToString("\n") {
                    "• " + errors.optString(it)
                }
                val more = if (errors.length() > 12) "\n…and ${errors.length() - 12} more" else ""
                throw InstallException(lines + more)
            }

            val targets = info.optJSONArray("targets")
            val games = (0 until (targets?.length() ?: 0)).map { i ->
                val target = targets!!.getJSONObject(i)
                val revisions = target.optJSONArray("revisions")
                val revisionText = if (revisions == null || revisions.length() == 0) "" else
                    " (rev " + (0 until revisions.length()).joinToString("/") {
                        revisions.optInt(it).toString()
                    } + ")"
                target.optString("game_id") + revisionText
            }

            val details = Details(
                id = info.optString("id"),
                name = info.optString("name"),
                version = info.optString("version"),
                author = info.optString("author"),
                description = info.optString("description"),
                games = games,
                writesMemory = info.optBoolean("write_memory"),
                pressesButtons = info.optBoolean("input")
            )
            return Prepared(details, root)
        } catch (e: InstallException) {
            staging.deleteRecursively()
            throw e
        } catch (e: IOException) {
            staging.deleteRecursively()
            throw InstallException(e.message ?: "Could not read the package.")
        } catch (e: org.json.JSONException) {
            staging.deleteRecursively()
            throw InstallException("The package could not be inspected.")
        }
    }

    /** Moves a prepared package into place, replacing an older install of the same companion. */
    @Throws(InstallException::class)
    fun commit(prepared: Prepared) {
        val companions = File(DuoNative.getCompanionsDirectory())
        companions.mkdirs()
        val target = File(companions, prepared.details.id)
        val backup = File(companions.parentFile, "Staging/replaced-${System.nanoTime()}")

        if (target.exists() && !target.renameTo(backup)) {
            discard(prepared)
            throw InstallException("Could not replace the installed version.")
        }
        if (!prepared.stagingDir.renameTo(target)) {
            // Fall back to copying (staging and target can be on different volumes).
            try {
                prepared.stagingDir.copyRecursively(target, overwrite = true)
            } catch (e: IOException) {
                target.deleteRecursively()
                backup.renameTo(target)
                discard(prepared)
                throw InstallException("Could not install: ${e.message}")
            }
        }
        backup.deleteRecursively()
        discard(prepared)
    }

    fun discard(prepared: Prepared) {
        // The staging folder is either the package root or its parent.
        val staging = File(DuoNative.getCompanionsDirectory()).parentFile?.let { File(it, "Staging") }
        var dir: File? = prepared.stagingDir
        while (dir != null && dir.parentFile != staging) dir = dir.parentFile
        (dir ?: prepared.stagingDir).deleteRecursively()
    }

    fun uninstall(directory: String): Boolean {
        val companions = File(DuoNative.getCompanionsDirectory()).canonicalFile
        val dir = File(directory).canonicalFile
        // Never delete anything outside the companions folder.
        if (dir.parentFile != companions) return false
        return dir.deleteRecursively()
    }

    private fun readLimited(input: InputStream): ByteArray {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(64 * 1024)
        var total = 0L
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            total += read
            if (total > MAX_TOTAL_BYTES) throw InstallException("The package is too large.")
            out.write(buffer, 0, read)
        }
        return out.toByteArray()
    }

    private fun looksLikeJson(bytes: ByteArray): Boolean {
        val first = bytes.firstOrNull { !it.toInt().toChar().isWhitespace() } ?: return false
        return first == '{'.code.toByte()
    }

    private fun unzip(bytes: ByteArray, destination: File) {
        val root = destination.canonicalFile
        var entries = 0
        var total = 0L
        ZipInputStream(bytes.inputStream()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                if (++entries > MAX_ENTRIES) throw InstallException("The package has too many files.")
                val name = entry.name.replace('\\', '/')
                if (name.startsWith("__MACOSX/") || name.endsWith("/.DS_Store")) continue
                if (name.startsWith("/") || name.split('/').any { it == ".." }) {
                    throw InstallException("The package contains an unsafe path: $name")
                }
                val file = File(root, name).canonicalFile
                if (!file.path.startsWith(root.path + File.separator)) {
                    throw InstallException("The package contains an unsafe path: $name")
                }
                if (entry.isDirectory) {
                    file.mkdirs()
                    continue
                }
                file.parentFile?.mkdirs()
                var size = 0L
                file.outputStream().use { out ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val read = zip.read(buffer)
                        if (read < 0) break
                        size += read
                        total += read
                        if (size > MAX_FILE_BYTES || total > MAX_TOTAL_BYTES) {
                            throw InstallException("The package is too large once unpacked.")
                        }
                        out.write(buffer, 0, read)
                    }
                }
            }
        }
    }

    /** The folder holding manifest.json: the staging root, or a single top-level folder. */
    private fun findPackageRoot(staging: File): File? {
        if (File(staging, MANIFEST).isFile) return staging
        val children = staging.listFiles()?.filter { it.name != "__MACOSX" } ?: return null
        val only = children.singleOrNull() ?: return null
        return if (only.isDirectory && File(only, MANIFEST).isFile) only else null
    }
}
