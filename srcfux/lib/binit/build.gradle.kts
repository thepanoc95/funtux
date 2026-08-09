/*
    ref: https://kotlinlang.org/docs/native-get-started.html#create-project-files

 */

import org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget

plugins {
    kotlin("multiplatform") version "2.4.10"
}

repositories {
    mavenCentral()
}

kotlin {
    linuxArm64()
    linuxAmd64()

    targets.withType<KotlinNativeTarget>().configureEach {
        binaries {
            executable()
        }
    }
}

tasks.withType<Wrapper> {
    gradleVersion = "9.5.0"
    distributionType = Wrapper.DistributionType.BIN
}