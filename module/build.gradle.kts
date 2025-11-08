import android.databinding.tool.ext.capitalizeUS
import java.security.MessageDigest
import org.apache.tools.ant.filters.ReplaceTokens

import org.apache.tools.ant.filters.FixCrLfFilter

import org.apache.commons.codec.binary.Hex
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.Signature
import java.security.interfaces.EdECPrivateKey
import java.security.interfaces.EdECPublicKey
import java.security.spec.EdECPrivateKeySpec
import java.security.spec.NamedParameterSpec
import java.util.TreeSet

plugins {
    alias(libs.plugins.agp.lib)
}

val moduleId: String by rootProject.extra
val moduleName: String by rootProject.extra
val verCode: Int by rootProject.extra
val verName: String by rootProject.extra
val minAPatchVersion: Int by rootProject.extra
val minKsuVersion: Int by rootProject.extra
val minKsudVersion: Int by rootProject.extra
val maxKsuVersion: Int by rootProject.extra
val minMagiskVersion: Int by rootProject.extra
val commitHash: String by rootProject.extra

android.buildFeatures {
    androidResources = false
    buildConfig = false
}

androidComponents.onVariants { variant ->
    val variantLowered = variant.name.lowercase()
    val variantCapped = variant.name.capitalizeUS()
    val buildTypeLowered = variant.buildType?.lowercase()

    val moduleDir = layout.buildDirectory.dir("outputs/module/$variantLowered")
    val zipFileName = "$moduleName-$verName-$verCode-$commitHash-$buildTypeLowered.zip".replace(' ', '-')

    val prepareModuleFilesTask = task<Sync>("prepareModuleFiles$variantCapped") {
        group = "module"
        dependsOn(
            ":loader:assemble$variantCapped",
            ":zygiskd:buildAndStrip",
        )
        into(moduleDir)
        from("${rootProject.projectDir}/README.md")
        from("$projectDir/src") {
            exclude("module.prop", "customize.sh", "post-fs-data.sh", "service.sh", "uninstall.sh")
            filter<FixCrLfFilter>("eol" to FixCrLfFilter.CrLf.newInstance("lf"))
        }
        from("$projectDir/src") {
            include("module.prop")
            expand(
                "moduleId" to moduleId,
                "moduleName" to moduleName,
                "versionName" to "$verName ($verCode-$commitHash-$variantLowered)",
                "versionCode" to verCode
            )
        }
        from("$projectDir/src") {
            include("customize.sh", "post-fs-data.sh", "service.sh", "uninstall.sh")
            val tokens = mapOf(
                "DEBUG" to if (buildTypeLowered == "debug") "true" else "false",
                "MIN_APATCH_VERSION" to "$minAPatchVersion",
                "MIN_KSU_VERSION" to "$minKsuVersion",
                "MIN_KSUD_VERSION" to "$minKsudVersion",
                "MIN_MAGISK_VERSION" to "$minMagiskVersion",
            )
            filter<ReplaceTokens>("tokens" to tokens)
            filter<FixCrLfFilter>("eol" to FixCrLfFilter.CrLf.newInstance("lf"))
        }
        into("bin") {
            from(project(":zygiskd").layout.buildDirectory.getAsFile().get())
            include("**/zygiskd")
        }
        into("lib") {
            from(project(":loader").layout.buildDirectory.file("intermediates/stripped_native_libs/$variantLowered/out/lib"))
        }
        into("webroot") {
            from("${rootProject.projectDir}/webroot")
        }

        val root = moduleDir.get()

        doLast {
            if (file("private_key").exists()) {
                val privateKey = file("private_key").readBytes()
                val publicKey = file("public_key").readBytes()
                val namedSpec = NamedParameterSpec("ed25519")
                val privKeySpec = EdECPrivateKeySpec(namedSpec, privateKey)
                val kf = KeyFactory.getInstance("ed25519")
                val privKey = kf.generatePrivate(privKeySpec);
                val sig = Signature.getInstance("ed25519")
                fun File.sha(realFile: File? = null) {
                    sig.update(this.name.toByteArray())
                    sig.update(0) // null-terminated string
                    val real = realFile ?: this
                    val buffer = ByteBuffer.allocate(8)
                        .order(ByteOrder.LITTLE_ENDIAN)
                        .putLong(real.length())
                        .array()
                    sig.update(buffer)
                    real.forEachBlock { bytes, size ->
                        sig.update(bytes, 0, size)
                    }
                }

                /* INFO: Misaki is the file that holds signed hash of
                           all files of ReZygisk module, to ensure the
                           zip (runtime and non-runtime) files hasn't
                           been tampered with.
                */
                fun misakiSign() {
                    sig.initSign(privKey)

                    val filesToProcess = TreeSet<File> { f1, f2 ->
                        f1.path.replace("\\", "/")
                            .compareTo(f2.path.replace("\\", "/"))
                    }

                    root.asFile.walkTopDown().forEach { file ->
                        if (!file.isFile) return@forEach

                        val fileName = file.name
                        if (fileName == "misaki.sig") return@forEach

                        filesToProcess.add(file)
                    }

                    filesToProcess.forEach { file -> file.sha(file) }

                    val misakiSignatureFile = root.file("misaki.sig").asFile
                    misakiSignatureFile.writeBytes(sig.sign())
                    misakiSignatureFile.appendBytes(publicKey)
                }

                fun getSign(name: String, abi: String, is64Bit: Boolean) {
                    val set = TreeSet<Pair<File, File?>> { o1, o2 ->
                        o1.first.path.replace("\\", "/")
                            .compareTo(o2.first.path.replace("\\", "/"))
                    }

                    val archSuffix = if (is64Bit) "64" else "32"
                    val pathSuffix = if (is64Bit) "lib64" else "lib"

                    set.add(Pair(root.file("module.prop").asFile, null))
                    set.add(Pair(root.file("sepolicy.rule").asFile, null))
                    set.add(Pair(root.file("post-fs-data.sh").asFile, null))
                    set.add(Pair(root.file("service.sh").asFile, null))
                    set.add(
                        Pair(
                            root.file("$pathSuffix/libzygisk.so").asFile,
                            root.file("lib/$abi/libzygisk.so").asFile
                        )
                    )
                    set.add(
                        Pair(
                            root.file("bin/zygisk-ptrace$archSuffix").asFile,
                            root.file("lib/$abi/libzygisk_ptrace.so").asFile
                        )
                    )
                    set.add(
                        Pair(
                            root.file("bin/zygiskd$archSuffix").asFile,
                            root.file("bin/$abi/zygiskd").asFile
                        )
                    )

                    sig.initSign(privKey)
                    set.forEach { it.first.sha(it.second) }
                    val signFile = root.file(name).asFile
                    signFile.writeBytes(sig.sign())
                    signFile.appendBytes(publicKey)
                }

                /* INFO: Machikado is the name of files that holds signed hash of
                           all runtime files of ReZygisk module, to ensure the
                           runtime files hasn't been tampered with.
                */
                println("=== Guards the peace of Machikado ===")

                getSign("machikado.arm64", "arm64-v8a", true)
                getSign("machikado.arm", "armeabi-v7a", false)

                getSign("machikado.x86_64", "x86_64", true)
                getSign("machikado.x86", "x86", false)

                fileTree(moduleDir).visit {
                    if (isDirectory) return@visit

                    val md = MessageDigest.getInstance("SHA-256")
                    file.forEachBlock(4096) { bytes, size ->
                        md.update(bytes, 0, size)
                    }

                    file(file.path + ".sha256").writeText(Hex.encodeHexString(md.digest()))
                }

                println("===   At the kitsune's wedding   ===")

                misakiSign()
            } else {
                println("no private_key found, this build will not be signed")

                root.file("machikado.arm64").asFile.createNewFile()
                root.file("machikado.arm").asFile.createNewFile()

                root.file("machikado.x86_64").asFile.createNewFile()
                root.file("machikado.x86").asFile.createNewFile()

                fileTree(moduleDir).visit {
                    if (isDirectory) return@visit

                    val md = MessageDigest.getInstance("SHA-256")
                    file.forEachBlock(4096) { bytes, size ->
                        md.update(bytes, 0, size)
                    }

                    file(file.path + ".sha256").writeText(Hex.encodeHexString(md.digest()))
                }

                root.file("misaki.sig").asFile.createNewFile()
            }
        }
    }

    val zipTask = task<Zip>("zip$variantCapped") {
        group = "module"
        dependsOn(prepareModuleFilesTask)
        archiveFileName.set(zipFileName)
        destinationDirectory.set(layout.buildDirectory.file("outputs/release").get().asFile)
        from(moduleDir)
    }

    val pushTask = task<Exec>("push$variantCapped") {
        group = "module"
        dependsOn(zipTask)
        commandLine("adb", "push", zipTask.outputs.files.singleFile.path, "/data/local/tmp")
    }

    val installKsuTask = task("installKsu$variantCapped") {
        group = "module"
        dependsOn(pushTask)
        doLast {
            exec {
                commandLine(
                    "adb", "shell", "echo",
                    "/data/adb/ksud module install /data/local/tmp/$zipFileName",
                    "> /data/local/tmp/install.sh"
                )
            }
            exec { commandLine("adb", "shell", "chmod", "755", "/data/local/tmp/install.sh") }
            exec { commandLine("adb", "shell", "su", "-c", "/data/local/tmp/install.sh") }
        }
    }

    val installAPatchTask = task<Exec>("installAPatch$variantCapped") {
        group = "module"
        dependsOn(pushTask)
        commandLine("adb", "shell", "su", "-c", "/data/adb/apd module install /data/local/tmp/$zipFileName")
    }

    val installMagiskTask = task<Exec>("installMagisk$variantCapped") {
        group = "module"
        dependsOn(pushTask)
        commandLine("adb", "shell", "su", "-M", "-c", "magisk --install-module /data/local/tmp/$zipFileName")
    }

    task<Exec>("installAPatchAndReboot$variantCapped") {
        group = "module"
        dependsOn(installAPatchTask)
        commandLine("adb", "reboot")
    }

    task<Exec>("installKsuAndReboot$variantCapped") {
        group = "module"
        dependsOn(installKsuTask)
        commandLine("adb", "reboot")
    }

    task<Exec>("installMagiskAndReboot$variantCapped") {
        group = "module"
        dependsOn(installMagiskTask)
        commandLine("adb", "reboot")
    }
}
