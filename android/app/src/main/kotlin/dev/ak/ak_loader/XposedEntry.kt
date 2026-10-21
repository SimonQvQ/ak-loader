package dev.ak.ak_loader

import android.content.Context
import android.os.Build
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
        private const val LIB_ARM64 = "lib/arm64-v8a/libakl_core.so"
        private const val LIB_X64 = "lib/x86_64/libakl_core.so"
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

        // 模块从没打开过时，配置文件不存在，XSharedPreferences 内部会 NPE：
        // 每次读取都兜住异常并退回默认值，不能因为读不到配置就不注入。
        fun flag(key: String, def: Boolean): Boolean = try {
            prefsOrNull()?.getBoolean(key, def) ?: def
        } catch (t: Throwable) { def }
        fun num(key: String, def: Int, lo: Int, hi: Int): Int = try {
            (prefsOrNull()?.getInt(key, def) ?: def).coerceIn(lo, hi)
        } catch (t: Throwable) { def }

        if (!flag("enabled", true)) {
            log("模块已关闭")
            return
        }
        val delayMs = num("delay_ms", 5000, 0, 60_000)
        val speedX100 = num("speed_x100", 200, 100, 1000)

        // 功能掩码：bit0 倍速 / bit1 无限费用 / bit2 生命不减 / bit3 敌人停滞 / bit4 强制胜利 / bit5 攻击倍率
        var mask = 0
        if (speedX100 > 100) mask = mask or 1
        if (flag("f_cost", false)) mask = mask or (1 shl 1)
        if (flag("f_life", false)) mask = mask or (1 shl 2)
        if (flag("f_freeze", false)) mask = mask or (1 shl 3)
        if (flag("f_win", false)) mask = mask or (1 shl 4)
        if (flag("f_instant", false)) mask = mask or (1 shl 6)
        val atkX10 = num("atk_x10", 10, 10, 200)
        if (atkX10 > 10) mask = mask or (1 shl 5)

        Thread({
            try {
                if (delayMs > 0) Thread.sleep(delayMs.toLong())
                val ctx = currentApplication()
                val apk = moduleApk(ctx)
                // 优先用"模块自己的 nativeLibraryDir"里的 so：
                // 只有在那种位置，bionic 才会把加载请求交给 houdini 原生桥，
                // arm64 的库才可能被翻译执行（这是 Waydroid 这类 x86_64 容器里唯一的路）。
                val nativeDirLib = moduleNativeLibDir(apk)
                if (nativeDirLib != null) {
                    try {
                        System.load(nativeDirLib.absolutePath)
                        log("arm64 引擎从 nativeLibraryDir 加载成功：${nativeDirLib.absolutePath}")
                        NativeBridge.start(mask, speedX100, atkX10)
                        log("引擎已注入 mask=0x${Integer.toHexString(mask)} speed=x${speedX100 / 100.0} atk=x${atkX10 / 10.0}")
                        return@Thread
                    } catch (e: UnsatisfiedLinkError) {
                        log("nativeLibraryDir 里的 so 加载失败（${e.message}），改用 code_cache")
                    }
                }
                val lib = extractNative(ctx, apk, false)
                try {
                    System.load(lib.absolutePath)
                } catch (e: UnsatisfiedLinkError) {
                    // x86 容器 + houdini 跑 arm 游戏时，进程是 EM_X86_64，
                    // 内核链接器拒绝加载 arm64 so。回退 x86_64 版让引擎能起来，
                    // 但 houdini 翻译层下无法 inline hook arm64 原生代码。
                    log("arm64 so 加载失败（${e.message}），回退 x86_64")
                    val alt = extractNative(ctx, apk, true)
                    System.load(alt.absolutePath)
                    log("警告：当前为 houdini/翻译层环境，原生 hook 不可用，请用真机 arm64 测试")
                }
                NativeBridge.start(mask, speedX100, atkX10)
                log("引擎已注入 mask=0x${Integer.toHexString(mask)} speed=x${speedX100 / 100.0} atk=x${atkX10 / 10.0}")
            } catch (t: Throwable) {
                log("注入失败: $t")
            }
        }, "akl-loader").start()
    }

    /**
     * 定位本模块 APK。多级回退，因为不同 Xposed 实现暴露的方式不同：
     *   1) protectionDomain.codeSource（旧 api 常见）
     *   2) /proc/self/maps：ART 会把 APK 里的 dex 以文件方式 mmap，路径会出现在 maps
     *   3) PackageManager（受包可见性限制，失败属正常）
     *   4) 直接扫 /data/app 下包名目录
     * 每个候选都要能当 zip 打开且确实含目标 so 才算命中。
     */
    private fun moduleApk(ctx: Context?): String {
        val candidates = LinkedHashSet<String>()

        listOfNotNull(
            XposedEntry::class.java.protectionDomain?.codeSource?.location,
            XposedBridge::class.java.protectionDomain?.codeSource?.location,
        ).forEach { uri ->
            runCatching { java.io.File(uri.toURI()).path }.getOrNull()?.let(candidates::add)
        }

        runCatching {
            File("/proc/self/maps").forEachLine { line ->
                val path = line.substringAfterLast(' ', "").trim()
                if (path.endsWith(".apk") && path.contains("ak_loader")) candidates.add(path)
            }
        }

        runCatching {
            val pm = (ctx ?: currentApplication())?.packageManager
            val ai = pm?.getApplicationInfo(MODULE_PKG, 0)
            ai?.sourceDir?.let(candidates::add)
        }

        runCatching {
            File("/data/app").listFiles()?.forEach { top ->
                top.listFiles()?.forEach { d ->
                    if (d.name.startsWith(MODULE_PKG) || d.name.startsWith("~~")) {
                        if (d.name.contains(MODULE_PKG)) {
                            File(d, "base.apk").takeIf { it.isFile }?.let { candidates.add(it.path) }
                            d.listFiles()?.firstOrNull { it.name == "base.apk" }?.let { candidates.add(it.path) }
                        }
                    }
                }
            }
        }

        for (path in candidates) {
            val ok = runCatching {
                ZipFile(path).use { it.getEntry(LIB_ARM64) != null || it.getEntry(LIB_X64) != null }
            }.getOrDefault(false)
            if (ok) {
                log("模块 APK = $path")
                return path
            }
        }
        throw IllegalStateException("定位不到模块 APK，候选=$candidates")
    }

    /** 模块 APK 自己的 nativeLibraryDir 里的 libakl_core.so（存在才返回） */
    private fun moduleNativeLibDir(modulePath: String?): File? {
        if (modulePath == null) return null
        val apk = File(modulePath)
        val dir = apk.parentFile ?: return null
        for (abi in listOf("arm64", "arm64-v8a")) {
            val f = File(File(dir, "lib/$abi"), "libakl_core.so")
            if (f.isFile) return f
        }
        // /data/app/.../base.apk 的兄弟目录
        return null
    }

    /** 从模块 APK 里取出 arm64 的 so，落到目标进程可写的 code_cache */
    private fun extractNative(ctx: Context, modulePath: String?, forceX64: Boolean): File {
        val dir = File(ctx.codeCacheDir, "akl").apply { mkdirs() }
        val name = if (forceX64) "libakl_core_x64.so" else "libakl_core.so"
        val out = File(dir, name)
        val entry = pickLibEntry(modulePath, forceX64)
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

    /**
     * 选哪个 ABI 的 so：
     *  - 原生 arm64 设备 → arm64-v8a
     *  - houdini/ndk-translation（x86 容器跑 arm 游戏）→ arm64-v8a 依然正确（游戏自己的 libil2cpp 也是 arm64）
     *  - 纯 x86 模拟器且 APK 没有 arm64 → 回退 x86_64
     */
    private fun pickLibEntry(modulePath: String?, forceX64: Boolean): String {
        if (forceX64) return LIB_X64
        val preferArm = Build.SUPPORTED_ABIS.any { it.startsWith("arm") } ||
            android.os.Process.is64Bit() && Build.SUPPORTED_64_BIT_ABIS.any { it.startsWith("arm") }
        ZipFile(modulePath).use { zip ->
            fun has(e: String) = zip.getEntry(e) != null
            if (preferArm && has(LIB_ARM64)) return LIB_ARM64
            if (has(LIB_ARM64)) return LIB_ARM64
            if (has(LIB_X64)) return LIB_X64
        }
        throw IllegalStateException("APK 里没有可用的 libakl_core.so")
    }

    private var cachedPrefs: XSharedPreferences? = null

    private fun prefsOrNull(): XSharedPreferences? {
        cachedPrefs?.let { return it }
        return try {
            XSharedPreferences(MODULE_PKG, "akl").also { it.reload(); cachedPrefs = it }
        } catch (t: Throwable) {
            log("读配置失败（按默认值继续）: $t")
            null
        }
    }

    private fun currentApplication(): Context {
        val activityThread = Class.forName("android.app.ActivityThread")
        val method = activityThread.getDeclaredMethod("currentApplication")
        method.isAccessible = true
        return method.invoke(null) as Context
    }
}
