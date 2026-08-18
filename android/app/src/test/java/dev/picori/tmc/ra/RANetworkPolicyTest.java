package dev.picori.tmc.ra;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import java.net.MalformedURLException;
import java.net.URL;
import org.junit.Test;

public final class RANetworkPolicyTest {
    @Test
    public void allowsOnlyRetroAchievementsHttps() throws Exception {
        URL url = RANetworkPolicy.allowHttps("https://retroachievements.org/dorequest.php");
        assertEquals("retroachievements.org", url.getHost());
        assertEquals("https", url.getProtocol());

        URL badge = RANetworkPolicy.allowHttps(
                "https://media.retroachievements.org/Badge/12345.png");
        assertEquals("media.retroachievements.org", badge.getHost());
    }

    @Test
    public void rejectsCleartextRedirectAndLookalikeHosts() {
        String[] rejected = {
                "http://retroachievements.org/dorequest.php",
                "https://retroachievements.org.evil.example/dorequest.php",
                "https://retroachievements.org:8443/dorequest.php",
                "https://user:password@retroachievements.org/dorequest.php"
        };
        for (String value : rejected) {
            try {
                RANetworkPolicy.allowHttps(value);
                fail("accepted URL: " + value);
            } catch (MalformedURLException expected) {
                // Expected fail-closed policy result.
            }
        }
    }
}
