// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

import android.app.Activity
import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Display
import android.view.WindowManager
import org.dolphinemu.dolphinemu.features.settings.model.BooleanSetting

/**
 * Shows the companion screen on a secondary display while the emulation activity is visible,
 * and follows displays being connected, removed or switched off.
 *
 * On the AYN Thor the top panel is the default display (where the game renders) and the bottom
 * panel is a presentation display, which is where the companion goes. Single-screen devices
 * simply never find a display, and the native runtime stays detached and idle.
 */
class DuoScreenController(private val activity: Activity) : DisplayManager.DisplayListener {
    private val displayManager = activity.getSystemService(DisplayManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var presentation: DuoPresentation? = null
    private var started = false

    fun start() {
        if (started) return
        started = true
        displayManager.registerDisplayListener(this, handler)
        update()
    }

    fun stop() {
        if (!started) return
        started = false
        displayManager.unregisterDisplayListener(this)
        dismiss()
    }

    /** Call when settings may have changed (for example after the in-game menu closes). */
    fun refresh() {
        if (started) update()
    }

    override fun onDisplayAdded(displayId: Int) = update()
    override fun onDisplayRemoved(displayId: Int) = update()
    override fun onDisplayChanged(displayId: Int) = update()

    private fun update() {
        val enabled = try {
            BooleanSetting.MAIN_DUO_ENABLED.boolean
        } catch (e: Exception) {
            // Settings can be briefly unavailable while the activity is being recreated.
            true
        }
        val display = if (enabled) findCompanionDisplay() else null

        val current = presentation
        if (display == null) {
            dismiss()
            return
        }
        if (current != null && current.isShowing && current.display.displayId == display.displayId) {
            return
        }

        dismiss()
        val newPresentation = DuoPresentation(activity, display)
        try {
            newPresentation.show()
        } catch (e: WindowManager.InvalidDisplayException) {
            Log.w(TAG, "Companion display ${display.displayId} went away before showing", e)
            return
        }
        presentation = newPresentation
        DuoNative.setAttached(true)
    }

    private fun dismiss() {
        presentation?.let {
            if (it.isShowing) it.dismiss()
        }
        presentation = null
        DuoNative.setAttached(false)
    }

    private fun findCompanionDisplay(): Display? {
        @Suppress("DEPRECATION")
        val gameDisplayId = activity.windowManager.defaultDisplay.displayId
        return displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .firstOrNull { it.displayId != gameDisplayId && it.state != Display.STATE_OFF }
    }

    companion object {
        private const val TAG = "DolphinDuo"
    }
}
