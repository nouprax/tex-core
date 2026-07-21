package com.nouprax.tex.core.ci;

import java.io.PrintWriter;
import org.junit.platform.engine.discovery.DiscoverySelectors;
import org.junit.platform.engine.discovery.ClassNameFilter;
import org.junit.platform.launcher.Launcher;
import org.junit.platform.launcher.core.LauncherDiscoveryRequestBuilder;
import org.junit.platform.launcher.core.LauncherFactory;
import org.junit.platform.launcher.listeners.SummaryGeneratingListener;

public final class KotlinJvmTestLauncher {
    private KotlinJvmTestLauncher() {}

    public static void main(String[] args) {
        if (args.length != 1 || !(args[0].equals("correctness") || args[0].equals("conformance"))) {
            System.err.println("usage: KotlinJvmTestLauncher correctness|conformance");
            System.exit(2);
        }

        var request = LauncherDiscoveryRequestBuilder.request()
            .selectors(DiscoverySelectors.selectPackage("com.nouprax.tex.core"));
        if (args[0].equals("conformance")) {
            request.filters(ClassNameFilter.includeClassNamePatterns(".*ConformanceTest"));
        } else {
            request.filters(ClassNameFilter.excludeClassNamePatterns(".*ConformanceTest"));
        }

        var listener = new SummaryGeneratingListener();
        Launcher launcher = LauncherFactory.create();
        launcher.registerTestExecutionListeners(listener);
        launcher.execute(request.build());
        var summary = listener.getSummary();
        summary.printTo(new PrintWriter(System.out));
        if (summary.getTestsFoundCount() == 0 || summary.getTotalFailureCount() != 0) {
            summary.printFailuresTo(new PrintWriter(System.err));
            System.exit(1);
        }
    }
}
