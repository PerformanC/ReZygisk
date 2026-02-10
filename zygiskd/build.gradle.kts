import java.nio.file.Paths
import org.gradle.internal.os.OperatingSystem

fun getLatestNDKPath(): String {
  val android_home = System.getenv("ANDROID_HOME")
  if (android_home == null) {
    throw Exception("ANDROID_HOME not set")
  }
  // Prefer flat android-ndk-* directory if present (e.g., android-ndk-r29)
  val flatNdk = Paths.get(android_home, "android-ndk-r29").toFile()
  if (flatNdk.exists()) {
    return flatNdk.absolutePath
  }

  // Otherwise fall back to side-by-side ndk/<version>
  val ndkPath = android_home + "/ndk"
  val ndkDir = Paths.get(ndkPath)
  if (!ndkDir.toFile().exists()) {
    throw Exception("NDK not found at $ndkPath")
  }

  val ndkVersion = ndkDir.toFile().listFiles().filter { it.isDirectory }.map { it.name }.sorted().last()
  return ndkPath + "/" + ndkVersion
}

val minAPatchVersion: Int by rootProject.extra
val minKsuVersion: Int by rootProject.extra
val maxKsuVersion: Int by rootProject.extra
val minMagiskVersion: Int by rootProject.extra
val verCode: Int by rootProject.extra
val verName: String by rootProject.extra
val commitHash: String by rootProject.extra

/* INFO: Due to a bug in NDK compiler, where it says "-Wno-fixed-enum-extension" option
           doesn't exist, we must utilize "-Wno-unknown-warning-option". */
val CStandardFlags = arrayOf(
  "-D_GNU_SOURCE", "-std=c99", "-Wpedantic", "-Wall", "-Wextra", "-Werror",
  "-Wformat", "-Wuninitialized", "-Wshadow", "-Wno-zero-length-array", 
  "-Wno-fixed-enum-extension", "-Wno-unknown-warning-option", "-Wno-c23-extensions",
  "-Wconversion", "-Iroot_impl", "-llog", "-DMIN_APATCH_VERSION=$minAPatchVersion",
  "-DMIN_KSU_VERSION=$minKsuVersion",
  "-DMIN_MAGISK_VERSION=$minMagiskVersion",
  "-DZKSU_VERSION=\\\"$verName\\\""
)

val CFlagsRelease = arrayOf(
  "-Wl,--strip-all", "-flto=thin", "-O3", "-ffast-math"
)

/* INFO: arm32 on older devices (e.g., Android 8/9 without linker32) can be
          picky about aggressive flags and high API levels. Avoid LTO for arm32,
          lower optimization, and use both hash styles for loader compatibility. */
val CFlagsReleaseArm32 = arrayOf(
  "-Wl,--strip-all",
  "-Wl,--hash-style=both",
  "-fno-stack-protector",
  "-fno-builtin",
  "-O0"
)

val CFlagsDebug = arrayOf(
  "-g", "-O0", "-DDEBUG"
)

val Files = arrayOf(
  "root_impl/apatch.c",
  "root_impl/common.c",
  "root_impl/kernelsu.c",
  "root_impl/magisk.c",
  "companion.c",
  "main.c",
  "utils.c",
  "zygiskd.c"
)

task("buildAndStrip") {
  group = "build"
  description = "Build the native library and strip the debug symbols."

  val isDebug = gradle.startParameter.taskNames.any { it.lowercase().contains("debug") }
  doLast {
    val ndkPath = getLatestNDKPath()
    val hostTag = if (OperatingSystem.current().isWindows) "windows-x86_64" else "linux-x86_64"
    val compilerExt = if (OperatingSystem.current().isWindows) ".cmd" else ""

    val aarch64Compiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", hostTag, "bin", "aarch64-linux-android34-clang$compilerExt").toString()
    /* INFO: Target API 26 for arm32 per request. */
    val armv7aCompiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", hostTag, "bin", "armv7a-linux-androideabi26-clang$compilerExt").toString()
    val x86Compiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", hostTag, "bin", "i686-linux-android34-clang$compilerExt").toString()
    val x86_64Compiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", hostTag, "bin", "x86_64-linux-android34-clang$compilerExt").toString()

    if (!Paths.get(aarch64Compiler).toFile().exists()) {
      throw Exception("aarch64 compiler not found at $aarch64Compiler")
    }

    if (!Paths.get(armv7aCompiler).toFile().exists()) {
      throw Exception("armv7a compiler not found at $armv7aCompiler")
    }

    if (!Paths.get(x86Compiler).toFile().exists()) {
      throw Exception("x86 compiler not found at $x86Compiler")
    }

    if (!Paths.get(x86_64Compiler).toFile().exists()) {
      throw Exception("x86_64 compiler not found at $x86_64Compiler")
    }

    val Files = Files.map { Paths.get(project.projectDir.toString(), "src", it).toString() }.toTypedArray()

    val buildDir = getLayout().getBuildDirectory().getAsFile().get()
    buildDir.mkdirs()

    val aarch64OutputDir = Paths.get(buildDir.toString(), "arm64-v8a").toFile()
    val armv7aOutputDir = Paths.get(buildDir.toString(), "armeabi-v7a").toFile()
    val x86OutputDir = Paths.get(buildDir.toString(), "x86").toFile()
    val x86_64OutputDir = Paths.get(buildDir.toString(), "x86_64").toFile()

    aarch64OutputDir.mkdirs()
    armv7aOutputDir.mkdirs()
    x86OutputDir.mkdirs()
    x86_64OutputDir.mkdirs()

    val compileArgs = (if (isDebug) CFlagsDebug else CFlagsRelease) + CStandardFlags
    val compileArgsArm32 = (if (isDebug) CFlagsDebug else CFlagsReleaseArm32) + CStandardFlags

    exec {
      commandLine(aarch64Compiler, "-o", Paths.get(aarch64OutputDir.toString(), "zygiskd").toString(), *compileArgs, *Files)
    }
    exec {
      commandLine(armv7aCompiler, "-o", Paths.get(armv7aOutputDir.toString(), "zygiskd").toString(), *compileArgsArm32, *Files)
    }
    exec {
      commandLine(x86Compiler, "-o", Paths.get(x86OutputDir.toString(), "zygiskd").toString(), *compileArgs, *Files)
    }
    exec {
      commandLine(x86_64Compiler, "-o", Paths.get(x86_64OutputDir.toString(), "zygiskd").toString(), *compileArgs, *Files)
    }
  }
}

