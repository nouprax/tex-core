import groovy.json.JsonSlurper
import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.publish.maven.MavenPublication
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.jvm.tasks.Jar
import org.gradle.language.jvm.tasks.ProcessResources
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import org.jetbrains.kotlin.gradle.dsl.KotlinVersion
import org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget
import org.jetbrains.kotlin.gradle.targets.jvm.KotlinJvmTarget
import org.jetbrains.kotlin.gradle.targets.native.tasks.KotlinNativeTest
import java.io.DataInputStream
import java.util.zip.ZipFile

// Generates the common-test conformance data from the shared render-tree
// manifest. Inputs are carried as hex bytes — they are byte-exact compile
// input and one case is deliberately invalid UTF-8 — while expected dumps
// are UTF-8 text.
@CacheableTask
abstract class GenerateRenderTreeFixtures : DefaultTask() {
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val specDirectory: DirectoryProperty

    @get:OutputFile
    abstract val outputFile: RegularFileProperty

    @TaskAction
    fun generate() {
        val root =
            specDirectory
                .get()
                .asFile.canonicalFile
                .toPath()
        val manifestFile = root.resolve("manifest.json").toFile()
        val manifest = JsonSlurper().parse(manifestFile) as? Map<*, *> ?: error("manifest must be an object")
        require((manifest["schemaVersion"] as? Number)?.toInt() == 5) {
            "shared render-tree manifest must use schemaVersion 5"
        }
        val cases = manifest["cases"] as? List<*> ?: error("manifest cases must be an array")
        require(cases.isNotEmpty()) { "shared render-tree manifest must contain at least one case" }

        fun caseBytes(relativePath: String): ByteArray {
            val path =
                root
                    .resolve(relativePath)
                    .normalize()
                    .toFile()
                    .canonicalFile
                    .toPath()
            require(path.startsWith(root)) { "render-tree case path escapes the spec directory: $relativePath" }
            return path.toFile().readBytes()
        }

        val lines =
            mutableListOf(
                "package com.nouprax.tex.core",
                "",
                "import com.nouprax.tex.core.model.CompileMode",
                "import com.nouprax.tex.core.model.CompileOptions",
                "",
                "internal enum class RenderTreeOutcome { TREE, ERROR }",
                "",
                "internal data class RenderTreeCase(",
                "    val name: String,",
                "    val sourceHex: String,",
                "    val expected: String,",
                "    val options: CompileOptions,",
                "    val outcome: RenderTreeOutcome,",
                ") {",
                "    val source: ByteArray",
                "        get() {",
                "            return ByteArray(sourceHex.length / 2) { index ->",
                "                sourceHex.substring(2 * index, 2 * index + 2).toInt(16).toByte()",
                "            }",
                "        }",
                "}",
                "",
                "internal val renderTreeCases: kotlin.collections.List<RenderTreeCase> =",
                "    listOf(",
            )

        for (rawCase in cases) {
            val testCase = rawCase as? Map<*, *> ?: error("every manifest case must be an object")
            val name = testCase["name"] as? String ?: error("case name must be a string")
            val input = testCase["input"] as? String ?: error("$name input must be a string")
            val expectedPath = testCase["expected"] as? String ?: error("$name expected must be a string")
            require(input == "$name.tex" && expectedPath == "$name.tree") {
                "$name has non-canonical fixture paths"
            }
            val compileOptions =
                testCase["compileOptions"] as? Map<*, *> ?: error("$name compileOptions must be an object")
            val mode =
                when (compileOptions["mode"]) {
                    "document" -> "CompileMode.DOCUMENT"
                    "mathInline" -> "CompileMode.MATH_INLINE"
                    "mathDisplay" -> "CompileMode.MATH_DISPLAY"
                    else -> error("$name has an unknown mode")
                }
            val outcome =
                when (testCase["outcome"]) {
                    "tree" -> "RenderTreeOutcome.TREE"
                    "error" -> "RenderTreeOutcome.ERROR"
                    else -> error("$name has an unknown outcome")
                }
            val sourceHex =
                caseBytes(input).joinToString("") { byte ->
                    (byte.toInt() and 0xFF).toString(16).padStart(2, '0')
                }
            val expected = caseBytes(expectedPath).toString(Charsets.UTF_8)

            lines += "        RenderTreeCase("
            lines += "            name = ${kotlinLiteral(name)},"
            appendStringProperty(lines, "sourceHex", sourceHex)
            appendStringProperty(lines, "expected", expected)
            lines += "            options = CompileOptions($mode),"
            lines += "            outcome = $outcome,"
            lines += "        ),"
        }
        lines += "    )"
        lines += ""

        val destination = outputFile.get().asFile
        destination.parentFile.mkdirs()
        destination.writeText(lines.joinToString("\n"))
    }

    private fun appendStringProperty(
        lines: MutableList<String>,
        name: String,
        value: String,
    ) {
        lines += "            $name ="
        lines += "                buildString {"
        for (chunk in value.chunked(30)) {
            lines += "                    append(${kotlinLiteral(chunk)})"
        }
        lines += "                },"
    }

    private fun kotlinLiteral(value: String): String =
        buildString {
            append('"')
            for (character in value) {
                when (character) {
                    '\\' -> {
                        append("\\\\")
                    }

                    '"' -> {
                        append("\\\"")
                    }

                    '\n' -> {
                        append("\\n")
                    }

                    '\r' -> {
                        append("\\r")
                    }

                    '\t' -> {
                        append("\\t")
                    }

                    '$' -> {
                        append('\\')
                        append('$')
                    }

                    else -> {
                        append(character)
                    }
                }
            }
            append('"')
        }
}

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.kotlin.multiplatform.library)
    alias(libs.plugins.dokka)
    alias(libs.plugins.ktlint)
    `maven-publish`
}

group = "com.nouprax"
version = rootProject.file("VERSION").readText().trim()

dokka {
    dokkaSourceSets.configureEach {
        // Public API is defined in commonMain. Platform source sets contain
        // internal actuals only, and both Native targets share one source
        // root that should not be rendered twice.
        if (name != "commonMain") {
            suppress.set(true)
        }
    }
}

dependencyLocking {
    lockAllConfigurations()
}

val repositoryRoot = rootProject.layout.projectDirectory
val renderTreeSpecDirectory = repositoryRoot.dir("specs/render-tree")
val generatedRenderTreeSource =
    layout.buildDirectory.file(
        "generated/renderTreeCommonTest/kotlin/com/nouprax/tex/core/RenderTreeCases.kt",
    )

// One closed model drives every host-dependent output and task selection.
// An unsupported host stops configuration instead of silently publishing
// a payload under a compatible-looking label.
data class HostTriple(
    val os: String,
    val architecture: String,
) {
    val platform: String get() = "$os-$architecture"
    val kotlinNativeTarget: String get() = if (os == "macos") "macosArm64" else "linuxX64"
    val managedDeviceAbi: String get() = if (architecture == "arm64") "arm64-v8a" else "x86_64"
    val nativeLibraryFileName: String
        get() = if (os == "macos") "libtex_core_kotlin.dylib" else "libtex_core_kotlin.so"
}

val supportedHostTriples = setOf(HostTriple("macos", "arm64"), HostTriple("linux", "x64"))
val hostTriple =
    run {
        val osName = System.getProperty("os.name").lowercase()
        val architectureName = System.getProperty("os.arch").lowercase()
        val os =
            when {
                osName.contains("mac") -> "macos"
                osName.contains("linux") -> "linux"
                osName.contains("windows") -> "windows"
                else -> osName
            }
        val architecture =
            when (architectureName) {
                "aarch64", "arm64" -> "arm64"
                "amd64", "x86_64" -> "x64"
                else -> architectureName
            }
        val triple = HostTriple(os, architecture)
        require(triple in supportedHostTriples) {
            "Unsupported build host: $osName/$architectureName. Supported hosts: " +
                supportedHostTriples.joinToString { it.platform } + "."
        }
        triple
    }
val hostOs = hostTriple.os
val androidManagedDeviceTestAbi = hostTriple.managedDeviceAbi
val jvmNativeBuildDirectory = layout.buildDirectory.dir("native/jvm")
val jvmNativeResourceDirectory = layout.buildDirectory.dir("generated/jvmResources")
val desktopPlatform = hostTriple.platform
// The Apple support floor (Package.swift declares macOS 15): every native
// artifact built on a macOS host pins this deployment target so a newer
// toolchain cannot silently raise the artifact's minimum OS.
val macosDeploymentTarget = "15.0"
val nativeOutputDirectory =
    jvmNativeResourceDirectory.map {
        it.dir("com/nouprax/tex/core/native/$desktopPlatform")
    }
val androidRuntimeAar =
    project(":packages:kotlin-tex-core:android-runtime")
        .layout.buildDirectory
        .file("outputs/aar/android-runtime-release.aar")

val generateRenderTreeCommonTest =
    tasks.register<GenerateRenderTreeFixtures>("generateRenderTreeCommonTest") {
        group = "verification"
        description = "Generates common-test conformance data from the root render-tree manifest."
        specDirectory.set(renderTreeSpecDirectory)
        outputFile.set(generatedRenderTreeSource)
    }

fun KotlinNativeTarget.configureNativeBridge() {
    val capitalizedTarget = name.replaceFirstChar { it.uppercase() }
    val buildDirectory = layout.buildDirectory.dir("native/$name")
    val archiveDirectory = layout.buildDirectory.dir("native/$name/archives")
    val generatedDefinitionDirectory = layout.buildDirectory.dir("generated/cinterop/$name")
    val configureTask =
        tasks.register<Exec>("configure${capitalizedTarget}NativeBridge") {
            inputs.files(
                repositoryRoot.files("CMakeLists.txt"),
                repositoryRoot.dir("packages/tex-core/core"),
                repositoryRoot.dir("packages/tex-core/bridge"),
                repositoryRoot.dir("packages/tex-core/include"),
                layout.projectDirectory.dir("src/native"),
            )
            // Configure owns only its stamp: the build task mutates the rest
            // of the build tree, so claiming the whole directory here would
            // give the two tasks overlapping outputs and unreliable
            // up-to-date checks.
            outputs.file(buildDirectory.map { it.file("CMakeCache.txt") })
            commandLine(
                buildList {
                    add("cmake")
                    add("-S")
                    add(repositoryRoot.asFile.absolutePath)
                    add("-B")
                    add(buildDirectory.get().asFile.absolutePath)
                    add("-DTEX_CORE_TESTS=OFF")
                    add("-DTEX_CORE_SHARED=OFF")
                    add("-DTEX_CORE_STATIC=ON")
                    add("-DTEX_CORE_KOTLIN_NATIVE=ON")
                    add("-DCMAKE_BUILD_TYPE=Release")
                    add("-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${archiveDirectory.get().asFile.absolutePath}")
                    if (hostOs.contains("mac")) {
                        add("-DCMAKE_OSX_DEPLOYMENT_TARGET=$macosDeploymentTarget")
                    }
                },
            )
        }
    val buildTask =
        tasks.register<Exec>("build${capitalizedTarget}NativeBridge") {
            dependsOn(configureTask)
            inputs.files(
                repositoryRoot.dir("packages/tex-core/core"),
                repositoryRoot.dir("packages/tex-core/bridge"),
                repositoryRoot.dir("packages/tex-core/include"),
                layout.projectDirectory.dir("src/native"),
            )
            outputs.dir(archiveDirectory)
            commandLine(
                "cmake",
                "--build",
                buildDirectory.get().asFile.absolutePath,
                "--config",
                "Release",
                "--target",
                "tex_core_kotlin_native",
                "--target",
                "libtex-core_static",
                "--parallel",
            )
        }
    val generateDefinition =
        tasks.register<Copy>("generate${capitalizedTarget}NativeDefinition") {
            from(layout.projectDirectory.file("src/nativeInterop/cinterop/tex_core_kotlin.def"))
            into(generatedDefinitionDirectory)
            filter { line: String ->
                line.replace("@LIBRARY_PATH@", archiveDirectory.get().asFile.absolutePath)
            }
        }

    compilations.getByName("main").cinterops.create("texCoreKotlin") {
        definitionFile.set(generatedDefinitionDirectory.map { it.file("tex_core_kotlin.def") })
        compilerOpts("-I${layout.projectDirectory.dir("src/native").asFile.absolutePath}")
        tasks.named(interopProcessingTaskName).configure {
            dependsOn(buildTask, generateDefinition)
            inputs.dir(archiveDirectory)
        }
    }
}

val hostNativeTest = "${hostTriple.kotlinNativeTarget}Test"
val hostNativeConformanceTest = "${hostTriple.kotlinNativeTarget}ConformanceTest"

val configureJvmNative =
    tasks.register<Exec>("configureJvmNative") {
        inputs.files(
            repositoryRoot.files("CMakeLists.txt"),
            repositoryRoot.dir("packages/tex-core/core"),
            repositoryRoot.dir("packages/tex-core/bridge"),
            repositoryRoot.dir("packages/tex-core/include"),
            layout.projectDirectory.dir("src/native"),
        )
        // Configure owns only its stamp; the build task owns the payload
        // directory, so the two tasks never claim overlapping outputs.
        outputs.file(jvmNativeBuildDirectory.map { it.file("CMakeCache.txt") })
        commandLine(
            buildList {
                add("cmake")
                add("-S")
                add(repositoryRoot.asFile.absolutePath)
                add("-B")
                add(jvmNativeBuildDirectory.get().asFile.absolutePath)
                add("-DTEX_CORE_TESTS=OFF")
                add("-DTEX_CORE_SHARED=OFF")
                add("-DTEX_CORE_STATIC=ON")
                add("-DTEX_CORE_KOTLIN_JNI=ON")
                add("-DCMAKE_BUILD_TYPE=Release")
                add("-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${nativeOutputDirectory.get().asFile.absolutePath}")
                add("-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${nativeOutputDirectory.get().asFile.absolutePath}")
                if (hostOs.contains("mac")) {
                    add("-DCMAKE_OSX_DEPLOYMENT_TARGET=$macosDeploymentTarget")
                }
            },
        )
    }

val buildJvmNative =
    tasks.register<Exec>("buildJvmNative") {
        dependsOn(configureJvmNative)
        inputs.files(
            repositoryRoot.dir("packages/tex-core/core"),
            repositoryRoot.dir("packages/tex-core/bridge"),
            repositoryRoot.dir("packages/tex-core/include"),
            layout.projectDirectory.dir("src/native"),
        )
        outputs.dir(nativeOutputDirectory)
        commandLine(
            "cmake",
            "--build",
            jvmNativeBuildDirectory.get().asFile.absolutePath,
            "--config",
            "Release",
            "--target",
            "tex_core_kotlin_jni",
            "--parallel",
        )
    }

kotlin {
    explicitApi()
    compilerOptions {
        languageVersion.set(KotlinVersion.KOTLIN_2_2)
        apiVersion.set(KotlinVersion.KOTLIN_2_2)
    }

    jvm {
        compilerOptions.jvmTarget.set(JvmTarget.JVM_17)
        // Emitting 17 bytecode is not enough: without a JDK API release
        // level the build can reference newer java.* APIs and fail only on
        // the minimum runtime promised to consumers.
        compilerOptions.freeCompilerArgs.add("-Xjdk-release=17")
        withSourcesJar(publish = true)
        testRuns["test"].executionTask.configure {
            useJUnitPlatform()
            filter.excludeTestsMatching("*ConformanceTest*")
        }
        testRuns.create("conformance") {
            executionTask.configure {
                useJUnitPlatform()
                filter.includeTestsMatching("*ConformanceTest*")
            }
        }
    }

    android {
        namespace = "com.nouprax.tex.core"
        compileSdk =
            libs.versions.android.compile.sdk
                .get()
                .toInt()
        minSdk =
            libs.versions.android.min.sdk
                .get()
                .toInt()
        withJava()
        withHostTestBuilder {}.configure {}
        withDeviceTestBuilder { sourceSetTreeName = "test" }.configure {
            instrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
            managedDevices {
                localDevices {
                    create("texCoreApi36Page4k") {
                        device = "Pixel 10 Pro XL"
                        apiLevel = 36
                        systemImageSource = "google"
                        require64Bit = true
                        pageAlignment =
                            com.android.build.api.dsl.ManagedVirtualDevice.PageAlignment
                                .FORCE_4KB_PAGES
                    }
                    create("texCoreApi36Page16k") {
                        device = "Pixel 10 Pro XL"
                        apiLevel = 36
                        systemImageSource = "google"
                        require64Bit = true
                        pageAlignment =
                            com.android.build.api.dsl.ManagedVirtualDevice.PageAlignment
                                .FORCE_16KB_PAGES
                    }
                    configureEach { testedAbi = androidManagedDeviceTestAbi }
                }
                groups {
                    create("texCoreAndroidPageSizes") {
                        targetDevices.add(localDevices["texCoreApi36Page4k"])
                        targetDevices.add(localDevices["texCoreApi36Page16k"])
                    }
                }
            }
        }
        compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
    }

    macosArm64 {
        configureNativeBridge()
        testRuns.create("conformance")
    }
    linuxX64 {
        configureNativeBridge()
        testRuns.create("conformance")
    }

    sourceSets {
        commonMain.dependencies {
            // The library is synchronous: production code takes no
            // coroutine dependency. Only the concurrency tests use
            // kotlinx-coroutines (test-scoped below), so consumers get no
            // transitive coroutines requirement.
            api(libs.kotlin.stdlib)
        }
        commonTest {
            kotlin.srcDir(layout.buildDirectory.dir("generated/renderTreeCommonTest/kotlin"))
            dependencies {
                implementation(kotlin("test"))
                implementation(libs.kotlinx.coroutines.core)
                implementation(libs.kotlinx.coroutines.test)
            }
        }
        jvmTest.dependencies { implementation(kotlin("test-junit5")) }
        getByName("androidDeviceTest").dependencies {
            implementation("androidx.test.ext:junit:1.3.0")
            implementation("androidx.test:runner:1.7.0")
        }
        androidMain.dependencies {
            implementation(
                project.dependencies.project(":packages:kotlin-tex-core:android-runtime"),
            )
        }
        macosArm64Main { kotlin.srcDir("src/nativePlatformMain/kotlin") }
        linuxX64Main { kotlin.srcDir("src/nativePlatformMain/kotlin") }
    }
}

tasks.matching { it.name.startsWith("compile") && it.name.contains("Test") }.configureEach {
    dependsOn(generateRenderTreeCommonTest)
}
tasks.matching { it.name.startsWith("runKtlint") && it.name.contains("CommonTest") }.configureEach {
    dependsOn(generateRenderTreeCommonTest)
}

tasks.named<ProcessResources>("jvmProcessResources") {
    dependsOn(buildJvmNative)
    from(jvmNativeResourceDirectory)
    // BSD-2-Clause requires binary redistributions to reproduce the license
    // text; ship it inside the JVM jar.
    from(repositoryRoot.file("LICENSE")) { into("META-INF") }
}

// Byte-reproducible archives: identical inputs must produce identical jars
// regardless of file-system ordering or build time, so release artifacts
// can be independently rebuilt and compared.
tasks.withType<AbstractArchiveTask>().configureEach {
    isPreserveFileTimestamps = false
    isReproducibleFileOrder = true
}

val jvmTarget = kotlin.targets.getByName("jvm") as KotlinJvmTarget
val jvmMainCompilation = jvmTarget.compilations.getByName("main")
val jvmTestCompilation = jvmTarget.compilations.getByName("test")
tasks.register<Sync>("stageJvmTestArtifact") {
    dependsOn("jvmTestClasses", "jvmProcessResources")
    into(layout.buildDirectory.dir("ci-test-artifact/jvm"))
    from(jvmMainCompilation.output.allOutputs) { into("classes") }
    from(jvmTestCompilation.output.allOutputs) { into("classes") }
    from(jvmTestCompilation.runtimeDependencyFiles.filter(File::isFile)) { into("lib") }
}

val stageAndroidHostTestArtifact =
    tasks.register<Sync>("stageAndroidHostTestArtifact") {
        dependsOn(buildJvmNative, "compileAndroidHostTest")
        duplicatesStrategy = DuplicatesStrategy.EXCLUDE
        into(layout.buildDirectory.dir("ci-test-artifact/android-host"))
        from(nativeOutputDirectory) { into("native") }
    }
val benchmarkCompilation =
    jvmTarget.compilations.create("benchmark") {
        associateWith(jvmTarget.compilations.getByName("main"))
    }

tasks.register<JavaExec>("kotlinBenchmark") {
    group = "benchmark"
    description = "Runs Kotlin/JNI compile and immutable render-tree copy performance workloads."
    dependsOn(benchmarkCompilation.compileTaskProvider, "jvmProcessResources")
    classpath =
        benchmarkCompilation.output.allOutputs +
        jvmTarget.compilations
            .getByName("main")
            .output.allOutputs +
        benchmarkCompilation.runtimeDependencyFiles
    mainClass.set("com.nouprax.tex.core.benchmark.BenchmarkKt")
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}

tasks.register<Sync>("stageJvmBenchmarkArtifact") {
    dependsOn(benchmarkCompilation.compileTaskProvider, "jvmProcessResources")
    into(layout.buildDirectory.dir("ci-test-artifact/jvm-benchmark"))
    from(jvmMainCompilation.output.allOutputs) { into("classes") }
    from(benchmarkCompilation.output.allOutputs) { into("classes") }
    from(benchmarkCompilation.runtimeDependencyFiles.filter(File::isFile)) { into("lib") }
}

tasks.withType<Test>().configureEach {
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}

for (target in listOf("macosArm64", "linuxX64")) {
    tasks.named<KotlinNativeTest>("${target}Test") {
        filter.excludeTestsMatching("*ConformanceTest*")
    }
    tasks.named<KotlinNativeTest>("${target}ConformanceTest") {
        filter.includeTestsMatching("*ConformanceTest*")
    }
}

val hostNativeLibraryPath =
    nativeOutputDirectory
        .get()
        .asFile
        .resolve(
            if (desktopPlatform.startsWith("macos")) {
                "libtex_core_kotlin.dylib"
            } else {
                "libtex_core_kotlin.so"
            },
        ).absolutePath
tasks.withType<Test>().matching { it.name == "testAndroidHostTest" }.configureEach {
    dependsOn(buildJvmNative)
    filter.excludeTestsMatching("*ConformanceTest*")
    systemProperty("tex.core.hostNativeLibrary", hostNativeLibraryPath)
}
val androidHostConformanceTest =
    tasks.register<Test>("androidHostConformanceTest") {
        group = "verification"
        description = "Runs Android host public-contract conformance checks."
        dependsOn(buildJvmNative, "compileAndroidHostTest")
        filter.includeTestsMatching("*ConformanceTest*")
        systemProperty("tex.core.hostNativeLibrary", hostNativeLibraryPath)
    }
afterEvaluate {
    val sourceTask = tasks.named<Test>("testAndroidHostTest").get()
    androidHostConformanceTest.configure {
        testClassesDirs = sourceTask.testClassesDirs
        classpath = sourceTask.classpath
    }
    stageAndroidHostTestArtifact.configure {
        from(sourceTask.testClassesDirs) { into("classes") }
        from(sourceTask.classpath.filter(File::isDirectory)) { into("classes") }
        from(sourceTask.classpath.filter(File::isFile)) { into("lib") }
    }
}

val javadocJar =
    tasks.register<Jar>("javadocJar") {
        archiveClassifier.set("javadoc")
        // Publish actual generated API reference; the spec and package
        // guide remain supplementary documentation.
        from(tasks.named("dokkaGeneratePublicationHtml"))
        from(repositoryRoot.file("docs/specs/render-tree.md"))
        from(layout.projectDirectory.file("README.md"))
        // Attached to every publication, so each Maven artifact set carries
        // the BSD-2-Clause text its binaries redistribute under.
        from(repositoryRoot.file("LICENSE")) { into("META-INF") }
    }

publishing {
    repositories {
        providers.gradleProperty("releaseRepositoryDir").orNull?.let { repositoryDirectory ->
            maven {
                name = "releaseStaging"
                url = uri(repositoryDirectory)
            }
        }
    }
    publications.withType<MavenPublication>().configureEach {
        pom {
            name.set("Kotlin TeX Core")
            description.set("Immutable Kotlin Multiplatform render-tree bindings for TeX Core.")
            url.set("https://github.com/nouprax/tex-core")
            licenses {
                license {
                    name.set("BSD-2-Clause")
                    url.set("https://github.com/nouprax/tex-core/blob/main/LICENSE")
                }
            }
            scm {
                connection.set("scm:git:https://github.com/nouprax/tex-core.git")
                developerConnection.set("scm:git:ssh://git@github.com/nouprax/tex-core.git")
                url.set("https://github.com/nouprax/tex-core")
            }
            developers {
                developer {
                    id.set("nouprax")
                    name.set("Nouprax")
                    url.set("https://github.com/nouprax")
                }
            }
        }
        artifact(javadocJar)
    }
}

ktlint {
    version.set(libs.versions.ktlintCli)
    android.set(true)
    outputToConsole.set(true)
    ignoreFailures.set(false)
}

// AGP 9.2.1 exposed testedAbi in the managed-device DSL but its setup-task
// CreationAction did not copy that value into the task input. Keep the public
// DSL declaration above and the explicit task input until the two-device
// remote smoke proves the upstream assignment on both page sizes.
tasks.withType<com.android.build.gradle.internal.tasks.ManagedDeviceInstrumentationTestSetupTask>().configureEach {
    testedAbi.set(androidManagedDeviceTestAbi)
}

// Freeze the ordinary Java-visible JVM surface. Kotlin `internal`
// declarations can still compile to public bytecode, so inspect every
// public class plus public/protected non-synthetic member. The class
// hierarchy is part of the ABI as well as the member descriptors.
val verifyJvmAbi =
    tasks.register("verifyJvmAbi") {
        group = "verification"
        description = "Compares the JVM jar's public ABI against the checked-in snapshot."
        dependsOn("jvmJar")
        val jarFile = tasks.named<Jar>("jvmJar").flatMap { it.archiveFile }
        val snapshotFile = layout.projectDirectory.file("jvm-abi.txt").asFile
        val write = providers.gradleProperty("writeJvmAbi").isPresent
        inputs.file(jarFile)

        doLast {
            fun utf8At(
                pool: Array<Any?>,
                index: Int,
            ): String = pool[index] as? String ?: error("constant pool index $index is not utf8")

            fun classSurface(bytes: ByteArray): kotlin.collections.List<String> {
                val input = DataInputStream(bytes.inputStream())
                require(input.readInt() == -0x35014542) { "not a class file" }
                input.readUnsignedShort()
                input.readUnsignedShort()
                val poolCount = input.readUnsignedShort()
                val pool = arrayOfNulls<Any?>(poolCount)
                val classRefs = IntArray(poolCount)
                var slot = 1
                while (slot < poolCount) {
                    when (val tag = input.readUnsignedByte()) {
                        1 -> {
                            pool[slot] = input.readUTF()
                        }

                        7 -> {
                            classRefs[slot] = input.readUnsignedShort()
                        }

                        8, 16, 19, 20 -> {
                            input.skipBytes(2)
                        }

                        15 -> {
                            input.skipBytes(3)
                        }

                        3, 4, 9, 10, 11, 12, 17, 18 -> {
                            input.skipBytes(4)
                        }

                        5, 6 -> {
                            input.skipBytes(8)
                            slot += 1
                        }

                        else -> {
                            error("unsupported constant pool tag $tag")
                        }
                    }
                    slot += 1
                }
                val access = input.readUnsignedShort()
                val thisClass = input.readUnsignedShort()
                val className = utf8At(pool, classRefs[thisClass])
                if ((access and 0x0001) == 0 || (access and 0x1000) != 0) {
                    return emptyList()
                }
                val superName =
                    input.readUnsignedShort().let { index ->
                        if (index == 0) "-" else utf8At(pool, classRefs[index])
                    }
                val interfaces =
                    (0 until input.readUnsignedShort())
                        .map { utf8At(pool, classRefs[input.readUnsignedShort()]) }
                        .sorted()
                val hierarchy =
                    buildString {
                        append("class $className extends $superName")
                        if (interfaces.isNotEmpty()) {
                            append(" implements ${interfaces.joinToString(",")}")
                        }
                    }
                val surface = mutableListOf(hierarchy)
                for (section in listOf("field", "method")) {
                    repeat(input.readUnsignedShort()) {
                        val memberAccess = input.readUnsignedShort()
                        val name = utf8At(pool, input.readUnsignedShort())
                        val descriptor = utf8At(pool, input.readUnsignedShort())
                        repeat(input.readUnsignedShort()) {
                            input.skipBytes(2)
                            input.skipBytes(input.readInt())
                        }
                        val visible = (memberAccess and 0x0005) != 0
                        val synthetic = (memberAccess and 0x1000) != 0
                        if (visible && !synthetic) {
                            surface += "  $section $className.$name $descriptor"
                        }
                    }
                }
                return surface
            }

            val lines = mutableListOf<String>()
            ZipFile(jarFile.get().asFile).use { archive ->
                for (entry in archive.entries()) {
                    if (entry.name.endsWith(".class") && entry.name.startsWith("com/nouprax/")) {
                        lines += classSurface(archive.getInputStream(entry).use { it.readBytes() })
                    }
                }
            }
            val rendered = lines.sorted().joinToString("\n") + "\n"
            if (write) {
                snapshotFile.writeText(rendered)
                logger.lifecycle("Wrote JVM ABI snapshot: ${snapshotFile.absolutePath}")
                return@doLast
            }
            check(snapshotFile.isFile) {
                "jvm-abi.txt is missing; generate it with " +
                    "./gradlew :packages:kotlin-tex-core:verifyJvmAbi -PwriteJvmAbi"
            }
            val expected = snapshotFile.readText()
            check(rendered == expected) {
                val actualLines = rendered.lines().toSet()
                val expectedLines = expected.lines().toSet()
                val added = (actualLines - expectedLines).sorted().joinToString("\n")
                val removed = (expectedLines - actualLines).sorted().joinToString("\n")
                "JVM public ABI drifted from jvm-abi.txt.\nAdded:\n$added\nRemoved:\n$removed\n" +
                    "If the change is intentional, regenerate with -PwriteJvmAbi."
            }
        }
    }

tasks.register("kotlinTest") {
    group = "verification"
    description = "Runs JVM, Android host, and the current host's Kotlin/Native correctness suites."
    dependsOn(
        "jvmTest",
        "testAndroidHostTest",
        hostNativeTest,
        "verifyKotlinNativePackaging",
        verifyJvmAbi,
    )
}

tasks.register("allKotlinTests") {
    group = "verification"
    description =
        "Runs all Kotlin correctness and conformance suites supported by this host, " +
        "including both Android managed devices."
    dependsOn(
        listOfNotNull(
            "jvmTest",
            "jvmConformanceTest",
            "testAndroidHostTest",
            "androidHostConformanceTest",
            hostNativeTest,
            hostNativeConformanceTest,
            "texCoreApi36Page4kAndroidDeviceTest",
            "texCoreApi36Page16kAndroidDeviceTest",
            verifyJvmAbi,
        ),
    )
}

tasks.register("verifyKotlinNativePackaging") {
    group = "verification"
    description = "Verifies the current desktop JNI payload and all supported Android ABIs."
    dependsOn("jvmJar", ":packages:kotlin-tex-core:android-runtime:assembleRelease")
    // Keep artifact locations as providers. Resolving them at configuration
    // time guesses plugin-owned outputs too early and makes the task harder
    // to reuse safely from the configuration cache.
    val runtimeAarFile = androidRuntimeAar
    val expectedDesktopPlatform = desktopPlatform
    val expectedDesktopArchitecture =
        if (hostTriple.os == "macos") "macho-${hostTriple.architecture}" else "elf-${hostTriple.architecture}"
    val desktopLibraryName = hostTriple.nativeLibraryFileName
    val desktopLibraryFile =
        nativeOutputDirectory.map { directory ->
            directory.file(desktopLibraryName)
        }
    inputs.file(runtimeAarFile)
    inputs.file(desktopLibraryFile)

    doLast {
        // Read enough of each ELF or Mach-O header to verify the actual
        // machine architecture, not merely an ABI-shaped filename.
        fun binaryArchitecture(bytes: ByteArray): String {
            require(bytes.size >= 20) { "native library header is truncated" }

            fun u16(offset: Int) = (bytes[offset].toInt() and 0xff) or ((bytes[offset + 1].toInt() and 0xff) shl 8)

            fun u32(offset: Int) =
                (bytes[offset].toInt() and 0xff) or ((bytes[offset + 1].toInt() and 0xff) shl 8) or
                    ((bytes[offset + 2].toInt() and 0xff) shl 16) or
                    ((bytes[offset + 3].toInt() and 0xff) shl 24)

            return when {
                bytes[0] == 0x7f.toByte() && bytes[1] == 'E'.code.toByte() &&
                    bytes[2] == 'L'.code.toByte() && bytes[3] == 'F'.code.toByte() -> {
                    when (u16(18)) {
                        0x3e -> "elf-x64"
                        0xb7 -> "elf-arm64"
                        0x28 -> "elf-arm32"
                        0x03 -> "elf-x86"
                        else -> "elf-unknown-${u16(18)}"
                    }
                }

                u32(0) == 0xfeedfacf.toInt() -> {
                    when (u32(4)) {
                        0x0100000c -> "macho-arm64"
                        0x01000007 -> "macho-x64"
                        else -> "macho-unknown-${u32(4)}"
                    }
                }

                else -> {
                    "unknown"
                }
            }
        }

        val expectedAndroidArchitectures =
            mapOf(
                "arm64-v8a" to "elf-arm64",
                "armeabi-v7a" to "elf-arm32",
                "x86" to "elf-x86",
                "x86_64" to "elf-x64",
            )
        ZipFile(runtimeAarFile.get().asFile).use { archive ->
            for ((abi, expectedArchitecture) in expectedAndroidArchitectures) {
                val entry =
                    archive.getEntry("jni/$abi/libtex_core_kotlin.so")
                        ?: error("Android runtime AAR is missing jni/$abi/libtex_core_kotlin.so")
                val architecture = archive.getInputStream(entry).use { binaryArchitecture(it.readNBytes(20)) }
                check(architecture == expectedArchitecture) {
                    "Android $abi payload has architecture $architecture, expected $expectedArchitecture"
                }
            }
        }

        val payload = desktopLibraryFile.get().asFile
        check(payload.isFile) {
            "JVM native payload is missing for $expectedDesktopPlatform"
        }
        val desktopArchitecture = binaryArchitecture(payload.inputStream().use { it.readNBytes(20) })
        check(desktopArchitecture == expectedDesktopArchitecture) {
            "JVM native payload for $expectedDesktopPlatform has architecture " +
                "$desktopArchitecture, expected $expectedDesktopArchitecture"
        }
        if (expectedDesktopPlatform.startsWith("macos")) {
            // The artifact's Mach-O minimum OS must not exceed the declared
            // support floor: a toolchain default of the build host's OS
            // would publish a dylib older macOS releases refuse to load.
            val process =
                ProcessBuilder("vtool", "-show-build", payload.absolutePath)
                    .redirectErrorStream(true)
                    .start()
            val output = process.inputStream.readBytes().decodeToString()
            check(process.waitFor() == 0) { "vtool failed for $payload:\n$output" }
            // Numeric comparison on major and minor: 15.1 must fail against
            // the 15.0 floor, so extracting the major alone is not enough.
            val minos =
                Regex("minos\\s+(\\d+)(?:\\.(\\d+))?").find(output)
                    ?: error("vtool reported no minos for $payload")
            val minosMajor = minos.groupValues[1].toInt()
            val minosMinor = minos.groupValues[2].ifEmpty { "0" }.toInt()
            check(minosMajor < 15 || (minosMajor == 15 && minosMinor == 0)) {
                "JVM native payload requires macOS $minosMajor.$minosMinor, " +
                    "above the supported minimum macOS 15.0"
            }
        }
    }
}

tasks.withType<org.gradle.api.publish.maven.tasks.PublishToMavenLocal>().configureEach {
    when {
        name.contains("LinuxX64") && hostTriple.os != "linux" -> enabled = false
        name.contains("MacosArm64") && hostTriple.os != "macos" -> enabled = false
    }
}

tasks.register("publishKotlinToMavenLocal") {
    group = "publishing"
    description = "Publishes KMP metadata and all target artifacts buildable on this host."
    dependsOn(
        "publishKotlinMultiplatformPublicationToMavenLocal",
        "publishJvmPublicationToMavenLocal",
        "publishAndroidPublicationToMavenLocal",
        "publish${hostTriple.kotlinNativeTarget.replaceFirstChar { it.uppercase() }}PublicationToMavenLocal",
        ":packages:kotlin-tex-core:android-runtime:publishToMavenLocal",
    )
}
