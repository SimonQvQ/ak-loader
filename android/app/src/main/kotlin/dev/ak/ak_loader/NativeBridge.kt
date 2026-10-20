package dev.ak.ak_loader

/** 与 libakl_core.so 的 JNI 桥（用 RegisterNatives 注册，避免名字修饰问题） */
object NativeBridge {
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
}
