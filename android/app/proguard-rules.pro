# Xposed 入口由 assets/xposed_init 以字符串方式反射加载，R8 看不到引用
-keep class dev.ak.ak_loader.XposedEntry { *; }
-keep class dev.ak.ak_loader.NativeBridge { *; }
-keepclasseswithmembers class * { native <methods>; }
-keep class de.robv.android.xposed.** { *; }
-dontwarn de.robv.android.xposed.**
