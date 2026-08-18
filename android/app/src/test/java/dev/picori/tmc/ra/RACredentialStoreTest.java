package dev.picori.tmc.ra;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertSame;

import org.junit.Test;

public final class RACredentialStoreTest {
    @Test
    public void credentialsExposeAndWipeOwnedByteArrays() {
        byte[] username = {1, 2, 3};
        byte[] token = {4, 5, 6};
        RACredentialStore.Credentials credentials = new RACredentialStore.Credentials(username, token);

        assertSame(username, credentials.username());
        assertSame(token, credentials.token());
        credentials.wipe();

        assertArrayEquals(new byte[username.length], username);
        assertArrayEquals(new byte[token.length], token);
    }
}
