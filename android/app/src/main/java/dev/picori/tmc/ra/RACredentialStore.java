package dev.picori.tmc.ra;

import android.content.Context;
import android.os.Build;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.security.KeyStore;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public final class RACredentialStore {
    private static final String ALIAS = "tmc-ra-credentials-v1";
    private static final byte[] AAD = "tmc-ra-credentials-v1".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    private static final int MAX_RECORD_BYTES = 2048;
    private final AtomicFile file;

    public static final class Credentials {
        private final byte[] username;
        private final byte[] token;

        public Credentials(byte[] username, byte[] token) {
            this.username = username;
            this.token = token;
        }

        public byte[] username() {
            return username;
        }

        public byte[] token() {
            return token;
        }

        public void wipe() {
            Arrays.fill(username, (byte) 0);
            Arrays.fill(token, (byte) 0);
        }
    }

    public RACredentialStore(Context context) {
        File directory = new File(context.getFilesDir(), "ra");
        directory.mkdirs();
        file = new AtomicFile(new File(directory, "credentials.bin"));
    }

    public boolean available() {
        return Build.VERSION.SDK_INT >= 23;
    }

    public synchronized Credentials load() {
        if (!available() || !file.getBaseFile().exists()) return null;
        byte[] encrypted = null;
        byte[] plain = null;
        byte[] iv = null;
        byte[] payload = null;
        byte[] user = null;
        byte[] token = null;
        try {
            if (file.getBaseFile().length() > MAX_RECORD_BYTES) throw new Exception();
            encrypted = file.readFully();
            if (encrypted.length > MAX_RECORD_BYTES) throw new Exception();
            try (DataInputStream input = new DataInputStream(new ByteArrayInputStream(encrypted))) {
                if (input.readInt() != 0x52414331 || input.readUnsignedByte() != 1) throw new Exception();
                int ivLength = input.readUnsignedByte();
                if (ivLength != 12 || input.available() <= ivLength) throw new Exception();
                iv = new byte[ivLength];
                input.readFully(iv);
                int payloadLength = input.available();
                if (payloadLength <= 0 || payloadLength > MAX_RECORD_BYTES) throw new Exception();
                payload = new byte[payloadLength];
                input.readFully(payload);
            }
            plain = cipher(Cipher.DECRYPT_MODE, iv).doFinal(payload);
            try (DataInputStream values = new DataInputStream(new ByteArrayInputStream(plain))) {
                int userLength = values.readUnsignedShort();
                if (userLength == 0 || userLength > 256 || values.available() < userLength + 2) throw new Exception();
                user = new byte[userLength];
                values.readFully(user);
                int tokenLength = values.readUnsignedShort();
                if (tokenLength == 0 || tokenLength > 1024 || values.available() != tokenLength) throw new Exception();
                token = new byte[tokenLength];
                values.readFully(token);
                Credentials credentials = new Credentials(user, token);
                user = null;
                token = null;
                return credentials;
            }
        } catch (Exception ignored) {
            delete();
            return null;
        } finally {
            if (encrypted != null) Arrays.fill(encrypted, (byte) 0);
            if (plain != null) Arrays.fill(plain, (byte) 0);
            if (iv != null) Arrays.fill(iv, (byte) 0);
            if (payload != null) Arrays.fill(payload, (byte) 0);
            if (user != null) Arrays.fill(user, (byte) 0);
            if (token != null) Arrays.fill(token, (byte) 0);
        }
    }

    public synchronized boolean save(byte[] username, byte[] token) {
        byte[] plain = null;
        byte[] encrypted = null;
        byte[] iv = null;
        FileOutputStream output = null;
        try {
            if (!available() || username == null || token == null || username.length == 0 || username.length > 256
                    || token.length == 0 || token.length > 1024) return false;
            plain = new byte[4 + username.length + token.length];
            plain[0] = (byte) (username.length >>> 8);
            plain[1] = (byte) username.length;
            System.arraycopy(username, 0, plain, 2, username.length);
            int tokenOffset = 2 + username.length;
            plain[tokenOffset] = (byte) (token.length >>> 8);
            plain[tokenOffset + 1] = (byte) token.length;
            System.arraycopy(token, 0, plain, tokenOffset + 2, token.length);
            Cipher cipher = cipher(Cipher.ENCRYPT_MODE, null);
            encrypted = cipher.doFinal(plain);
            iv = cipher.getIV();
            output = file.startWrite();
            try {
                DataOutputStream record = new DataOutputStream(output);
                record.writeInt(0x52414331);
                record.writeByte(1);
                record.writeByte(iv.length);
                record.write(iv);
                record.write(encrypted);
                record.flush();
                file.finishWrite(output);
                output = null;
            } catch (Exception e) {
                if (output != null) file.failWrite(output);
                throw e;
            }
            return true;
        } catch (Exception ignored) {
            return false;
        } finally {
            if (username != null) Arrays.fill(username, (byte) 0);
            if (token != null) Arrays.fill(token, (byte) 0);
            if (plain != null) Arrays.fill(plain, (byte) 0);
            if (encrypted != null) Arrays.fill(encrypted, (byte) 0);
            if (iv != null) Arrays.fill(iv, (byte) 0);
        }
    }

    public synchronized boolean savePart(String key, byte[] value) {
        if (key == null || value == null) return false;
        if ("native_ra.username".equals(key)) {
            if (value.length == 0 || value.length > 256) return false;
            if (pendingUsername != null) Arrays.fill(pendingUsername, (byte) 0);
            pendingUsername = Arrays.copyOf(value, value.length);
            return true;
        }
        if (!"native_ra.token".equals(key) || pendingUsername == null
                || value.length == 0 || value.length > 1024) return false;
        byte[] user = pendingUsername;
        pendingUsername = null;
        try {
            return save(user, value);
        } finally {
            Arrays.fill(user, (byte) 0);
        }
    }

    public synchronized void delete() {
        if (pendingUsername != null) {
            Arrays.fill(pendingUsername, (byte) 0);
            pendingUsername = null;
        }
        file.delete();
    }

    private byte[] pendingUsername;

    private static Cipher cipher(int mode, byte[] iv) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        if (mode == Cipher.ENCRYPT_MODE) cipher.init(mode, key());
        else cipher.init(mode, key(), new GCMParameterSpec(128, iv));
        cipher.updateAAD(AAD);
        return cipher;
    }

    private static SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        SecretKey existing = (SecretKey) store.getKey(ALIAS, null);
        if (existing != null) return existing;
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256).build());
        return generator.generateKey();
    }
}
