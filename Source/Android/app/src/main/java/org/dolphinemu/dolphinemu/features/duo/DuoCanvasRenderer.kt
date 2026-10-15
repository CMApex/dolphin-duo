// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffColorFilter
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.text.TextPaint
import androidx.core.graphics.PathParser
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import kotlin.math.max
import kotlin.math.min

/**
 * Draws a canvas page: elements the native runtime has already positioned on the layout's design
 * canvas (for example 1240x1080 on the AYN Thor). The canvas is scaled to fit the screen and
 * centred; the background colour fills any margin.
 *
 * Keep the drawing rules in step with Tools/DolphinDuo/preview/duo-preview.js, which renders the
 * same snapshots on desktop for authoring.
 */
class DuoCanvasRenderer {
    class Hit(val rect: RectF, val action: String, val source: String, val index: Int)

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = TextPaint(Paint.ANTI_ALIAS_FLAG)
    private val bitmapPaint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)

    private val typefaces = HashMap<String, Typeface>()
    private val bitmaps = HashMap<String, Bitmap?>()
    private val discFailures = HashMap<String, Long>()
    private val paths = LinkedHashMap<String, Path?>(64, 0.75f, true)
    private var directory: String? = null

    /** Draws [canvas] JSON + [elements] into a view of [viewWidth] x [viewHeight]. */
    fun draw(
        target: Canvas, viewWidth: Int, viewHeight: Int, info: DuoSnapshot.CanvasInfo,
        elements: JSONArray, companionDirectory: String?, hits: MutableList<Hit>
    ) {
        if (companionDirectory != directory) {
            directory = companionDirectory
            typefaces.clear()
            bitmaps.clear()
            discFailures.clear()
        }
        val scale = min(viewWidth / info.width, viewHeight / info.height)
        val dx = (viewWidth - info.width * scale) / 2f
        val dy = (viewHeight - info.height * scale) / 2f

        target.drawColor(info.background ?: Color.BLACK)
        target.save()
        target.translate(dx, dy)
        target.scale(scale, scale)
        target.clipRect(0f, 0f, info.width, info.height)

        for (i in 0 until elements.length()) {
            val e = elements.optJSONObject(i) ?: continue
            val opacity = e.optDouble("opacity", 1.0).toFloat().coerceIn(0f, 1f)
            if (opacity <= 0f) continue
            val layer = opacity < 1f
            if (layer) target.saveLayerAlpha(0f, 0f, info.width, info.height, (opacity * 255).toInt())
            when (e.optString("t")) {
                "rect" -> drawRect(target, e)
                "frame" -> drawFrame(target, e)
                "text" -> drawText(target, e, info)
                "bar" -> drawBar(target, e)
                "pips" -> drawPips(target, e)
                "image" -> drawImage(target, e)
                "path" -> drawPath(target, e)
                "grid" -> drawGrid(target, e, scale, dx, dy, hits)
            }
            if (layer) target.restore()

            val action = e.optStringOrNull("action")
            if (action != null) {
                val r = box(e)
                hits.add(
                    Hit(
                        RectF(dx + r.left * scale, dy + r.top * scale, dx + r.right * scale, dy + r.bottom * scale),
                        action, e.optString("source", ""), e.optInt("index", -1)
                    )
                )
            }
        }
        target.restore()
    }

    // -------------------------------------------------------------------------------------------
    // Elements

    private fun drawRect(c: Canvas, e: JSONObject) {
        val r = box(e)
        val radius = e.num("radius", 0f)
        applyFill(e, r)?.let { c.drawRoundRect(r, radius, radius, it) }
        color(e, "stroke")?.let {
            stroke.color = it
            stroke.strokeWidth = e.num("stroke_width", 1f)
            val half = stroke.strokeWidth / 2
            c.drawRoundRect(inset(r, half), radius, radius, stroke)
        }
    }

    /**
     * An ornamented window: dark outer edge, gradient body, a thin trim line inset from the edge
     * and corner ornaments. `corner` is one of none, notch (clipped corners), diamond, bracket.
     */
    private fun drawFrame(c: Canvas, e: JSONObject) {
        val r = box(e)
        val corner = e.optString("corner", "notch")
        val size = e.num("corner_size", 14f)
        val radius = e.num("radius", 0f)
        val outline = framePath(r, if (corner == "notch") size else 0f, radius)

        applyFill(e, r)?.let { c.drawPath(outline, it) }
        color(e, "glow")?.let { glow ->
            // A soft sheen across the top third, the way Tellius windows catch the light.
            fill.shader = LinearGradient(
                0f, r.top, 0f, r.top + r.height() * 0.45f, glow, glow and 0x00FFFFFF, Shader.TileMode.CLAMP
            )
            c.save()
            c.clipPath(outline)
            c.drawRect(r, fill)
            c.restore()
            fill.shader = null
        }
        color(e, "edge")?.let {
            stroke.color = it
            stroke.strokeWidth = e.num("stroke_width", 2f)
            c.drawPath(framePath(inset(r, stroke.strokeWidth / 2), if (corner == "notch") size else 0f, radius), stroke)
        }
        val trim = color(e, "trim") ?: return
        val inset = e.num("inset", 5f)
        val trimWidth = e.num("trim_width", 1.5f)
        val inner = inset(r, inset)
        stroke.color = trim
        stroke.strokeWidth = trimWidth
        val innerCut = if (corner == "notch") max(0f, size - inset * 0.6f) else 0f
        c.drawPath(framePath(inner, innerCut, max(0f, radius - inset)), stroke)

        fill.color = trim
        fill.shader = null
        when (corner) {
            "diamond" -> for ((x, y) in corners(inner)) diamond(c, x, y, size * 0.35f, fill)
            "notch" -> for ((x, y) in corners(inner)) {
                // A small lozenge sitting on each clipped corner.
                val ox = if (x < inner.centerX()) innerCut * 0.5f else -innerCut * 0.5f
                val oy = if (y < inner.centerY()) innerCut * 0.5f else -innerCut * 0.5f
                diamond(c, x + ox, y + oy, size * 0.22f, fill)
            }
            "bracket" -> {
                stroke.strokeWidth = trimWidth * 2
                for ((x, y) in corners(inner)) {
                    val sx = if (x < inner.centerX()) 1 else -1
                    val sy = if (y < inner.centerY()) 1 else -1
                    c.drawLine(x, y, x + sx * size, y, stroke)
                    c.drawLine(x, y, x, y + sy * size, stroke)
                }
            }
        }
    }

    private fun drawText(c: Canvas, e: JSONObject, info: DuoSnapshot.CanvasInfo) {
        var value = e.optString("text", "")
        if (value.isEmpty()) return
        if (e.optString("case") == "upper") value = value.uppercase()
        val r = box(e)
        text.reset()
        text.isAntiAlias = true
        text.typeface = typeface(e.optString("font", "serif"), e.optString("weight") == "bold", info)
        text.textSize = e.num("size", r.height() * 0.7f)
        text.letterSpacing = e.num("letter_spacing", 0f)
        if (e.num("fit", 0f) != 0f && r.width() > 0f) {
            val minSize = e.num("min_size", text.textSize * 0.5f)
            while (text.measureText(value) > r.width() && text.textSize > minSize) text.textSize *= 0.95f
            if (text.measureText(value) > r.width()) {
                value = android.text.TextUtils.ellipsize(value, text, r.width(), android.text.TextUtils.TruncateAt.END).toString()
            }
        }
        val width = text.measureText(value)
        val x = when (e.optString("align", "left")) {
            "center" -> r.centerX() - width / 2
            "right" -> r.right - width
            else -> r.left
        }
        val fm = text.fontMetrics
        val baseline = when (e.optString("valign", "middle")) {
            "top" -> r.top - fm.ascent
            "bottom" -> r.bottom - fm.descent
            else -> r.centerY() - (fm.ascent + fm.descent) / 2
        }
        color(e, "shadow")?.let {
            text.style = Paint.Style.FILL
            text.color = it
            c.drawText(value, x + e.num("shadow_dx", 2f), baseline + e.num("shadow_dy", 2f), text)
        }
        color(e, "outline")?.let {
            text.style = Paint.Style.STROKE
            text.strokeJoin = Paint.Join.ROUND
            text.strokeWidth = e.num("outline_width", 3f)
            text.color = it
            c.drawText(value, x, baseline, text)
        }
        text.style = Paint.Style.FILL
        val top = color(e, "color") ?: Color.WHITE
        val bottom = color(e, "fill2")
        if (bottom != null) {
            text.shader = LinearGradient(0f, baseline + fm.ascent, 0f, baseline, top, bottom, Shader.TileMode.CLAMP)
        } else {
            text.color = top
        }
        c.drawText(value, x, baseline, text)
        text.shader = null
    }

    private fun drawBar(c: Canvas, e: JSONObject) {
        val r = box(e)
        val radius = e.num("radius", r.height() / 2)
        color(e, "track")?.let { trackTop ->
            val trackBottom = color(e, "track2")
            fill.shader = if (trackBottom != null) {
                LinearGradient(0f, r.top, 0f, r.bottom, trackTop, trackBottom, Shader.TileMode.CLAMP)
            } else null
            fill.color = trackTop
            c.drawRoundRect(r, radius, radius, fill)
            fill.shader = null
        }
        val fraction = e.optDouble("fraction", Double.NaN)
        if (!fraction.isNaN() && fraction > 0) {
            val filled = RectF(r.left, r.top, r.left + r.width() * fraction.toFloat().coerceIn(0f, 1f), r.bottom)
            c.save()
            c.clipRect(filled)
            applyFill(e, r, horizontal = true)?.let { c.drawRoundRect(r, radius, radius, it) }
            c.restore()
        }
        // Segment ticks, like the stat gauges on a Tellius status screen.
        val segments = e.num("count", 0f).toInt()
        if (segments in 2..200) {
            stroke.color = color(e, "off") ?: 0x66000000
            stroke.strokeWidth = e.num("gap", 2f)
            for (i in 1 until segments) {
                val x = r.left + r.width() * i / segments
                c.drawLine(x, r.top, x, r.bottom, stroke)
            }
        }
        color(e, "stroke")?.let {
            stroke.color = it
            stroke.strokeWidth = e.num("stroke_width", 1f)
            c.drawRoundRect(inset(r, stroke.strokeWidth / 2), radius, radius, stroke)
        }
    }

    private fun drawPips(c: Canvas, e: JSONObject) {
        val r = box(e)
        val count = e.num("count", 0f).toInt().coerceIn(0, 100)
        if (count == 0) return
        val filled = e.num("filled", 0f)
        val gap = e.num("gap", r.height() * 0.25f)
        val size = min(r.height(), (r.width() - gap * (count - 1)) / count)
        val on = color(e, "fill") ?: Color.WHITE
        val off = color(e, "off") ?: 0x40FFFFFF
        for (i in 0 until count) {
            val cx = r.left + size / 2 + i * (size + gap)
            val cy = r.centerY()
            fill.shader = null
            fill.color = if (i < filled) on else off
            when (e.optString("shape", "diamond")) {
                "circle" -> c.drawCircle(cx, cy, size / 2, fill)
                "square" -> c.drawRect(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2, fill)
                else -> diamond(c, cx, cy, size / 2, fill)
            }
        }
    }

    private fun drawImage(c: Canvas, e: JSONObject) {
        val bitmap = loadBitmap(e.optString("src")) ?: return
        val tint = color(e, "tint")
        bitmapPaint.colorFilter = tint?.let { PorterDuffColorFilter(it, PorterDuff.Mode.SRC_IN) }
        val r = box(e)
        val flip = e.num("flip_x", 0f) != 0f
        if (flip) {
            // Mirror around the box's centre (end caps drawn from one texture).
            c.save()
            c.scale(-1f, 1f, r.centerX(), r.centerY())
        }
        val crop = e.optJSONArray("crop")
        val sx = crop?.optDouble(0)?.toFloat() ?: 0f
        val sy = crop?.optDouble(1)?.toFloat() ?: 0f
        val sw = crop?.optDouble(2)?.toFloat() ?: bitmap.width.toFloat()
        val sh = crop?.optDouble(3)?.toFloat() ?: bitmap.height.toFloat()
        val slice = e.optJSONArray("slice")
        if (slice == null) {
            drawPart(c, bitmap, sx, sy, sx + sw, sy + sh, r.left, r.top, r.right, r.bottom)
        } else {
            // Nine-slice: corners keep their size (times slice_scale), edges stretch along one
            // axis, the centre along both, so a window frame fits any panel.
            val scale = e.num("slice_scale", 1f)
            val l = slice.optDouble(0).toFloat()
            val t = slice.optDouble(1).toFloat()
            val rr = slice.optDouble(2).toFloat()
            val b = slice.optDouble(3).toFloat()
            val xs = floatArrayOf(sx, sx + l, sx + sw - rr, sx + sw)
            val ys = floatArrayOf(sy, sy + t, sy + sh - b, sy + sh)
            val xd = floatArrayOf(
                r.left, r.left + min(l * scale, r.width() / 2), r.right - min(rr * scale, r.width() / 2), r.right
            )
            val yd = floatArrayOf(
                r.top, r.top + min(t * scale, r.height() / 2), r.bottom - min(b * scale, r.height() / 2), r.bottom
            )
            for (j in 0 until 3) for (i in 0 until 3) {
                drawPart(c, bitmap, xs[i], ys[j], xs[i + 1], ys[j + 1], xd[i], yd[j], xd[i + 1], yd[j + 1])
            }
        }
        if (flip) c.restore()
        bitmapPaint.colorFilter = null
    }

    private val srcRect = android.graphics.Rect()
    private val dstRect = RectF()

    private fun drawPart(
        c: Canvas, bitmap: Bitmap, sl: Float, st: Float, sr: Float, sb: Float,
        dl: Float, dt: Float, dr: Float, db: Float
    ) {
        if (sr <= sl || sb <= st || dr <= dl || db <= dt) return
        srcRect.set(sl.toInt(), st.toInt(), sr.toInt(), sb.toInt())
        dstRect.set(dl, dt, dr, db)
        c.drawBitmap(bitmap, srcRect, dstRect, bitmapPaint)
    }

    private fun drawPath(c: Canvas, e: JSONObject) {
        val data = e.optString("d")
        val path = paths.getOrPut(data) {
            if (paths.size > 256) paths.remove(paths.keys.first())
            try {
                PathParser.createPathFromPathData(data)
            } catch (ex: RuntimeException) {
                null
            }
        } ?: return
        val viewbox = e.optJSONArray("viewbox") ?: return
        val vx = viewbox.optDouble(0).toFloat()
        val vy = viewbox.optDouble(1).toFloat()
        val vw = viewbox.optDouble(2).toFloat()
        val vh = viewbox.optDouble(3).toFloat()
        if (vw <= 0f || vh <= 0f) return
        val r = box(e)
        val s = min(r.width() / vw, r.height() / vh)
        val matrix = Matrix().apply {
            postTranslate(-vx, -vy)
            postScale(s, s)
            postTranslate(r.left + (r.width() - vw * s) / 2, r.top + (r.height() - vh * s) / 2)
        }
        val placed = Path(path).apply { transform(matrix) }
        color(e, "fill")?.let {
            fill.shader = null
            fill.color = it
            c.drawPath(placed, fill)
        }
        color(e, "stroke")?.let {
            stroke.color = it
            stroke.strokeWidth = e.num("stroke_width", 1f)
            stroke.strokeJoin = Paint.Join.ROUND
            stroke.strokeCap = Paint.Cap.ROUND
            c.drawPath(placed, stroke)
        }
    }

    private fun drawGrid(c: Canvas, e: JSONObject, scale: Float, dx: Float, dy: Float, hits: MutableList<Hit>) {
        val r = box(e)
        val cols = e.optInt("cols", 0)
        val rows = e.optInt("rows", 0)
        if (cols <= 0 || rows <= 0) return
        // Square cells, centred in the box.
        val cell = min(r.width() / cols, r.height() / rows)
        val left = r.left + (r.width() - cell * cols) / 2
        val top = r.top + (r.height() - cell * rows) / 2

        val palette = e.optJSONArray("palette")
        val cells = e.optJSONArray("cells")
        if (palette != null && cells != null) {
            val colors = IntArray(palette.length()) { DuoSnapshot.parseColor(palette.optString(it)) ?: Color.TRANSPARENT }
            fill.shader = null
            for (i in 0 until min(cells.length(), cols * rows)) {
                val index = cells.optInt(i, -1)
                if (index !in colors.indices) continue
                fill.color = colors[index]
                val x = left + (i % cols) * cell
                val y = top + (i / cols) * cell
                c.drawRect(x, y, x + cell + 0.5f, y + cell + 0.5f, fill)
            }
        }
        color(e, "grid_color")?.let {
            stroke.color = it
            stroke.strokeWidth = e.num("grid_width", 1f)
            for (i in 0..cols) c.drawLine(left + i * cell, top, left + i * cell, top + rows * cell, stroke)
            for (i in 0..rows) c.drawLine(left, top + i * cell, left + cols * cell, top + i * cell, stroke)
        }

        val marks = e.optJSONArray("marks") ?: return
        for (i in 0 until marks.length()) {
            val m = marks.optJSONObject(i) ?: continue
            val cx = left + (m.optDouble("x").toFloat() + 0.5f) * cell
            val cy = top + (m.optDouble("y").toFloat() + 0.5f) * cell
            val half = cell * m.optDouble("size", 0.8).toFloat() / 2
            val markFill = color(m, "fill")
            val markStroke = color(m, "stroke")
            fill.shader = null
            stroke.strokeWidth = max(1.5f, cell * 0.08f)
            when (m.optString("shape", "dot")) {
                "square" -> {
                    markFill?.let { fill.color = it; c.drawRect(cx - half, cy - half, cx + half, cy + half, fill) }
                    markStroke?.let { stroke.color = it; c.drawRect(cx - half, cy - half, cx + half, cy + half, stroke) }
                }
                "diamond" -> {
                    markFill?.let { fill.color = it; diamond(c, cx, cy, half, fill) }
                    markStroke?.let { stroke.color = it; diamond(c, cx, cy, half, stroke) }
                }
                "ring" -> {
                    stroke.color = markStroke ?: markFill ?: Color.WHITE
                    c.drawCircle(cx, cy, half, stroke)
                }
                "cursor" -> {
                    // Four corner brackets around the cell, like the in-game map cursor.
                    stroke.color = markStroke ?: markFill ?: Color.WHITE
                    stroke.strokeWidth = max(2f, cell * 0.12f)
                    val arm = half * 0.55f
                    for ((x, y) in listOf(cx - half to cy - half, cx + half to cy - half, cx - half to cy + half, cx + half to cy + half)) {
                        val sx = if (x < cx) 1 else -1
                        val sy = if (y < cy) 1 else -1
                        c.drawLine(x, y, x + sx * arm, y, stroke)
                        c.drawLine(x, y, x, y + sy * arm, stroke)
                    }
                }
                else -> {
                    markFill?.let { fill.color = it; c.drawCircle(cx, cy, half, fill) }
                    markStroke?.let { stroke.color = it; c.drawCircle(cx, cy, half, stroke) }
                }
            }
            m.optStringOrNull("label")?.takeIf { it.isNotEmpty() }?.let { label ->
                text.reset()
                text.isAntiAlias = true
                text.typeface = Typeface.DEFAULT_BOLD
                text.textSize = cell * 0.5f
                text.color = Color.WHITE
                text.setShadowLayer(cell * 0.08f, 0f, 0f, Color.BLACK)
                c.drawText(label, cx - text.measureText(label) / 2, cy - (text.ascent() + text.descent()) / 2, text)
                text.clearShadowLayer()
            }
            m.optStringOrNull("action")?.let { action ->
                val touch = max(half, cell / 2)
                hits.add(
                    Hit(
                        RectF(dx + (cx - touch) * scale, dy + (cy - touch) * scale, dx + (cx + touch) * scale, dy + (cy + touch) * scale),
                        action, m.optString("source", ""), m.optInt("index", -1)
                    )
                )
            }
        }
    }

    // -------------------------------------------------------------------------------------------
    // Helpers

    private fun box(e: JSONObject): RectF {
        val x = e.num("x", 0f)
        val y = e.num("y", 0f)
        return RectF(x, y, x + e.num("w", 0f), y + e.num("h", 0f))
    }

    private fun inset(r: RectF, by: Float) = RectF(r.left + by, r.top + by, r.right - by, r.bottom - by)

    private fun corners(r: RectF) = listOf(r.left to r.top, r.right to r.top, r.left to r.bottom, r.right to r.bottom)

    /** Fill paint for `fill` (and `fill2` as a gradient), or null without a fill. */
    private fun applyFill(e: JSONObject, r: RectF, horizontal: Boolean = false): Paint? {
        val top = color(e, "fill") ?: return null
        val bottom = color(e, "fill2")
        fill.shader = if (bottom != null) {
            if (horizontal) LinearGradient(r.left, 0f, r.right, 0f, top, bottom, Shader.TileMode.CLAMP)
            else LinearGradient(0f, r.top, 0f, r.bottom, top, bottom, Shader.TileMode.CLAMP)
        } else null
        fill.color = top
        return fill
    }

    private fun framePath(r: RectF, cut: Float, radius: Float): Path = Path().apply {
        if (cut > 0f) {
            val k = min(cut, min(r.width(), r.height()) / 2)
            moveTo(r.left + k, r.top)
            lineTo(r.right - k, r.top)
            lineTo(r.right, r.top + k)
            lineTo(r.right, r.bottom - k)
            lineTo(r.right - k, r.bottom)
            lineTo(r.left + k, r.bottom)
            lineTo(r.left, r.bottom - k)
            lineTo(r.left, r.top + k)
            close()
        } else {
            addRoundRect(r, radius, radius, Path.Direction.CW)
        }
    }

    private fun diamond(c: Canvas, cx: Float, cy: Float, r: Float, paint: Paint) {
        val path = Path().apply {
            moveTo(cx, cy - r); lineTo(cx + r, cy); lineTo(cx, cy + r); lineTo(cx - r, cy); close()
        }
        c.drawPath(path, paint)
    }

    private fun typeface(name: String, bold: Boolean, info: DuoSnapshot.CanvasInfo): Typeface {
        val key = "$name|$bold"
        return typefaces.getOrPut(key) {
            val file = info.fonts[name]?.let { resolveInPackage(it) }
            val base = when {
                file != null -> try {
                    Typeface.createFromFile(file)
                } catch (ex: RuntimeException) {
                    null
                }
                name == "sans" -> Typeface.SANS_SERIF
                name == "mono" -> Typeface.MONOSPACE
                else -> Typeface.SERIF
            } ?: Typeface.SERIF
            if (bold) Typeface.create(base, Typeface.BOLD) else base
        }
    }

    private fun loadBitmap(relativePath: String): Bitmap? {
        if (bitmaps.containsKey(relativePath)) return bitmaps[relativePath]
        // "disc:" images come from the running game's disc, decoded once into Duo's cache. A miss
        // (no game yet, unreadable disc) is not remembered, so it is retried on a later frame.
        val isDisc = relativePath.startsWith("disc:")
        val now = android.os.SystemClock.uptimeMillis()
        if (isDisc && now - (discFailures[relativePath] ?: Long.MIN_VALUE / 2) < 2000) return null
        val file = if (isDisc) {
            DuoNative.resolveDiscImage(relativePath)?.let { File(it) }?.takeIf { it.isFile }
        } else {
            resolveInPackage(relativePath)
        }
        val bitmap = file?.let { decodeBitmap(it) }
        if (bitmap != null || !isDisc) bitmaps[relativePath] = bitmap
        if (bitmap == null && isDisc) discFailures[relativePath] = now
        return bitmap
    }

    private fun decodeBitmap(file: File): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.path, bounds)
        var sample = 1
        while (bounds.outWidth / sample > 2048 || bounds.outHeight / sample > 2048) sample *= 2
        return BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample })
    }

    /** A file inside the companion's own folder, never outside it. */
    private fun resolveInPackage(relativePath: String): File? {
        val root = File(directory ?: return null).canonicalFile
        val file = File(root, relativePath).canonicalFile
        if (!file.path.startsWith(root.path + File.separator) || !file.isFile) return null
        return file
    }

    private fun color(e: JSONObject, key: String): Int? = DuoSnapshot.parseColor(e.optStringOrNull(key))

    private fun JSONObject.num(key: String, fallback: Float): Float {
        if (!has(key) || isNull(key)) return fallback
        val value = optDouble(key)
        return if (value.isNaN()) fallback else value.toFloat()
    }

    private fun JSONObject.optStringOrNull(key: String): String? =
        if (!has(key) || isNull(key)) null else optString(key).takeIf { it.isNotEmpty() }
}
