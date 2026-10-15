// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

/**
 * Native side of Dolphin Duo (Source/Core/Core/Duo). All calls are cheap and thread-safe: they
 * only exchange requests and the latest snapshot with the emulation thread.
 */
object DuoNative {
    /** Starts or stops the companion runtime. It does no work while detached. */
    external fun setAttached(attached: Boolean)

    /** Returns "<sequence>\n<json>" if a snapshot newer than [after] exists, else null. */
    external fun getSnapshot(after: Long): String?

    external fun setPage(page: String)

    /** [rowArray] is the list or map source the tap came from ("" if none), [row] its index. */
    external fun triggerAction(action: String, rowArray: String, row: Int)

    external fun selectCompanion(companionId: String)

    /** Rescans installed companions and reloads the one for the running game. */
    external fun reload()

    external fun getCompanionsDirectory(): String

    external fun getWatchesDirectory(): String

    /** Validates a manifest. Returns JSON with "errors" and, when valid, the companion details. */
    external fun inspectManifest(json: String): String

    /** JSON array describing every installed companion. */
    external fun listInstalled(): String
}
