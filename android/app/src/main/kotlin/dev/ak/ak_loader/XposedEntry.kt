package dev.ak.ak_loader

import android.content.Context
import android.os.Build
import android.system.Os
import de.robv.android.xposed.IXposedHookLoadPackage
import de.robv.android.xposed.XSharedPreferences
import de.robv.android.xposed.XposedBridge
import de.robv.android.xposed.callbacks.XC_LoadPackage
import java.io.File
import java.util.zip.ZipFile
import dalvik.system.PathClassLoader
import java.lang.reflect.InvocationTargetException

/**
 * 只在明日方舟进程里工作：
 *   1. 从本模块的 SharedPreferences 读配置（LSPosed 的 xposedsharedprefs 机制）
 *   2. 按配置延迟
 *   3. 在继承游戏原生命名空间的 ClassLoader 中加载 arm64 引擎
 *   4. 交给 native 引擎处理 il2cpp
 */
class XposedEntry : IXposedHookLoadPackage {

    companion object {
        private const val MODULE_PKG = "dev.ak.ak_loader"
        private const val LIB_ARM64 = "lib/arm64-v8a/libakl_core.so"
        private const val LIB_X64 = "lib/x86_64/libakl_core.so"
        private const val GUEST_ASSET = "assets/akl/libakl_guest.so"
        private const val BRIDGE_CLASS = "dev.ak.ak_loader.NativeBridge"
        // JNI 返回后仍保留加载器，避免原生 hook 还在使用时库被卸载。
        private var engineClassLoader: ClassLoader? = null
        private val GAMES = setOf(
            "com.hypergryph.arknights",
            "com.hypergryph.arknights.bilibili",
            "tw.arknights",
        )
        fun log(msg: String) = XposedBridge.log("[AKL] $msg")
    }

    override fun handleLoadPackage(lpparam: XC_LoadPackage.LoadPackageParam) {
        if (lpparam.packageName !in GAMES) return
        if (lpparam.processName != lpparam.packageName) return
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
        log("配置 delay=${delayMs}ms mask=0x${mask.toString(16)} speed=$speedX100 attack=$atkX10")

        Thread({
            try {
                if (delayMs > 0) Thread.sleep(delayMs.toLong())
                val ctx = currentApplication()
                val apk = moduleApk(ctx)
                val machine = Os.uname().machine
                if (android.os.Process.is64Bit() && machine in setOf("x86_64", "amd64")) {
                    startOnX64(ctx, apk, mask, speedX100, atkX10)
                    return@Thread
                }
                val lib = moduleNativeLibDir(apk) ?: extractNative(ctx, apk, false)
                startInGameNamespace(ctx, apk, lib, mask, speedX100, atkX10)
                log("arm64 引擎调用完成；是否找到 il2cpp / 安装 hook 请看 AKL 原生日志")
            } catch (t: Throwable) {
                log("注入失败: $t")
            }
        }, "akl-loader").start()
    }

    private fun startOnX64(ctx: Context, apk: String, mask: Int, speed: Int, attack: Int) {
        ZipFile(apk).use { zip ->
            check(zip.getEntry(LIB_X64) != null && zip.getEntry(GUEST_ASSET) != null) {
                "Waydroid 需要新版 zoot-waydroid-x64.apk 或 universal 包（含 x86_64 加载器与 arm64 guest）"
            }
        }
        val host = extractNative(ctx, apk, true)
        val guest = extractEntry(ctx, apk, GUEST_ASSET, "libakl_guest.so")
        NativeBridge.loadHost(host.absolutePath)
        val status = NativeBridge.startTranslated(guest.absolutePath, ctx.classLoader, mask, speed, attack)
        when (status) {
            2 -> log("x86_64 → arm64 探针通过；本次未启动功能引擎")
            0 -> log("x86_64 桥接引擎返回：至少一个 hook 已安装，战斗效果待确认")
            1 -> log("x86_64 桥接已进入 arm64 引擎，异步功能仍在等待验证")
            else -> log("x86_64 桥接启动失败 status=$status，查看 AKL-X64 / AKL 日志定位阶段")
        }
    }

    /**
     * libnativeloader 按调用方 ClassLoader 选择原生命名空间。
     * 用游戏 ClassLoader 作父级，让子命名空间继承其 NativeBridge 属性；
     * 只把 JNI 桥类放进子加载器，避免把 LSPosed 的宿主命名空间带进来。
     * 这是待 Waydroid 实测的加载路线，不代表 Houdini 下 hook 已验证。
     */
    private fun startInGameNamespace(
        ctx: Context, apk: String, lib: File, mask: Int, speedX100: Int, atkX10: Int,
    ) {
        val searchPath = listOf(ctx.applicationInfo.nativeLibraryDir, lib.parent)
            .filterNotNull().distinct().joinToString(File.pathSeparator)
        val loader = object : PathClassLoader(apk, searchPath, ctx.classLoader) {
            override fun loadClass(name: String, resolve: Boolean): Class<*> {
                if (name != BRIDGE_CLASS) return super.loadClass(name, resolve)
                synchronized(this) {
                    val found = findLoadedClass(name) ?: findClass(name)
                    if (resolve) resolveClass(found)
                    return found
                }
            }
        }
        val bridge = loader.loadClass(BRIDGE_CLASS)
        check(bridge.classLoader === loader) { "JNI 桥类未隔离" }
        engineClassLoader = loader
        log("游戏命名空间：parent=${ctx.classLoader}, search=$searchPath, so=$lib")
        try {
            bridge.getMethod(
                "loadAndStart", String::class.java, Int::class.javaPrimitiveType,
                Int::class.javaPrimitiveType, Int::class.javaPrimitiveType,
            ).invoke(null, lib.absolutePath, mask, speedX100, atkX10)
        } catch (e: InvocationTargetException) {
            throw (e.targetException ?: e)
        }
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
        val entry = pickLibEntry(modulePath, forceX64)
        return extractEntry(ctx, requireNotNull(modulePath), entry,
            if (forceX64) "libakl_core_x64.so" else "libakl_core.so")
    }

    private fun extractEntry(ctx: Context, modulePath: String, entry: String, name: String): File {
        val dir = File(ctx.codeCacheDir, "akl").apply { mkdirs() }
        val out = File(dir, name)
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
