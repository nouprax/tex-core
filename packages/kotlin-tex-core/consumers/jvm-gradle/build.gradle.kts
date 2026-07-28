plugins {
    kotlin("jvm") version "2.4.0"
    application
}

// The library promises JVM 17: this consumer compiles and *runs* on a 17
// toolchain, so bytecode or API above the declared minimum fails here
// instead of on a consumer's machine.
kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation("com.nouprax:kotlin-tex-core-jvm:0.1.0")
}

application {
    mainClass.set("consumer.MainKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
