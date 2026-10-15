// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.features.duo

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.text.TextUtils
import android.view.Gravity
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.lifecycle.lifecycleScope
import com.google.android.material.appbar.AppBarLayout
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.dolphinemu.dolphinemu.R
import org.dolphinemu.dolphinemu.utils.ThemeHelper
import org.json.JSONArray

/** Lists installed Dolphin Duo companions and installs or removes them. */
class DuoCompanionsActivity : AppCompatActivity() {
    private lateinit var list: LinearLayout

    private val pickPackage = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? -> uri?.let(::install) }

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeHelper.setTheme(this)
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)

        val toolbar = MaterialToolbar(this)
        val appBar = AppBarLayout(this).apply { addView(toolbar) }
        setSupportActionBar(toolbar)
        title = getString(R.string.duo_companions)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        val padding = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding, padding, padding)
        }

        root.addView(TextView(this).apply {
            setText(R.string.duo_companions_intro)
            setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodyMedium)
        })

        root.addView(MaterialButton(this).apply {
            setText(R.string.duo_install_companion)
            setOnClickListener { pickPackage.launch(arrayOf("application/zip", "application/json", "application/octet-stream")) }
        }, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = padding })

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(list)

        root.addView(TextView(this).apply {
            text = getString(
                R.string.duo_folders, DuoNative.getCompanionsDirectory(), DuoNative.getWatchesDirectory()
            )
            setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodySmall)
            setTextIsSelectable(true)
        }, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = padding })

        val scroll = ScrollView(this).apply { addView(root) }
        val screen = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(appBar)
            addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        }
        ViewCompat.setOnApplyWindowInsetsListener(screen) { _, insets ->
            val bars = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()
            )
            appBar.updatePadding(top = bars.top, left = bars.left, right = bars.right)
            scroll.updatePadding(left = bars.left, right = bars.right, bottom = bars.bottom)
            insets
        }
        setContentView(screen)
        refresh()
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) {
            finish()
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    private fun refresh() {
        lifecycleScope.launch {
            val installed = withContext(Dispatchers.IO) { JSONArray(DuoNative.listInstalled()) }
            list.removeAllViews()
            if (installed.length() == 0) {
                list.addView(TextView(this@DuoCompanionsActivity).apply {
                    setText(R.string.duo_no_companions)
                    gravity = Gravity.CENTER
                    setPadding(0, dp(24), 0, dp(24))
                })
                return@launch
            }
            for (i in 0 until installed.length()) {
                list.addView(companionCard(installed.getJSONObject(i)))
            }
        }
    }

    private fun companionCard(companion: org.json.JSONObject): View {
        val padding = dp(16)
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding, padding, padding)
        }

        val name = companion.optString("name")
        val version = companion.optString("version")
        body.addView(TextView(this).apply {
            text = if (version.isEmpty()) name else "$name  ·  v$version"
            setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_TitleMedium)
        })

        val targets = companion.optJSONArray("targets")
        val games = (0 until (targets?.length() ?: 0)).joinToString(", ") {
            targets!!.getJSONObject(it).optString("game_id")
        }
        val author = companion.optString("author")
        val lines = listOfNotNull(
            if (games.isNotEmpty()) getString(R.string.duo_for_games, games) else null,
            if (author.isNotEmpty()) getString(R.string.duo_by_author, author) else null,
            companion.optString("description").takeIf { it.isNotEmpty() }
        )
        if (lines.isNotEmpty()) {
            body.addView(TextView(this).apply {
                text = lines.joinToString("\n")
                setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodyMedium)
            })
        }

        val errors = companion.optJSONArray("errors")
        if (errors != null && errors.length() > 0) {
            body.addView(TextView(this).apply {
                text = getString(R.string.duo_companion_broken) + "\n" +
                        (0 until minOf(errors.length(), 5)).joinToString("\n") { "• " + errors.optString(it) }
                setTextColor(0xFFE57373.toInt())
                ellipsize = TextUtils.TruncateAt.END
            })
        }

        body.addView(MaterialButton(this, null, com.google.android.material.R.attr.borderlessButtonStyle).apply {
            setText(R.string.duo_uninstall)
            setOnClickListener { confirmUninstall(name, companion.optString("directory")) }
        }, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ).apply { gravity = Gravity.END })

        return MaterialCardView(this).apply {
            addView(body)
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(12) }
        }
    }

    private fun install(uri: Uri) {
        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                try {
                    Result.success(DuoPackageInstaller.prepare(applicationContext, uri))
                } catch (e: DuoPackageInstaller.InstallException) {
                    Result.failure(e)
                }
            }
            result.onFailure { error ->
                MaterialAlertDialogBuilder(this@DuoCompanionsActivity)
                    .setTitle(R.string.duo_install_failed)
                    .setMessage(error.message)
                    .setPositiveButton(R.string.ok, null)
                    .show()
            }
            result.onSuccess { prepared -> confirmInstall(prepared) }
        }
    }

    private fun confirmInstall(prepared: DuoPackageInstaller.Prepared) {
        val details = prepared.details
        val message = buildString {
            append(getString(R.string.duo_for_games, details.games.joinToString(", ")))
            if (details.author.isNotEmpty()) append("\n").append(getString(R.string.duo_by_author, details.author))
            if (details.description.isNotEmpty()) append("\n\n").append(details.description)
            if (details.writesMemory || details.pressesButtons) {
                append("\n\n").append(getString(R.string.duo_permissions_header))
                if (details.writesMemory) append("\n• ").append(getString(R.string.duo_permission_write))
                if (details.pressesButtons) append("\n• ").append(getString(R.string.duo_permission_input))
            }
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(getString(R.string.duo_install_title, details.name, details.version))
            .setMessage(message)
            .setPositiveButton(R.string.duo_install) { _, _ ->
                lifecycleScope.launch {
                    val error = withContext(Dispatchers.IO) {
                        try {
                            DuoPackageInstaller.commit(prepared)
                            null
                        } catch (e: DuoPackageInstaller.InstallException) {
                            e.message
                        }
                    }
                    if (error != null) {
                        MaterialAlertDialogBuilder(this@DuoCompanionsActivity)
                            .setTitle(R.string.duo_install_failed)
                            .setMessage(error)
                            .setPositiveButton(R.string.ok, null)
                            .show()
                    }
                    DuoNative.reload()
                    refresh()
                }
            }
            .setNegativeButton(R.string.cancel) { _, _ -> DuoPackageInstaller.discard(prepared) }
            .setOnCancelListener { DuoPackageInstaller.discard(prepared) }
            .show()
    }

    private fun confirmUninstall(name: String, directory: String) {
        MaterialAlertDialogBuilder(this)
            .setTitle(getString(R.string.duo_uninstall_title, name))
            .setPositiveButton(R.string.duo_uninstall) { _, _ ->
                lifecycleScope.launch {
                    withContext(Dispatchers.IO) { DuoPackageInstaller.uninstall(directory) }
                    DuoNative.reload()
                    refresh()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()

    companion object {
        fun launch(context: Context) {
            context.startActivity(Intent(context, DuoCompanionsActivity::class.java))
        }
    }
}
