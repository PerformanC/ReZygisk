import java.nio.file.Paths
import org.gradle.internal.os.OperatingSystem

fun getLatestNDKPath(): String {
  val android_home = System.getenv("ANDROID_HOME")
  if (android_home == null) {
    throw Exception("ANDROID_HOME not set")
  }

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

val CFlagsRelease = arrayOf(
  "-D_GNU_SOURCE", "-std=c99", "-Wpedantic", "-Wall", "-Wextra", "-Werror",
  "-Wformat", "-Wuninitialized", "-Wshadow", "-Wno-zero-length-array", 
  "-Wno-fixed-enum-extension", "-Iroot_impl", "-llog",
  "-Wl,--strip-all", "-flto=thin", "-Ofast"
)

val CFlagsDebug = arrayOf(
  "-D_GNU_SOURCE", "-std=c99", "-Wpedantic", "-Wall", "-Wextra", "-Werror",
  "-Wformat", "-Wuninitialized", "-Wshadow", "-Wno-zero-length-array", 
  "-Wno-fixed-enum-extension", "-Iroot_impl", "-llog",
  "-g", "-O0"
)

val Files = arrayOf(
  "root_impl/common.c",
  "root_impl/kernelsu.c",
  "companion.c",
  "dl.c",
  "main.c",
  "utils.c",
  "zygiskd.c"
)

task<Task>("buildAndStrip") {
  group = "build"
  description = "Build the native library and strip the debug symbols."

  val isDebug = gradle.startParameter.taskNames.any { it.lowercase().contains("debug") }
  doLast {
    val ndkPath = getLatestNDKPath()

    val aarch64Compiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin", "aarch64-linux-android34-clang").toString()
    val armv7aCompiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin", "armv7a-linux-androideabi34-clang").toString()

    if (!Paths.get(aarch64Compiler).toFile().exists()) {
      throw Exception("aarch64 compiler not found at $aarch64Compiler")
    }

    if (!Paths.get(armv7aCompiler).toFile().exists()) {
      throw Exception("armv7a compiler not found at $armv7aCompiler")
    }

    val Files = Files.map { Paths.get(project.projectDir.toString(), "src", it).toString() }.toTypedArray()

    val buildDir = getLayout().getBuildDirectory().getAsFile().get()
    buildDir.mkdirs()

    val compileArgs = if (isDebug) CFlagsDebug else CFlagsRelease

    val execFile = { command: Array<String> ->
      val process = Runtime.getRuntime().exec(command)
      val output = process.inputStream.bufferedReader().readText()
      process.waitFor()
      output
    }

    val aarch64OutputDir = Paths.get(buildDir.toString(), "arm64-v8a").toFile()
    aarch64OutputDir.mkdirs()

    /* INFO: Compile for aarch64 */
    val aarch64Command = arrayOf(aarch64Compiler, "-o", Paths.get(aarch64OutputDir.toString(), "zygiskd").toString(), *compileArgs, *Files)
    val aarch64CommandResult = execFile(aarch64Command)
    if (aarch64CommandResult.isNotEmpty()) {
      println(aarch64CommandResult)
    }

    val armv7aOutputDir = Paths.get(buildDir.toString(), "armeabi-v7a").toFile()
    armv7aOutputDir.mkdirs()

    /* INFO: Compile for armv7a */
    val armv7aCommand = arrayOf(armv7aCompiler, "-o", Paths.get(armv7aOutputDir.toString(), "zygiskd").toString(), *compileArgs, *Files)
    val armv7aCommandResult = execFile(armv7aCommand)
    if (armv7aCommandResult.isNotEmpty()) {
      println(armv7aCommandResult)
    }

    val x86OutputDir = Paths.get(buildDir.toString(), "x86").toFile()
    x86OutputDir.mkdirs()

    /* INFO: Compile for x86 */
    val x86Compiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin", "i686-linux-android34-clang").toString()
    val x86Command = arrayOf(x86Compiler, "-o", Paths.get(x86OutputDir.toString(), "zygiskd").toString(), *compileArgs, *Files)
    val x86CommandResult = execFile(x86Command)
    if (x86CommandResult.isNotEmpty()) {
      println(x86CommandResult)
    }

    val x86_64OutputDir = Paths.get(buildDir.toString(), "x86_64").toFile()
    x86_64OutputDir.mkdirs()

    /* INFO: Compile for x86_64 */
    val x86_64Compiler = Paths.get(ndkPath, "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin", "x86_64-linux-android34-clang").toString()
    val x86_64Command = arrayOf(x86_64Compiler, "-o", Paths.get(x86_64OutputDir.toString(), "zygiskd").toString(), *compileArgs, *Files)
    val x86_64CommandResult = execFile(x86_64Command)
    if (x86_64CommandResult.isNotEmpty()) {
      println(x86_64CommandResult)
    }
  }
}

