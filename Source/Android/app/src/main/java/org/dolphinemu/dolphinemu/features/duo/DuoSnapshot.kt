// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

import org.json.JSONArray
import org.json.JSONObject

/** What the companion screen should show, as published by the native runtime. */
data class DuoSnapshot(
    val sequence: Long,
    val status: String,
    val message: String,
    val gameId: String,
    val gameTitle: String,
    val revision: Int,
    val companionId: String?,
    val companionName: String?,
    val companionVersion: String?,
    val companionDirectory: String?,
    val available: List<Available>,
    val errors: List<String>,
    val notices: List<String>,
    val pages: List<Page>,
    val page: String,
    val widgets: List<Widget>,
    /** Set for canvas pages: the layout's design canvas and its positioned elements. */
    val canvas: CanvasInfo? = null,
    val elements: JSONArray? = null
) {
    data class CanvasInfo(
        val layout: String,
        val width: Float,
        val height: Float,
        val background: Int?,
        val fonts: Map<String, String>
    )

    data class Available(val id: String, val name: String, val version: String)
    data class Page(val id: String, val title: String)

    sealed class Widget {
        data class Text(val style: String, val text: String, val color: Int?) : Widget()
        data class Bar(
            val label: String?,
            val text: String?,
            val fraction: Float?,
            val color: Int?
        ) : Widget()

        data class Stats(val columns: Int, val items: List<Pair<String, String>>) : Widget()
        data class ListWidget(
            val source: String,
            val action: String?,
            val empty: String?,
            val rows: List<Row>
        ) : Widget()

        data class Buttons(val buttons: List<Button>) : Widget()
        data class MapWidget(val aspect: Float, val image: String?, val points: List<Point>) : Widget()
        data object Divider : Widget()
    }

    data class Row(
        val index: Int,
        val text: String,
        val detail: String?,
        val badge: String?,
        val color: Int?,
        val fraction: Float?
    )

    data class Button(val label: String, val action: String, val enabled: Boolean)

    data class Point(
        val x: Float,
        val y: Float,
        val label: String?,
        val color: Int?,
        val shape: String,
        val action: String?,
        val source: String?,
        val index: Int
    )

    companion object {
        /** Parses the "<sequence>\n<json>" form returned by [DuoNative.getSnapshot]. */
        fun parse(raw: String): DuoSnapshot? {
            val newline = raw.indexOf('\n')
            if (newline < 0) return null
            val sequence = raw.substring(0, newline).toLongOrNull() ?: return null
            val body = raw.substring(newline + 1)
            if (body.isBlank()) return empty(sequence)
            return try {
                fromJson(sequence, JSONObject(body))
            } catch (e: org.json.JSONException) {
                null
            }
        }

        fun empty(sequence: Long = 0) = DuoSnapshot(
            sequence, "idle", "", "", "", 0, null, null, null, null,
            emptyList(), emptyList(), emptyList(), emptyList(), "", emptyList()
        )

        private fun fromJson(sequence: Long, json: JSONObject): DuoSnapshot {
            val game = json.optJSONObject("game")
            val companion = json.optJSONObject("companion")
            return DuoSnapshot(
                sequence = sequence,
                status = json.optString("status", "idle"),
                message = json.optString("message", ""),
                gameId = game?.optString("id", "") ?: "",
                gameTitle = game?.optString("title", "") ?: "",
                revision = game?.optInt("revision", 0) ?: 0,
                companionId = companion?.optStringOrNull("id"),
                companionName = companion?.optStringOrNull("name"),
                companionVersion = companion?.optStringOrNull("version"),
                companionDirectory = companion?.optStringOrNull("directory"),
                available = json.optJSONArray("available").objects().map {
                    Available(it.optString("id"), it.optString("name"), it.optString("version"))
                },
                errors = json.optJSONArray("errors").strings(),
                notices = json.optJSONArray("notices").strings(),
                pages = json.optJSONArray("pages").objects().map {
                    Page(it.optString("id"), it.optString("title"))
                },
                page = json.optString("page", ""),
                widgets = json.optJSONArray("widgets").objects().mapNotNull { parseWidget(it) },
                canvas = json.optJSONObject("canvas")?.let { canvas ->
                    val fonts = canvas.optJSONObject("fonts")
                    CanvasInfo(
                        canvas.optString("layout"),
                        canvas.optDouble("w", 1.0).toFloat().coerceAtLeast(1f),
                        canvas.optDouble("h", 1.0).toFloat().coerceAtLeast(1f),
                        parseColor(canvas.optStringOrNull("background")),
                        fonts?.keys()?.asSequence()?.associateWith { fonts.optString(it) } ?: emptyMap()
                    )
                },
                elements = json.optJSONArray("elements")
            )
        }

        private fun parseWidget(json: JSONObject): Widget? = when (json.optString("type")) {
            "text" -> Widget.Text(
                json.optString("style", "body"),
                json.optString("text", ""),
                parseColor(json.optStringOrNull("color"))
            )

            "bar" -> Widget.Bar(
                json.optStringOrNull("label"),
                json.optStringOrNull("text"),
                json.optFloatOrNull("fraction"),
                parseColor(json.optStringOrNull("color"))
            )

            "stats" -> Widget.Stats(
                json.optInt("columns", 2).coerceIn(1, 6),
                json.optJSONArray("items").objects().map {
                    it.optString("label") to it.optString("value")
                }
            )

            "list" -> Widget.ListWidget(
                json.optString("source"),
                json.optStringOrNull("action"),
                json.optStringOrNull("empty"),
                json.optJSONArray("rows").objects().map {
                    Row(
                        it.optInt("index"),
                        it.optString("text", ""),
                        it.optStringOrNull("detail"),
                        it.optStringOrNull("badge"),
                        parseColor(it.optStringOrNull("color")),
                        it.optFloatOrNull("fraction")
                    )
                }
            )

            "buttons" -> Widget.Buttons(
                json.optJSONArray("buttons").objects().map {
                    Button(
                        it.optString("label"),
                        it.optString("action"),
                        it.optBoolean("enabled", true)
                    )
                }
            )

            "map" -> Widget.MapWidget(
                json.optDouble("aspect", 1.0).toFloat().coerceIn(0.05f, 20f),
                json.optStringOrNull("image"),
                json.optJSONArray("points").objects().map {
                    Point(
                        it.optDouble("x", 0.0).toFloat(),
                        it.optDouble("y", 0.0).toFloat(),
                        it.optStringOrNull("label"),
                        parseColor(it.optStringOrNull("color")),
                        it.optString("shape", "dot"),
                        it.optStringOrNull("action"),
                        it.optStringOrNull("source"),
                        it.optInt("index", -1)
                    )
                }
            )

            "divider" -> Widget.Divider
            else -> null
        }

        /** Accepts "#RGB", "#RRGGBB", "#AARRGGBB" and Android color names. Bad input is null. */
        fun parseColor(text: String?): Int? {
            if (text.isNullOrBlank()) return null
            var value = text.trim()
            if (value.length == 4 && value.startsWith("#")) {
                value = "#" + value.substring(1).map { "$it$it" }.joinToString("")
            }
            return try {
                android.graphics.Color.parseColor(value)
            } catch (e: IllegalArgumentException) {
                null
            }
        }

        private fun JSONObject.optStringOrNull(key: String): String? =
            if (isNull(key) || !has(key)) null else optString(key)

        private fun JSONObject.optFloatOrNull(key: String): Float? =
            if (isNull(key) || !has(key)) null else optDouble(key).toFloat().takeIf { !it.isNaN() }

        private fun JSONArray?.objects(): List<JSONObject> {
            if (this == null) return emptyList()
            return (0 until length()).mapNotNull { optJSONObject(it) }
        }

        private fun JSONArray?.strings(): List<String> {
            if (this == null) return emptyList()
            return (0 until length()).map { optString(it) }
        }
    }
}
