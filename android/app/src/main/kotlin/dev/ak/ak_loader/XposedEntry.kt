package dev.ak.ak_loader

import android.content.Context
import de.robv.android.xposed.IXposedHookLoadPackage
import de.robv.android.xposed.XSharedPreferences
import de.robv.android.xposed.XposedBridge
import de.robv.android.xposed.callbacks.XC_LoadPackage
import java.io.File
import java.util.zip.ZipFile

/**
 * 只在明日方舟进程里工作：
 *   1. 从本模块的 SharedPreferences 读配置（LSPosed 的 xposedsharedprefs 机制）
 *   2. 按配置延迟
 *   3. 把本模块 APK 里的 libakl_core.so 释放到游戏自己的 code_cache 目录并加载
 *   4. 交给 native 引擎处理 il2cpp
 */
class XposedEntry : IXposedHookLoadPackage {

    companion object {
        private const val MODULE_PKG = "dev.ak.ak_loader"
        private val GAMES = setOf(
            "com.hypergryph.arknights",
            "com.hypergryph.arknights.bilibili",
            "tw.arknights",
        )
        fun log(msg: String) = XposedBridge.log("[AKL] $msg")
    }

    override fun handleLoadPackage(lpparam: XC_LoadPackage.LoadPackageParam) {
        if (lpparam.packageName !in GAMES) return
        if (!lpparam.isFirstApplication) return
        log("进入 ${lpparam.packageName}")

        val prefs = try {
            XSharedPreferences(MODULE_PKG, "akl").also { it.reload() }
        } catch (t: Throwable) {
            log("读配置失败: $t")
            return
        }
        if (!prefs.getBoolean("enabled", true)) {
            log("模块已关闭")
            return
        }
        val delayMs = prefs.getInt("delay_ms", 5000).coerceIn(0, 60_000)
        val speedX100 = prefs.getInt("speed_x100", 200).coerceIn(100, 1000)

        Thread({
            try {
                if (delayMs > 0) Thread.sleep(delayMs.toLong())
                val ctx = currentApplication()
                val lib = extractNative(ctx, moduleApk())
                System.load(lib.absolutePath)
                NativeBridge.start(speedX100)
                log("引擎已注入 speed=x${speedX100 / 100.0}")
            } catch (t: Throwable) {
                log("注入失败: $t")
            }
        }, "akl-loader").start()
    }

    /**
     * 定位本模块 APK。api-82 没有 LoadPackageParam.modulePath，所以从类加载器
     * 的 CodeSource 拿（LSPosed 把模块 APK 注入目标进程时就能拿到），
     * 并用"能当 zip 打开且确实含目标 so"来校验候选。
     */
    private fun moduleApk(): String {
        val candidates = listOfNotNull(
            XposedEntry::class.java.protectionDomain?.codeSource?.location,
            XposedBridge::class.java.protectionDomain?.codeSource?.location,
        ).mapNotNull { runCatching { java.io.File(it.toURI()).path }.getOrNull() }

        for (path in candidates) {
            val hasLib = runCatching {
                ZipFile(path).use { it.getEntry("lib/arm64-v8a/libakl_core.so") != null }
            }.getOrDefault(false)
            if (hasLib) return path
        }
        throw IllegalStateException("定位不到模块 APK，候选=$candidates")
    }

    /** 从模块 APK 里取出 arm64 的 so，落到目标进程可写的 code_cache */
    private fun extractNative(ctx: Context, modulePath: String?): File {
        val dir = File(ctx.codeCacheDir, "akl").apply { mkdirs() }
        val out = File(dir, "libakl_core.so")
        val abi = if (android.os.Process.is64Bit()) "arm64-v8a" else "armeabi-v7a"
        val entry = "lib/$abi/libakl_core.so"
        ZipFile(modulePath).use { zip ->
            val e = zip.getEntry(entry) ?: throw IllegalStateException("APK 里没有 $entry")
            zip.getInputStream(e).use { input ->
                out.outputStream().use { input.copyTo(it) }
            }
        }
        out.setReadable(true, false)
        out.setExecutable(true, false)
        return out
    }

    private fun currentApplication(): Context {
        val activityThread = Class.forName("android.app.ActivityThread")
        val method = activityThread.getDeclaredMethod("currentApplication")
        method.isAccessible = true
        return method.invoke(null) as Context
    }
}
