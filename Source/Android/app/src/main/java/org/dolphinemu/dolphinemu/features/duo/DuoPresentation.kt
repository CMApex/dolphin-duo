// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

import android.app.Presentation
import android.content.Context
import android.os.Bundle
import android.view.Choreographer
import android.view.Display
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * The companion screen: a window on the handheld's second display (the AYN Thor's bottom
 * screen) that shows whatever the running game's companion publishes.
 */
class DuoPresentation(outerContext: Context, display: Display) :
    Presentation(outerContext, display), DuoCompanionView.Listener {

    private lateinit var view: DuoCompanionView
    private var lastSequence = 0L
    private var polling = false

    // Polls once per display frame; the native side only hands over a snapshot when it changed,
    // so idle frames cost one JNI call and nothing else.
    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!polling) return
            DuoNative.getSnapshot(lastSequence)?.let { raw ->
                DuoSnapshot.parse(raw)?.let { snapshot ->
                    lastSequence = snapshot.sequence
                    view.setSnapshot(snapshot)
                }
            }
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        view = DuoCompanionView(context)
        view.listener = this
        setContentView(view)

        window?.let { window ->
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            WindowCompat.setDecorFitsSystemWindows(window, false)
            WindowInsetsControllerCompat(window, window.decorView).apply {
                hide(WindowInsetsCompat.Type.systemBars())
                systemBarsBehavior =
                    WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        }
    }

    override fun onStart() {
        super.onStart()
        polling = true
        lastSequence = 0L
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    override fun onStop() {
        polling = false
        Choreographer.getInstance().removeFrameCallback(frameCallback)
        super.onStop()
    }

    override fun onPageSelected(pageId: String) = DuoNative.setPage(pageId)

    override fun onAction(action: String, source: String, row: Int) =
        DuoNative.triggerAction(action, source, row)

    override fun onCompanionSelected(companionId: String) = DuoNative.selectCompanion(companionId)
}
