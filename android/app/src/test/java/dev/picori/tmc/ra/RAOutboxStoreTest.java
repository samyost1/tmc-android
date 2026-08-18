package dev.picori.tmc.ra;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.Arrays;
import org.junit.Test;

public final class RAOutboxStoreTest {
    @Test
    public void enforcesEnvelopeBounds() {
        assertTrue(RAOutboxStore.isValidPayloadSize(0));
        assertTrue(RAOutboxStore.isValidPayloadSize(RAOutboxStore.MAX_PAYLOAD_BYTES));
        assertFalse(RAOutboxStore.isValidPayloadSize(-1));
        assertFalse(RAOutboxStore.isValidPayloadSize(RAOutboxStore.MAX_PAYLOAD_BYTES + 1));
        assertTrue(RAOutboxStore.isValidRecordSize(RAOutboxStore.MAX_FILE_BYTES));
        assertFalse(RAOutboxStore.isValidRecordSize(RAOutboxStore.MAX_FILE_BYTES + 1));
    }

    @Test
    public void distinguishesMissingFromUnreadablePayload() {
        assertTrue(RAOutboxStore.isMissingPayload(new byte[0]));
        assertFalse(RAOutboxStore.isMissingPayload(null));
        assertFalse(RAOutboxStore.isMissingPayload(new byte[] {1}));
    }

    @Test
    public void rejectsMalformedEnvelope() {
        byte[] record = new byte[RAOutboxStore.MIN_RECORD_BYTES];
        record[0] = 'R';
        record[1] = 'A';
        record[2] = 'O';
        record[3] = '1';
        record[4] = 1;
        record[5] = 12;
        assertTrue(RAOutboxStore.isValidRecord(record));

        record[0] = 'X';
        assertFalse(RAOutboxStore.isValidRecord(record));
        record[0] = 'R';
        record[5] = 16;
        assertFalse(RAOutboxStore.isValidRecord(record));
        assertFalse(RAOutboxStore.isValidRecord(Arrays.copyOf(record, RAOutboxStore.MIN_RECORD_BYTES - 1)));
    }
}
