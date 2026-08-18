package dev.picori.tmc.ra;

import java.net.MalformedURLException;
import java.net.URL;

public final class RANetworkPolicy {
    public static final int MAX_RESPONSE_BYTES = 1024 * 1024;
    public static final int MAX_BADGE_BYTES = 256 * 1024;
    private static final String API_HOST = "retroachievements.org";
    private static final String MEDIA_HOST = "media.retroachievements.org";

    private RANetworkPolicy() {}

    public static URL allowHttps(String value) throws MalformedURLException {
        URL url = new URL(value);
        int port = url.getPort();
        boolean allowedHost = API_HOST.equals(url.getHost()) || MEDIA_HOST.equals(url.getHost());
        if (!"https".equals(url.getProtocol()) || !allowedHost
                || url.getUserInfo() != null || (port != -1 && port != 443)) {
            throw new MalformedURLException("RetroAchievements URL rejected");
        }
        return url;
    }
}
