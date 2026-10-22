package dev.ak.ak_loader

/** 与 libakl_core.so 的 JNI 桥（用 RegisterNatives 注册，避免名字修饰问题） */
object NativeBridge {
    fun loadHost(path: String) {
        if (!loaded) {
            System.load(path)
            loaded = true
        }
    }

    /** 必须在声明 JNI 方法的同一个 ClassLoader 内调用 System.load。 */
    @JvmStatic
    fun loadAndStart(path: String, mask: Int, speedX100: Int, atkX10: Int) {
        System.load(path)
        start(mask, speedX100, atkX10)
    }

    @Volatile var loaded: Boolean = false
        private set

    fun load(): Boolean {
        if (loaded) return true
        return try {
            System.loadLibrary("akl_core")
            loaded = true
            true
        } catch (t: Throwable) {
            false
        }
    }

    external fun start(mask: Int, speedX100: Int, atkX10: Int)
    external fun startTranslated(
        guestPath: String, gameLoader: ClassLoader, mask: Int, speedX100: Int, atkX10: Int,
    ): Int
}
