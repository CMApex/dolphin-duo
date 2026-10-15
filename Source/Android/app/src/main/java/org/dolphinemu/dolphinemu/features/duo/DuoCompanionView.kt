// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import android.text.TextUtils
import android.view.GestureDetector
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.widget.OverScroller
import java.io.File
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min

/**
 * Draws a [DuoSnapshot] on the companion screen. Everything is drawn directly on a Canvas so a
 * new snapshot (up to 60 per second) costs one invalidate, not a view hierarchy rebuild.
 */
class DuoCompanionView(context: Context) : View(context) {
    interface Listener {
        fun onPageSelected(pageId: String)
        fun onAction(action: String, source: String, row: Int)
        fun onCompanionSelected(companionId: String)
    }

    var listener: Listener? = null

    private var snapshot = DuoSnapshot.empty()

    private val density = resources.displayMetrics.density
    private val scaledDensity = density * resources.configuration.fontScale
    private fun dp(value: Float) = value * density
    private fun sp(value: Float) = value * scaledDensity

    private object Palette {
        const val BACKGROUND = 0xFF0F1014.toInt()
        const val SURFACE = 0xFF1A1C22.toInt()
        const val SURFACE_RAISED = 0xFF23262E.toInt()
        const val TRACK = 0xFF2C3039.toInt()
        const val TEXT = 0xFFECEDF0.toInt()
        const val TEXT_SECONDARY = 0xFF9BA1AB.toInt()
        const val ACCENT = 0xFF7EA6FF.toInt()
        const val ON_ACCENT = 0xFF0B0D12.toInt()
        const val ERROR = 0xFFFF7A7A.toInt()
        const val ERROR_SURFACE = 0xFF3A1E22.toInt()
        const val DIVIDER = 0xFF2A2D35.toInt()
    }

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }

    private fun textPaint(sizeSp: Float, color: Int, bold: Boolean = false, mono: Boolean = false) =
        TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            textSize = sp(sizeSp)
            this.color = color
            typeface = when {
                mono -> Typeface.MONOSPACE
                bold -> Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
                else -> Typeface.DEFAULT
            }
        }

    private val headingPaint = textPaint(20f, Palette.TEXT, bold = true)
    private val titlePaint = textPaint(17f, Palette.TEXT, bold = true)
    private val bodyPaint = textPaint(15f, Palette.TEXT)
    private val bodyBoldPaint = textPaint(15f, Palette.TEXT, bold = true)
    private val captionPaint = textPaint(12.5f, Palette.TEXT_SECONDARY)
    private val monoPaint = textPaint(14f, Palette.TEXT, mono = true)
    private val smallPaint = textPaint(11f, Palette.TEXT)
    private val tabPaint = textPaint(14f, Palette.TEXT_SECONDARY, bold = true)
    private val scratchPaint = TextPaint(Paint.ANTI_ALIAS_FLAG)

    private val padding = dp(16f)
    private val gap = dp(12f)
    private val headerHeight = dp(44f)
    private val tabHeight = dp(40f)

    private class Hit(val rect: RectF, val onTap: () -> Unit)

    private val hits = ArrayList<Hit>()
    private var contentTop = 0f
    private var contentHeight = 0f
    private var scrollOffset = 0f
    private val scroller = OverScroller(context)

    private val layoutCache = LinkedHashMap<String, StaticLayout>(64, 0.75f, true)
    private val bitmapCache = HashMap<String, Bitmap?>()

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean {
            scroller.forceFinished(true)
            return true
        }

        override fun onScroll(
            e1: MotionEvent?, e2: MotionEvent, distanceX: Float, distanceY: Float
        ): Boolean {
            scrollBy(distanceY)
            return true
        }

        override fun onFling(
            e1: MotionEvent?, e2: MotionEvent, velocityX: Float, velocityY: Float
        ): Boolean {
            scroller.fling(
                0, scrollOffset.toInt(), 0, -velocityY.toInt(),
                0, 0, 0, maxScroll().toInt()
            )
            postInvalidateOnAnimation()
            return true
        }

        override fun onSingleTapUp(e: MotionEvent): Boolean {
            val hit = hits.lastOrNull { it.rect.contains(e.x, e.y) } ?: return false
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            hit.onTap()
            return true
        }
    })

    fun setSnapshot(newSnapshot: DuoSnapshot) {
        if (newSnapshot.page != snapshot.page || newSnapshot.companionId != snapshot.companionId) {
            scrollOffset = 0f
            scroller.forceFinished(true)
        }
        if (newSnapshot.companionDirectory != snapshot.companionDirectory) bitmapCache.clear()
        snapshot = newSnapshot
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean = gestures.onTouchEvent(event)

    override fun computeScroll() {
        if (scroller.computeScrollOffset()) {
            scrollOffset = scroller.currY.toFloat().coerceIn(0f, maxScroll())
            postInvalidateOnAnimation()
        }
    }

    private fun maxScroll() = max(0f, contentHeight - (height - contentTop))

    private fun scrollBy(dy: Float) {
        scrollOffset = (scrollOffset + dy).coerceIn(0f, maxScroll())
        invalidate()
    }

    // ---------------------------------------------------------------------------------------
    // Drawing

    override fun onDraw(canvas: Canvas) {
        hits.clear()
        canvas.drawColor(Palette.BACKGROUND)

        var top = drawHeader(canvas)
        if (snapshot.pages.size > 1) top = drawTabs(canvas, top)
        contentTop = top

        val width = width.toFloat()
        val contentWidth = width - padding * 2

        canvas.save()
        canvas.clipRect(0f, contentTop, width, height.toFloat())
        canvas.translate(0f, contentTop - scrollOffset)

        var y = padding
        if (snapshot.errors.isNotEmpty()) y = drawErrors(canvas, y, contentWidth) + gap

        if (snapshot.widgets.isEmpty() && snapshot.errors.isEmpty()) {
            val text = when (snapshot.status) {
                "idle" -> "Waiting for a game…"
                else -> snapshot.message.ifEmpty { "Nothing to show yet." }
            }
            y = drawWrapped(canvas, text, captionPaint, padding, y, contentWidth) + gap
        }

        for (widget in snapshot.widgets) {
            y = drawWidget(canvas, widget, y, contentWidth) + gap
        }
        contentHeight = y + padding
        canvas.restore()

        scrollOffset = scrollOffset.coerceIn(0f, maxScroll())
        drawNotices(canvas)
    }

    private fun drawHeader(canvas: Canvas): Float {
        val width = width.toFloat()
        fillPaint.color = Palette.SURFACE
        canvas.drawRect(0f, 0f, width, headerHeight, fillPaint)

        val title = snapshot.companionName
            ?: snapshot.gameTitle.ifEmpty { "Dolphin Duo" }
        val subtitle = when {
            snapshot.companionVersion != null -> "v${snapshot.companionVersion}"
            snapshot.gameId.isNotEmpty() -> snapshot.gameId
            else -> ""
        }

        var right = width - padding
        if (snapshot.available.size > 1) {
            // Cycles through the companions installed for this game.
            val label = "Switch"
            val chipWidth = tabPaint.measureText(label) + dp(24f)
            val chip = RectF(right - chipWidth, dp(8f), right, headerHeight - dp(8f))
            fillPaint.color = Palette.SURFACE_RAISED
            canvas.drawRoundRect(chip, dp(14f), dp(14f), fillPaint)
            tabPaint.color = Palette.ACCENT
            drawCentered(canvas, label, tabPaint, chip)
            val ids = snapshot.available.map { it.id }
            val next = ids[(ids.indexOf(snapshot.companionId ?: "") + 1).mod(ids.size)]
            hits.add(Hit(chip) { listener?.onCompanionSelected(next) })
            right = chip.left - dp(8f)
        }

        val baseline = headerHeight / 2 - (titlePaint.descent() + titlePaint.ascent()) / 2
        val subtitleWidth = if (subtitle.isEmpty()) 0f else captionPaint.measureText(subtitle) + dp(8f)
        val titleWidth = right - padding - subtitleWidth
        drawLine(canvas, title, titlePaint, padding, baseline, titleWidth)
        if (subtitle.isNotEmpty()) {
            val used = min(titlePaint.measureText(title), titleWidth)
            canvas.drawText(subtitle, padding + used + dp(8f), baseline, captionPaint)
        }
        return headerHeight
    }

    private fun drawTabs(canvas: Canvas, top: Float): Float {
        val width = width.toFloat()
        val pages = snapshot.pages
        val tabWidth = width / pages.size
        fillPaint.color = Palette.SURFACE
        canvas.drawRect(0f, top, width, top + tabHeight, fillPaint)

        pages.forEachIndexed { i, page ->
            val rect = RectF(i * tabWidth, top, (i + 1) * tabWidth, top + tabHeight)
            val selected = page.id == snapshot.page
            tabPaint.color = if (selected) Palette.TEXT else Palette.TEXT_SECONDARY
            val label = TextUtils.ellipsize(
                page.title, tabPaint, rect.width() - dp(12f), TextUtils.TruncateAt.END
            ).toString()
            drawCentered(canvas, label, tabPaint, rect)
            if (selected) {
                fillPaint.color = Palette.ACCENT
                val inset = rect.width() * 0.2f
                canvas.drawRoundRect(
                    rect.left + inset, rect.bottom - dp(3f), rect.right - inset, rect.bottom,
                    dp(1.5f), dp(1.5f), fillPaint
                )
            }
            hits.add(Hit(rect) { if (!selected) listener?.onPageSelected(page.id) })
        }
        fillPaint.color = Palette.DIVIDER
        canvas.drawRect(0f, top + tabHeight - dp(1f), width, top + tabHeight, fillPaint)
        return top + tabHeight
    }

    private fun drawErrors(canvas: Canvas, top: Float, width: Float): Float {
        val inner = width - dp(24f)
        val lines = snapshot.errors.take(30)
        var height = dp(12f) + titlePaint.fontSpacing
        val layouts = lines.map { layoutFor("• $it", captionPaint, inner) }
        height += layouts.sumOf { it.height.toDouble() }.toFloat() + dp(12f)

        val box = RectF(padding, top, padding + width, top + height)
        fillPaint.color = Palette.ERROR_SURFACE
        canvas.drawRoundRect(box, dp(12f), dp(12f), fillPaint)

        titlePaint.color = Palette.ERROR
        canvas.drawText("Companion problems", box.left + dp(12f), box.top + dp(12f) - titlePaint.ascent(), titlePaint)
        titlePaint.color = Palette.TEXT

        var y = box.top + dp(12f) + titlePaint.fontSpacing
        for (layout in layouts) {
            canvas.save()
            canvas.translate(box.left + dp(12f), y)
            layout.draw(canvas)
            canvas.restore()
            y += layout.height
        }
        return box.bottom
    }

    private fun drawWidget(canvas: Canvas, widget: DuoSnapshot.Widget, top: Float, width: Float): Float =
        when (widget) {
            is DuoSnapshot.Widget.Text -> drawText(canvas, widget, top, width)
            is DuoSnapshot.Widget.Bar -> drawBar(canvas, widget, top, width)
            is DuoSnapshot.Widget.Stats -> drawStats(canvas, widget, top, width)
            is DuoSnapshot.Widget.ListWidget -> drawList(canvas, widget, top, width)
            is DuoSnapshot.Widget.Buttons -> drawButtons(canvas, widget, top, width)
            is DuoSnapshot.Widget.MapWidget -> drawMap(canvas, widget, top, width)
            DuoSnapshot.Widget.Divider -> {
                fillPaint.color = Palette.DIVIDER
                canvas.drawRect(padding, top + dp(4f), padding + width, top + dp(5f), fillPaint)
                top + dp(9f)
            }
        }

    private fun paintForStyle(style: String): TextPaint = when (style) {
        "heading" -> headingPaint
        "title" -> titlePaint
        "caption" -> captionPaint
        "mono" -> monoPaint
        else -> bodyPaint
    }

    private fun drawText(canvas: Canvas, widget: DuoSnapshot.Widget.Text, top: Float, width: Float): Float {
        val base = paintForStyle(widget.style)
        val paint = if (widget.color != null) {
            scratchPaint.set(base)
            scratchPaint.color = widget.color
            scratchPaint
        } else base
        return drawWrapped(canvas, widget.text, paint, padding, top, width)
    }

    private fun drawBar(canvas: Canvas, widget: DuoSnapshot.Widget.Bar, top: Float, width: Float): Float {
        var y = top
        if (widget.label != null || widget.text != null) {
            val baseline = y - captionPaint.ascent()
            widget.label?.let { drawLine(canvas, it, captionPaint, padding, baseline, width * 0.6f) }
            widget.text?.let {
                val textWidth = bodyBoldPaint.measureText(it)
                canvas.drawText(it, padding + width - textWidth, y - bodyBoldPaint.ascent(), bodyBoldPaint)
            }
            y += max(captionPaint.fontSpacing, bodyBoldPaint.fontSpacing) + dp(4f)
        }
        val track = RectF(padding, y, padding + width, y + dp(10f))
        fillPaint.color = Palette.TRACK
        canvas.drawRoundRect(track, dp(5f), dp(5f), fillPaint)
        widget.fraction?.let { fraction ->
            if (fraction > 0f) {
                fillPaint.color = widget.color ?: Palette.ACCENT
                val fill = RectF(track.left, track.top, track.left + track.width() * fraction, track.bottom)
                canvas.drawRoundRect(fill, dp(5f), dp(5f), fillPaint)
            }
        }
        return track.bottom
    }

    private fun drawStats(canvas: Canvas, widget: DuoSnapshot.Widget.Stats, top: Float, width: Float): Float {
        if (widget.items.isEmpty()) return top
        val columns = widget.columns
        val cellGap = dp(8f)
        val cellWidth = (width - cellGap * (columns - 1)) / columns
        val cellHeight = dp(12f) + captionPaint.fontSpacing + bodyBoldPaint.fontSpacing + dp(8f)
        val rows = ceil(widget.items.size / columns.toFloat()).toInt()

        widget.items.forEachIndexed { i, (label, value) ->
            val column = i % columns
            val row = i / columns
            val left = padding + column * (cellWidth + cellGap)
            val cellTop = top + row * (cellHeight + cellGap)
            val rect = RectF(left, cellTop, left + cellWidth, cellTop + cellHeight)
            fillPaint.color = Palette.SURFACE
            canvas.drawRoundRect(rect, dp(10f), dp(10f), fillPaint)
            val inner = cellWidth - dp(20f)
            drawLine(canvas, label, captionPaint, left + dp(10f), cellTop + dp(8f) - captionPaint.ascent(), inner)
            drawLine(
                canvas, value, bodyBoldPaint, left + dp(10f),
                cellTop + dp(8f) + captionPaint.fontSpacing - bodyBoldPaint.ascent(), inner
            )
        }
        return top + rows * cellHeight + (rows - 1) * cellGap
    }

    private fun drawList(canvas: Canvas, widget: DuoSnapshot.Widget.ListWidget, top: Float, width: Float): Float {
        if (widget.rows.isEmpty()) {
            return drawWrapped(canvas, widget.empty ?: "Nothing here right now.", captionPaint, padding, top, width)
        }

        val hasDetail = widget.rows.any { it.detail != null }
        val rowHeight = if (hasDetail) dp(58f) else dp(46f)
        val box = RectF(padding, top, padding + width, top + rowHeight * widget.rows.size)
        fillPaint.color = Palette.SURFACE
        canvas.drawRoundRect(box, dp(12f), dp(12f), fillPaint)

        canvas.save()
        val clip = Path().apply { addRoundRect(box, dp(12f), dp(12f), Path.Direction.CW) }
        canvas.clipPath(clip)

        widget.rows.forEachIndexed { i, row ->
            val rowTop = top + i * rowHeight
            val rect = RectF(box.left, rowTop, box.right, rowTop + rowHeight)
            if (i > 0) {
                fillPaint.color = Palette.DIVIDER
                canvas.drawRect(rect.left + dp(12f), rowTop, rect.right, rowTop + dp(1f), fillPaint)
            }
            row.color?.let {
                fillPaint.color = it
                canvas.drawRect(rect.left, rect.top, rect.left + dp(4f), rect.bottom, fillPaint)
            }

            var textRight = rect.right - dp(12f)
            row.badge?.takeIf { it.isNotEmpty() }?.let { badge ->
                val badgeWidth = smallPaint.measureText(badge) + dp(16f)
                val badgeRect = RectF(
                    textRight - badgeWidth, rect.centerY() - dp(11f), textRight, rect.centerY() + dp(11f)
                )
                fillPaint.color = (row.color ?: Palette.ACCENT) and 0x00FFFFFF or 0x33000000
                canvas.drawRoundRect(badgeRect, dp(11f), dp(11f), fillPaint)
                smallPaint.color = row.color ?: Palette.ACCENT
                drawCentered(canvas, badge, smallPaint, badgeRect)
                smallPaint.color = Palette.TEXT
                textRight = badgeRect.left - dp(8f)
            }

            val textLeft = rect.left + dp(14f)
            val textWidth = textRight - textLeft
            if (row.detail != null) {
                val textBaseline = rowTop + dp(9f) - bodyPaint.ascent()
                drawLine(canvas, row.text, bodyPaint, textLeft, textBaseline, textWidth)
                drawLine(canvas, row.detail, captionPaint, textLeft, textBaseline + dp(4f) + captionPaint.fontSpacing, textWidth)
            } else {
                val baseline = rect.centerY() - (bodyPaint.descent() + bodyPaint.ascent()) / 2
                drawLine(canvas, row.text, bodyPaint, textLeft, baseline, textWidth)
            }

            row.fraction?.let { fraction ->
                val barTop = rect.bottom - dp(4f)
                fillPaint.color = Palette.TRACK
                canvas.drawRect(textLeft, barTop, rect.right - dp(12f), barTop + dp(2.5f), fillPaint)
                fillPaint.color = row.color ?: Palette.ACCENT
                canvas.drawRect(
                    textLeft, barTop, textLeft + (rect.right - dp(12f) - textLeft) * fraction,
                    barTop + dp(2.5f), fillPaint
                )
            }

            widget.action?.let { action ->
                addContentHit(rect) { listener?.onAction(action, widget.source, row.index) }
            }
        }
        canvas.restore()
        return box.bottom
    }

    private fun drawButtons(canvas: Canvas, widget: DuoSnapshot.Widget.Buttons, top: Float, width: Float): Float {
        val buttonHeight = dp(46f)
        val spacing = dp(8f)
        val buttons = widget.buttons
        if (buttons.isEmpty()) return top

        // Lay out greedily into rows, then stretch each row to the full width.
        val rows = ArrayList<List<DuoSnapshot.Button>>()
        var current = ArrayList<DuoSnapshot.Button>()
        var used = 0f
        for (button in buttons) {
            val natural = bodyBoldPaint.measureText(button.label) + dp(32f)
            if (current.isNotEmpty() && used + spacing + natural > width) {
                rows.add(current)
                current = ArrayList()
                used = 0f
            }
            used += (if (current.isEmpty()) 0f else spacing) + natural
            current.add(button)
        }
        rows.add(current)

        var y = top
        for (row in rows) {
            val buttonWidth = (width - spacing * (row.size - 1)) / row.size
            row.forEachIndexed { i, button ->
                val left = padding + i * (buttonWidth + spacing)
                val rect = RectF(left, y, left + buttonWidth, y + buttonHeight)
                fillPaint.color = if (button.enabled) Palette.ACCENT else Palette.SURFACE_RAISED
                canvas.drawRoundRect(rect, dp(12f), dp(12f), fillPaint)
                bodyBoldPaint.color = if (button.enabled) Palette.ON_ACCENT else Palette.TEXT_SECONDARY
                val label = TextUtils.ellipsize(
                    button.label, bodyBoldPaint, rect.width() - dp(16f), TextUtils.TruncateAt.END
                ).toString()
                drawCentered(canvas, label, bodyBoldPaint, rect)
                bodyBoldPaint.color = Palette.TEXT
                if (button.enabled) addContentHit(rect) { listener?.onAction(button.action, "", -1) }
            }
            y += buttonHeight + spacing
        }
        return y - spacing
    }

    private fun drawMap(canvas: Canvas, widget: DuoSnapshot.Widget.MapWidget, top: Float, width: Float): Float {
        val viewportHeight = height - contentTop - padding * 2
        var mapWidth = width
        var mapHeight = width * widget.aspect
        if (mapHeight > viewportHeight && viewportHeight > 0) {
            mapHeight = viewportHeight
            mapWidth = mapHeight / widget.aspect
        }
        val left = padding + (width - mapWidth) / 2
        val rect = RectF(left, top, left + mapWidth, top + mapHeight)

        fillPaint.color = Palette.SURFACE
        canvas.drawRoundRect(rect, dp(12f), dp(12f), fillPaint)

        val bitmap = widget.image?.let { loadImage(it) }
        canvas.save()
        canvas.clipPath(Path().apply { addRoundRect(rect, dp(12f), dp(12f), Path.Direction.CW) })
        if (bitmap != null) {
            canvas.drawBitmap(bitmap, null, rect, fillPaint.apply { isFilterBitmap = true })
        } else {
            strokePaint.color = Palette.DIVIDER
            strokePaint.strokeWidth = dp(1f)
            for (i in 1 until 8) {
                val x = rect.left + rect.width() * i / 8
                val y = rect.top + rect.height() * i / 8
                canvas.drawLine(x, rect.top, x, rect.bottom, strokePaint)
                canvas.drawLine(rect.left, y, rect.right, y, strokePaint)
            }
        }

        val showLabels = widget.points.size <= 40
        for (point in widget.points) {
            val cx = rect.left + rect.width() * point.x
            val cy = rect.top + rect.height() * point.y
            drawMarker(canvas, point, cx, cy)
            if (showLabels && !point.label.isNullOrEmpty()) {
                smallPaint.color = Palette.TEXT
                smallPaint.setShadowLayer(dp(2f), 0f, 0f, Color.BLACK)
                canvas.drawText(point.label, cx + dp(8f), cy + smallPaint.textSize / 3, smallPaint)
                smallPaint.clearShadowLayer()
            }
            point.action?.let { action ->
                val touch = dp(14f)
                addContentHit(RectF(cx - touch, cy - touch, cx + touch, cy + touch)) {
                    listener?.onAction(action, point.source ?: "", point.index)
                }
            }
        }
        canvas.restore()
        return rect.bottom
    }

    private fun drawMarker(canvas: Canvas, point: DuoSnapshot.Point, cx: Float, cy: Float) {
        val color = point.color ?: Palette.ACCENT
        fillPaint.color = color
        strokePaint.color = Palette.BACKGROUND
        strokePaint.strokeWidth = dp(1.5f)
        when (point.shape) {
            "player" -> {
                val r = dp(8f)
                val path = Path().apply {
                    moveTo(cx, cy - r)
                    lineTo(cx + r * 0.87f, cy + r * 0.5f)
                    lineTo(cx - r * 0.87f, cy + r * 0.5f)
                    close()
                }
                fillPaint.color = point.color ?: Color.WHITE
                canvas.drawPath(path, fillPaint)
                canvas.drawPath(path, strokePaint)
            }

            "square" -> {
                val r = dp(5f)
                canvas.drawRect(cx - r, cy - r, cx + r, cy + r, fillPaint)
                canvas.drawRect(cx - r, cy - r, cx + r, cy + r, strokePaint)
            }

            "diamond" -> {
                val r = dp(6.5f)
                val path = Path().apply {
                    moveTo(cx, cy - r); lineTo(cx + r, cy); lineTo(cx, cy + r); lineTo(cx - r, cy); close()
                }
                canvas.drawPath(path, fillPaint)
                canvas.drawPath(path, strokePaint)
            }

            else -> {
                canvas.drawCircle(cx, cy, dp(5f), fillPaint)
                canvas.drawCircle(cx, cy, dp(5f), strokePaint)
            }
        }
    }

    private fun drawNotices(canvas: Canvas) {
        if (snapshot.notices.isEmpty()) return
        val width = width - padding * 2
        val layouts = snapshot.notices.takeLast(3).map { layoutFor(it, bodyPaint, width - dp(24f)) }
        val height = layouts.sumOf { it.height.toDouble() }.toFloat() + dp(20f)
        val box = RectF(padding, this.height - padding - height, padding + width, this.height - padding)
        fillPaint.color = Palette.SURFACE_RAISED
        canvas.drawRoundRect(box, dp(12f), dp(12f), fillPaint)
        var y = box.top + dp(10f)
        for (layout in layouts) {
            canvas.save()
            canvas.translate(box.left + dp(12f), y)
            layout.draw(canvas)
            canvas.restore()
            y += layout.height
        }
    }

    // ---------------------------------------------------------------------------------------
    // Helpers

    /** Registers a tap target drawn inside the scrolling content area. */
    private fun addContentHit(rect: RectF, onTap: () -> Unit) {
        val onScreen = RectF(rect).apply { offset(0f, contentTop - scrollOffset) }
        if (onScreen.bottom < contentTop || onScreen.top > height) return
        onScreen.top = max(onScreen.top, contentTop)
        hits.add(Hit(onScreen, onTap))
    }

    private fun drawLine(canvas: Canvas, text: String, paint: TextPaint, x: Float, baseline: Float, maxWidth: Float) {
        if (maxWidth <= 0f) return
        val fitted = TextUtils.ellipsize(text, paint, maxWidth, TextUtils.TruncateAt.END)
        canvas.drawText(fitted, 0, fitted.length, x, baseline, paint)
    }

    private fun drawCentered(canvas: Canvas, text: String, paint: Paint, rect: RectF) {
        val x = rect.centerX() - paint.measureText(text) / 2
        val baseline = rect.centerY() - (paint.descent() + paint.ascent()) / 2
        canvas.drawText(text, x, baseline, paint)
    }

    private fun drawWrapped(canvas: Canvas, text: String, paint: TextPaint, x: Float, top: Float, width: Float): Float {
        if (text.isEmpty() || width <= 0f) return top
        val layout = layoutFor(text, paint, width)
        canvas.save()
        canvas.translate(x, top)
        layout.draw(canvas)
        canvas.restore()
        return top + layout.height
    }

    private fun layoutFor(text: String, paint: TextPaint, width: Float): StaticLayout {
        val key = "${System.identityHashCode(paint)}|${paint.color}|${width.toInt()}|$text"
        layoutCache[key]?.let { return it }
        val layout = StaticLayout.Builder
            .obtain(text, 0, text.length, TextPaint(paint), width.toInt().coerceAtLeast(1))
            .setAlignment(Layout.Alignment.ALIGN_NORMAL)
            .setIncludePad(false)
            .build()
        if (layoutCache.size > 200) layoutCache.remove(layoutCache.keys.first())
        layoutCache[key] = layout
        return layout
    }

    private fun loadImage(relativePath: String): Bitmap? {
        val directory = snapshot.companionDirectory ?: return null
        return bitmapCache.getOrPut(relativePath) {
            val root = File(directory).canonicalFile
            val file = File(root, relativePath).canonicalFile
            // Only images inside the companion's own folder.
            if (!file.path.startsWith(root.path + File.separator) || !file.isFile) return@getOrPut null
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.path, bounds)
            var sample = 1
            while (bounds.outWidth / sample > 2048 || bounds.outHeight / sample > 2048) sample *= 2
            BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample })
        }
    }
}
