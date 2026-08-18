package dev.picori.tmc.ra;

import android.content.Context;
import android.os.Build;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public final class RAOutboxStore {
    private static final String ALIAS = "tmc-ra-outbox-v1";
    private static final byte[] AAD = "tmc-ra-outbox-v1".getBytes(StandardCharsets.US_ASCII);
    private static final int VERSION = 1;
    private static final int HEADER_BYTES = 6;
    private static final int IV_BYTES = 12;
    private static final int TAG_BYTES = 16;
    static final int MAX_FILE_BYTES = 512 * 1024;
    static final int MIN_RECORD_BYTES = HEADER_BYTES + IV_BYTES + TAG_BYTES;
    static final int MAX_PAYLOAD_BYTES = MAX_FILE_BYTES - MIN_RECORD_BYTES;
    private final AtomicFile file;

    public RAOutboxStore(Context context) {
        File directory = new File(context.getFilesDir(), "ra");
        directory.mkdirs();
        file = new AtomicFile(new File(directory, "outbox.bin"));
    }

    public boolean available() {
        return Build.VERSION.SDK_INT >= 23;
    }

    public synchronized byte[] load() {
        if (!available()) {
            return null;
        }
        byte[] record = null;
        byte[] iv = null;
        byte[] plain = null;
        try {
            record = readRecord();
            if (!isValidRecord(record)) {
                throw new IOException("Invalid outbox record");
            }
            iv = Arrays.copyOfRange(record, HEADER_BYTES, HEADER_BYTES + IV_BYTES);
            plain = cipher(Cipher.DECRYPT_MODE, iv).doFinal(record, HEADER_BYTES + IV_BYTES,
                    record.length - HEADER_BYTES - IV_BYTES);
            if (!isValidPayloadSize(plain.length)) {
                throw new IOException("Invalid outbox payload");
            }
            byte[] result = plain;
            plain = null;
            return result;
        } catch (FileNotFoundException ignored) {
            // An empty byte array is the stable "no record" sentinel. Null is
            // reserved for an unreadable or unauthenticated record.
            return new byte[0];
        } catch (Exception ignored) {
            deleteQuietly();
            return null;
        } finally {
            wipe(record);
            wipe(iv);
            wipe(plain);
        }
    }

    public synchronized boolean save(byte[] payload) {
        if (!available() || !isValidPayloadSize(payload == null ? -1 : payload.length)) {
            return false;
        }
        byte[] plain = null;
        byte[] encrypted = null;
        byte[] iv = null;
        byte[] record = null;
        FileOutputStream output = null;
        try {
            plain = Arrays.copyOf(payload, payload.length);
            Cipher cipher = cipher(Cipher.ENCRYPT_MODE, null);
            encrypted = cipher.doFinal(plain);
            iv = cipher.getIV();
            if (iv == null || iv.length != IV_BYTES) {
                throw new IOException("Invalid GCM IV");
            }
            int recordLength = HEADER_BYTES + iv.length + encrypted.length;
            if (!isValidRecordSize(recordLength)) {
                throw new IOException("Outbox record too large");
            }
            record = new byte[recordLength];
            record[0] = 'R';
            record[1] = 'A';
            record[2] = 'O';
            record[3] = '1';
            record[4] = (byte) VERSION;
            record[5] = (byte) iv.length;
            System.arraycopy(iv, 0, record, HEADER_BYTES, iv.length);
            System.arraycopy(encrypted, 0, record, HEADER_BYTES + iv.length, encrypted.length);

            output = file.startWrite();
            output.write(record);
            output.flush();
            output.getFD().sync();
            file.finishWrite(output);
            output = null;
            return true;
        } catch (Exception ignored) {
            return false;
        } finally {
            if (output != null) {
                try {
                    file.failWrite(output);
                } catch (Exception ignored) {
                    // Best-effort cleanup of AtomicFile's temporary record.
                }
            }
            wipe(plain);
            wipe(encrypted);
            wipe(iv);
            wipe(record);
        }
    }

    public synchronized void delete() {
        deleteQuietly();
    }

    static boolean isValidPayloadSize(int size) {
        return size >= 0 && size <= MAX_PAYLOAD_BYTES;
    }

    static boolean isMissingPayload(byte[] payload) {
        return payload != null && payload.length == 0;
    }

    static boolean isValidRecordSize(int size) {
        return size >= MIN_RECORD_BYTES && size <= MAX_FILE_BYTES;
    }

    static boolean isValidRecord(byte[] record) {
        return record != null && isValidRecordSize(record.length)
                && record[0] == 'R' && record[1] == 'A' && record[2] == 'O' && record[3] == '1'
                && (record[4] & 0xff) == VERSION && (record[5] & 0xff) == IV_BYTES;
    }

    private byte[] readRecord() throws IOException {
        byte[] record = null;
        boolean complete = false;
        try (FileInputStream input = file.openRead()) {
            long size = input.getChannel().size();
            if (size < MIN_RECORD_BYTES || size > MAX_FILE_BYTES || size > Integer.MAX_VALUE) {
                throw new IOException("Invalid outbox size");
            }
            record = new byte[(int) size];
            int offset = 0;
            while (offset < record.length) {
                int count = input.read(record, offset, record.length - offset);
                if (count <= 0) {
                    throw new IOException("Truncated outbox record");
                }
                offset += count;
            }
            if (input.read() != -1) {
                throw new IOException("Growing outbox record");
            }
            complete = true;
            return record;
        } finally {
            if (!complete) {
                wipe(record);
            }
        }
    }

    private void deleteQuietly() {
        try {
            file.delete();
        } catch (Exception ignored) {
            // A failed delete still fails closed because callers receive no payload.
        }
    }

    private static Cipher cipher(int mode, byte[] iv) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        if (mode == Cipher.ENCRYPT_MODE) {
            cipher.init(mode, key());
        } else {
            cipher.init(mode, key(), new GCMParameterSpec(TAG_BYTES * 8, iv));
        }
        cipher.updateAAD(AAD);
        return cipher;
    }

    private static SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        SecretKey existing = (SecretKey) store.getKey(ALIAS, null);
        if (existing != null) {
            return existing;
        }
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return generator.generateKey();
    }

    private static void wipe(byte[] value) {
        if (value != null) {
            Arrays.fill(value, (byte) 0);
        }
    }
}
