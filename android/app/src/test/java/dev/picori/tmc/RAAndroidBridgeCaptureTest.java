package dev.picori.tmc;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.io.File;
import org.junit.Test;

public final class RAAndroidBridgeCaptureTest {
    @Test
    public void validatesStateAndScopesCaptureDirectory() throws Exception {
        File root = new File(System.getProperty("java.io.tmpdir"), "tmc-ra-capture-contract");

        assertTrue(RAAndroidBridge.isCaptureState("S0"));
        assertTrue(RAAndroidBridge.isCaptureState("S14"));
        assertFalse(RAAndroidBridge.isCaptureState(null));
        assertFalse(RAAndroidBridge.isCaptureState("S15"));
        assertFalse(RAAndroidBridge.isCaptureState("../outside"));

        File captureDirectory = RAAndroidBridge.captureDirectoryFor(root, "S4");
        assertNotNull(captureDirectory);
        assertEquals(new File(root, "S4/native").getCanonicalPath(),
                captureDirectory.getPath());
        assertNull(RAAndroidBridge.captureDirectoryFor(root, "S15"));
        assertNull(RAAndroidBridge.captureDirectoryFor(root, "../outside"));
    }
}
