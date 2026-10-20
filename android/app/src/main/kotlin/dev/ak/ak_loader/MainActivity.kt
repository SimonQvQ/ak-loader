package dev.ak.ak_loader

import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {

    private val channelName = "akl/bridge"
    private val games = listOf(
        Triple("com.hypergryph.arknights", "明日方舟 · 官服", "官服"),
        Triple("com.hypergryph.arknights.bilibili", "明日方舟 · B服", "B服"),
        Triple("tw.arknights", "明日方舟 · 台服", "台服"),
    )

    private val prefs: SharedPreferences by lazy { getSharedPreferences("akl", MODE_PRIVATE) }

    override fun configureFlutterEngine(engine: FlutterEngine) {
        super.configureFlutterEngine(engine)
        MethodChannel(engine.dartExecutor.binaryMessenger, channelName).setMethodCallHandler { call, result ->
            when (call.method) {
                "games" -> result.success(listGames())
                "config" -> result.success(
                    mapOf(
                        "enabled" to prefs.getBoolean("enabled", true),
                        "delayMs" to prefs.getInt("delay_ms", 5000),
                        "speedX100" to prefs.getInt("speed_x100", 200),
                        "cost" to prefs.getBoolean("f_cost", false),
                        "life" to prefs.getBoolean("f_life", false),
                        "freeze" to prefs.getBoolean("f_freeze", false),
                        "win" to prefs.getBoolean("f_win", false),
                        "atkX10" to prefs.getInt("atk_x10", 10),
                    )
                )
                "saveConfig" -> {
                    val enabled = call.argument<Boolean>("enabled") ?: true
                    val delay = call.argument<Int>("delayMs") ?: 5000
                    val speed = call.argument<Int>("speedX100") ?: 200
                    prefs.edit()
                        .putBoolean("enabled", enabled)
                        .putInt("delay_ms", delay.coerceIn(0, 60_000))
                        .putInt("speed_x100", speed.coerceIn(100, 1000))
                        .putBoolean("f_cost", call.argument<Boolean>("cost") ?: false)
                        .putBoolean("f_life", call.argument<Boolean>("life") ?: false)
                        .putBoolean("f_freeze", call.argument<Boolean>("freeze") ?: false)
                        .putBoolean("f_win", call.argument<Boolean>("win") ?: false)
                        .putInt("atk_x10", (call.argument<Int>("atkX10") ?: 10).coerceIn(10, 200))
                        .apply()
                    result.success(true)
                }
                "launch" -> {
                    val pkg = call.argument<String>("pkg")
                    result.success(pkg != null && launchGame(pkg))
                }
                else -> result.notImplemented()
            }
        }
    }

    private fun listGames(): List<Map<String, Any?>> = games.map { (pkg, label, short) ->
        val installed = isInstalled(pkg)
        mapOf(
            "pkg" to pkg,
            "label" to label,
            "short" to short,
            "installed" to installed,
            "version" to versionOf(pkg),
        )
    }

    private fun isInstalled(pkg: String): Boolean = try {
        packageManager.getPackageInfo(pkg, 0)
        true
    } catch (e: PackageManager.NameNotFoundException) {
        false
    }

    private fun versionOf(pkg: String): String? = try {
        packageManager.getPackageInfo(pkg, 0).versionName
    } catch (e: PackageManager.NameNotFoundException) {
        null
    }

    private fun launchGame(pkg: String): Boolean {
        val intent = packageManager.getLaunchIntentForPackage(pkg) ?: return false
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        startActivity(intent)
        return true
    }
}
