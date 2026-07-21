pluginManagement {
    repositories {
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        providers.gradleProperty("consumerRepository").orNull?.let { repositoryDirectory ->
            maven { url = uri(repositoryDirectory) }
        } ?: mavenLocal()
        mavenCentral()
    }
}
rootProject.name = "kotlin-tex-core-kmp-consumer"
