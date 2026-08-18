package dev.picori.tmc;

import android.app.AlertDialog;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Build;
import android.text.Editable;
import android.text.InputType;
import android.widget.EditText;
import dev.picori.tmc.ra.RANetworkPolicy;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.charset.CharsetEncoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.CoderResult;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.ThreadPoolExecutor;
import javax.net.ssl.HttpsURLConnection;
import dev.picori.tmc.ra.RACredentialStore.Credentials;

public final class RAAndroidBridge {
    private final TMCActivity activity;
    private final ThreadPoolExecutor executor = new ThreadPoolExecutor(
            2, 2, 0L, TimeUnit.MILLISECONDS, new ArrayBlockingQueue<>(14),
            new ThreadPoolExecutor.AbortPolicy());
    private final Set<HttpsURLConnection> connections = Collections.synchronizedSet(new HashSet<>());
    private final dev.picori.tmc.ra.RACredentialStore credentials;
    private final dev.picori.tmc.ra.RAOutboxStore outbox;
    private final Set<String> badgeInFlight = Collections.synchronizedSet(new HashSet<>());
    private final Set<String> badgeLoaded = Collections.synchronizedSet(new HashSet<>());
    private volatile boolean accepting = true;
    private AlertDialog passwordDialog;

    private static void wipe(byte[] value) {
        if (value != null) {
            Arrays.fill(value, (byte) 0);
        }
    }

    private static final class RequestTask implements Runnable {
        private final RAAndroidBridge bridge;
        private final long generation;
        private final long requestId;
        private final String url;
        private final String contentType;
        private final String userAgent;
        private final int connectTimeoutMs;
        private final int readTimeoutMs;
        private final int maxResponseBytes;
        private byte[] postData;

        RequestTask(RAAndroidBridge bridge, long generation, long requestId, String url, byte[] postData,
                String contentType, String userAgent, int connectTimeoutMs, int readTimeoutMs,
                int maxResponseBytes) {
            this.bridge = bridge;
            this.generation = generation;
            this.requestId = requestId;
            this.url = url;
            this.postData = postData;
            this.contentType = contentType;
            this.userAgent = userAgent;
            this.connectTimeoutMs = connectTimeoutMs;
            this.readTimeoutMs = readTimeoutMs;
            this.maxResponseBytes = maxResponseBytes;
        }

        @Override
        public void run() {
            try {
                bridge.runRequest(generation, requestId, url, postData, contentType, userAgent,
                        connectTimeoutMs, readTimeoutMs, maxResponseBytes);
            } finally {
                wipe();
            }
        }

        void wipe() {
            RAAndroidBridge.wipe(postData);
            postData = null;
        }
    }

    private RAAndroidBridge(TMCActivity activity) {
        this.activity = activity;
        credentials = new dev.picori.tmc.ra.RACredentialStore(activity);
        outbox = new dev.picori.tmc.ra.RAOutboxStore(activity);
    }

    public static RAAndroidBridge attach(TMCActivity activity) {
        if (Build.VERSION.SDK_INT < 23) {
            return null;
        }
        RAAndroidBridge bridge = new RAAndroidBridge(activity);
        if (!nativeAttach(bridge)) return null;
        Credentials restored = bridge.credentials.load();
        if (restored != null) {
            try {
                nativeToken(nativeGeneration(), restored.username(), restored.token());
            } finally {
                restored.wipe();
            }
        } else {
            bridge.requestPasswordLogin(nativeGeneration());
        }
        return bridge;
    }

    static boolean isCaptureState(String state) {
        if (state == null) {
            return false;
        }
        switch (state) {
            case "S0": case "S1": case "S2": case "S3": case "S4":
            case "S5": case "S6": case "S7": case "S8": case "S9":
            case "S10": case "S11": case "S12": case "S13": case "S14":
                return true;
            default:
                return false;
        }
    }

    static File captureDirectoryFor(File root, String state) throws IOException {
        if (root == null || !isCaptureState(state)) {
            return null;
        }
        File canonicalRoot = root.getCanonicalFile();
        File captureDirectory = new File(new File(canonicalRoot, state), "native").getCanonicalFile();
        String rootPrefix = canonicalRoot.getPath() + File.separator;
        return captureDirectory.getPath().startsWith(rootPrefix) ? captureDirectory : null;
    }

    boolean requestCapture(String state) {
        if (!accepting || !isCaptureState(state)) {
            return false;
        }
        File root = activity.getExternalFilesDir("ra-captures");
        if (root == null) {
            return false;
        }
        try {
            File captureDirectory = captureDirectoryFor(root, state);
            if (captureDirectory == null ||
                    (!captureDirectory.exists() && !captureDirectory.mkdirs()) ||
                    !captureDirectory.isDirectory()) {
                return false;
            }
            return nativeRequestCapture(state, captureDirectory.getPath());
        } catch (IOException | SecurityException ignored) {
            return false;
        }
    }

    void beginRequest(long generation, long requestId, String url, byte[] postData, String contentType,
            String userAgent, int connectTimeoutMs, int readTimeoutMs, int maxResponseBytes) {
        if (postData == null) {
            postData = new byte[0];
        }
        if (!accepting) {
            wipe(postData);
            return;
        }
        if (maxResponseBytes < 0 || maxResponseBytes > RANetworkPolicy.MAX_RESPONSE_BYTES) {
            try {
                nativeHttpCompleted(generation, requestId, 0, new byte[0], true);
            } finally {
                wipe(postData);
            }
            return;
        }
        RequestTask task = new RequestTask(this, generation, requestId, url, postData, contentType, userAgent,
                connectTimeoutMs, readTimeoutMs, maxResponseBytes);
        try {
            executor.execute(task);
        } catch (RejectedExecutionException ignored) {
            task.wipe();
            nativeHttpCompleted(generation, requestId, 0, new byte[0], true);
        }
    }

    void requestPasswordLogin(long generation) {
        if (!accepting || activity.isFinishing()) {
            return;
        }
        activity.runOnUiThread(() -> showPasswordDialog(generation));
    }

    void syncBadgeImages(long generation, String[] urls) {
        if (!accepting || urls == null) {
            return;
        }
        for (String url : urls) {
            if (url == null || url.length() == 0 || url.length() >= 512 ||
                    badgeLoaded.contains(url) || !badgeInFlight.add(url)) {
                continue;
            }
            try {
                executor.execute(() -> runBadgeRequest(generation, url));
            } catch (RejectedExecutionException ignored) {
                badgeInFlight.remove(url);
            }
        }
    }

    void shutdownFromNative() {
        accepting = false;
        synchronized (connections) {
            for (HttpsURLConnection connection : connections) {
                connection.disconnect();
            }
        }
        for (Runnable pending : executor.shutdownNow()) {
            if (pending instanceof RequestTask) {
                ((RequestTask) pending).wipe();
            }
        }
        badgeInFlight.clear();
        badgeLoaded.clear();
        try {
            executor.awaitTermination(2, TimeUnit.SECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    void detach() {
        nativeDetach();
    }

    boolean saveCredentialPart(String key, byte[] value) {
        try {
            return credentials.savePart(key, value);
        } finally {
            wipe(value);
        }
    }

    void deleteCredentials(String key) {
        credentials.delete();
    }

    byte[] loadSecureBlob(String key) {
        return "native_ra.outbox".equals(key) ? outbox.load() : null;
    }

    boolean saveSecureBlob(String key, byte[] value) {
        return "native_ra.outbox".equals(key) && outbox.save(value);
    }

    boolean deleteSecureBlob(String key) {
        if (!"native_ra.outbox".equals(key)) {
            return false;
        }
        outbox.delete();
        return true;
    }

    private void showPasswordDialog(long generation) {
        if (!accepting || activity.isFinishing()) {
            return;
        }
        if (passwordDialog != null && passwordDialog.isShowing()) {
            return;
        }
        EditText username = new EditText(activity);
        username.setInputType(InputType.TYPE_CLASS_TEXT);
        username.setHint("Username");
        EditText password = new EditText(activity);
        password.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        password.setHint("Password");
        android.widget.LinearLayout form = new android.widget.LinearLayout(activity);
        form.setOrientation(android.widget.LinearLayout.VERTICAL);
        form.addView(username);
        form.addView(password);
        AlertDialog dialog = new AlertDialog.Builder(activity)
                .setTitle("RetroAchievements Casual")
                .setView(form)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(android.R.string.ok, null)
                .create();
        passwordDialog = dialog;
        dialog.setOnDismissListener(ignored -> {
            username.getText().clear();
            password.getText().clear();
            if (passwordDialog == dialog) {
                passwordDialog = null;
            }
        });
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            byte[] userBytes = encodeUtf8(username.getText(), 256);
            byte[] passwordBytes = encodeUtf8(password.getText(), 256);
            try {
                if (userBytes != null && passwordBytes != null) {
                    nativePassword(generation, userBytes, passwordBytes);
                    dialog.dismiss();
                }
            } finally {
                wipe(userBytes);
                wipe(passwordBytes);
                username.getText().clear();
                password.getText().clear();
            }
        }));
        dialog.show();
    }

    private void runRequest(long generation, long requestId, String value, byte[] postData, String contentType,
            String userAgent, int connectTimeoutMs, int readTimeoutMs, int maxResponseBytes) {
        HttpsURLConnection connection = null;
        byte[] response = new byte[0];
        int status = 0;
        boolean retryable = true;
        try {
            URL url = RANetworkPolicy.allowHttps(value);
            connection = (HttpsURLConnection) url.openConnection();
            synchronized (connections) {
                if (!accepting) {
                    connection.disconnect();
                    return;
                }
                connections.add(connection);
            }
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(Math.min(Math.max(connectTimeoutMs, 1), 10_000));
            connection.setReadTimeout(Math.min(Math.max(readTimeoutMs, 1), 30_000));
            connection.setRequestProperty("User-Agent", userAgent);
            if (postData.length != 0) {
                connection.setRequestMethod("POST");
                connection.setDoOutput(true);
                connection.setRequestProperty("Content-Type", contentType);
                try (OutputStream output = connection.getOutputStream()) {
                    output.write(postData);
                }
            }
            status = connection.getResponseCode();
            InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
            response = readBounded(stream, Math.min(maxResponseBytes, RANetworkPolicy.MAX_RESPONSE_BYTES));
            retryable = status >= 500;
        } catch (Exception ignored) {
            retryable = true;
        } finally {
            if (connection != null) {
                connections.remove(connection);
                connection.disconnect();
            }
        }
        try {
            if (accepting) {
                nativeHttpCompleted(generation, requestId, status, response, retryable);
            }
        } finally {
            Arrays.fill(response, (byte) 0);
        }
    }

    private void runBadgeRequest(long generation, String value) {
        HttpsURLConnection connection = null;
        byte[] response = new byte[0];
        try {
            URL url = RANetworkPolicy.allowHttps(value);
            if (!"media.retroachievements.org".equals(url.getHost())) {
                throw new MalformedURLException("RetroAchievements badge host rejected");
            }
            connection = (HttpsURLConnection) url.openConnection();
            synchronized (connections) {
                if (!accepting) {
                    connection.disconnect();
                    return;
                }
                connections.add(connection);
            }
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(10_000);
            connection.setReadTimeout(30_000);
            connection.setRequestMethod("GET");
            connection.setRequestProperty("User-Agent", "SkyEmu/4.0");
            int status = connection.getResponseCode();
            if (status != 200) {
                return;
            }
            response = readBounded(connection.getInputStream(), RANetworkPolicy.MAX_BADGE_BYTES);
            if (response.length == 0) {
                return;
            }
            int[] dimensions = new int[2];
            int[] pixels = decodeBadge(response, dimensions);
            if (pixels != null && accepting &&
                    nativeBadgeImage(generation, value, dimensions[0], dimensions[1], pixels)) {
                badgeLoaded.add(value);
                java.util.Arrays.fill(pixels, 0);
            } else if (pixels != null) {
                java.util.Arrays.fill(pixels, 0);
            }
        } catch (Exception ignored) {
            // A missing badge is presentation-only; RA gameplay must continue.
        } finally {
            badgeInFlight.remove(value);
            if (connection != null) {
                connections.remove(connection);
                connection.disconnect();
            }
            java.util.Arrays.fill(response, (byte) 0);
        }
    }

    private static byte[] encodeUtf8(Editable value, int maxBytes) {
        if (value == null || maxBytes <= 0) {
            return null;
        }
        int length = value.length();
        if (length == 0 || length > maxBytes) {
            return null;
        }
        char[] chars = new char[length];
        byte[] encoded = new byte[maxBytes];
        try {
            value.getChars(0, length, chars, 0);
            CharsetEncoder encoder = StandardCharsets.UTF_8.newEncoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT);
            ByteBuffer output = ByteBuffer.wrap(encoded);
            CoderResult result = encoder.encode(CharBuffer.wrap(chars), output, true);
            if (!result.isUnderflow()) {
                return null;
            }
            result = encoder.flush(output);
            if (!result.isUnderflow() || output.position() == 0) {
                return null;
            }
            return Arrays.copyOf(encoded, output.position());
        } finally {
            Arrays.fill(chars, '\0');
            Arrays.fill(encoded, (byte) 0);
        }
    }

    private static int[] decodeBadge(byte[] encoded, int[] dimensions) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(encoded, 0, encoded.length, bounds);
        if (bounds.outWidth < 1 || bounds.outHeight < 1 ||
                bounds.outWidth > 256 || bounds.outHeight > 256) {
            return null;
        }
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        int sample = 1;
        while ((bounds.outWidth + sample - 1) / sample > 64 ||
                (bounds.outHeight + sample - 1) / sample > 64) {
            sample <<= 1;
        }
        options.inSampleSize = sample;
        Bitmap bitmap = BitmapFactory.decodeByteArray(encoded, 0, encoded.length, options);
        if (bitmap == null) {
            return null;
        }
        if (bitmap.getWidth() > 64 || bitmap.getHeight() > 64) {
            int width = Math.max(1, Math.min(64, bitmap.getWidth()));
            int height = Math.max(1, Math.min(64, bitmap.getHeight()));
            Bitmap scaled = Bitmap.createScaledBitmap(bitmap, width, height, false);
            if (scaled != bitmap) {
                bitmap.recycle();
                bitmap = scaled;
            }
        }
        int width = bitmap.getWidth();
        int height = bitmap.getHeight();
        int[] pixels = new int[width * height];
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height);
        bitmap.recycle();
        dimensions[0] = width;
        dimensions[1] = height;
        return pixels;
    }

    private static byte[] readBounded(InputStream stream, int limit) throws Exception {
        if (stream == null) {
            return new byte[0];
        }
        byte[] buffer = new byte[8192];
        byte[] result = new byte[limit];
        int size = 0;
        try (InputStream input = stream) {
            if (limit == 0) {
                if (input.read() != -1) {
                    throw new java.io.IOException("response too large");
                }
                return new byte[0];
            }
            for (int count; (count = input.read(buffer)) != -1;) {
                if (size + count > limit) {
                    throw new java.io.IOException("response too large");
                }
                System.arraycopy(buffer, 0, result, size, count);
                size += count;
            }
            return Arrays.copyOf(result, size);
        } finally {
            Arrays.fill(buffer, (byte) 0);
            Arrays.fill(result, (byte) 0);
        }
    }

    private static native boolean nativeAttach(RAAndroidBridge bridge);
    private static native void nativeDetach();
    private static native boolean nativeRequestCapture(String checkpoint, String outputPath);
    private static native void nativeHttpCompleted(long generation, long requestId, int status, byte[] body,
            boolean retryable);
    private static native void nativePassword(long generation, byte[] username, byte[] password);
    private static native void nativeToken(long generation, byte[] username, byte[] token);
    private static native boolean nativeBadgeImage(long generation, String url, int width, int height,
            int[] pixels);
    private static native long nativeGeneration();
}
