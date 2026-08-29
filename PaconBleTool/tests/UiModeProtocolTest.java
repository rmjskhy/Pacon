package com.pacon.bletool;

import java.io.IOException;

public final class UiModeProtocolTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        for (int i = 0; i < 3; i++) {
            String name = UiModeProtocol.MODES[i];
            check(UiModeProtocol.command(i).equals("SET UI " + name), "command mapping");
            UiModeProtocol.requireQueued("OK UI QUEUED " + name, i);
            check(!UiModeProtocol.completed("OK UI QUEUED " + name, i), "queued is not completed");
            check(!UiModeProtocol.completed("{\"ok\":true,\"screen\":\"" + name + "\",\"state\":\"pending\"}", i), "pending is not completed");
            check(!UiModeProtocol.completed("{\"ok\":false,\"screen\":\"" + name + "\",\"state\":\"done\"}", i), "failed is not completed");
            check(!UiModeProtocol.completed("{\"ok\":true,\"screen\":\"OTHER\",\"state\":\"done\"}", i), "other screen is not completed");
            check(UiModeProtocol.completed("{\"ok\":true,\"screen\":\"" + name + "\",\"state\":\"done\"}", i), "confirmed screen");
            check(!UiModeProtocol.completed(null, i), "null response");
        }
        for (String response : new String[]{"ERR UI busy", "ERR unknown command", "OK UI QUEUED OUO", "", null}) {
            try {
                UiModeProtocol.requireQueued(response, 0);
                throw new AssertionError("unexpected acknowledgement accepted: " + response);
            } catch (IOException expected) { }
        }
        try {
            UiModeProtocol.command(3);
            throw new AssertionError("invalid screen accepted");
        } catch (IllegalArgumentException expected) { }
        System.out.println("Remote screen command / pending / success / failure tests passed.");
    }
}
