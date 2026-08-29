package com.pacon.bletool;

import java.io.IOException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Wire names are independent of translated button labels. */
final class UiModeProtocol {
    static final String[] MODES = {"HOME", "FLUID", "OUO"};
    static final String[] LABELS = {"主界面", "流体", "OuO"};

    static String command(int index) {
        if (index < 0 || index >= MODES.length) throw new IllegalArgumentException("Unknown screen");
        return "SET UI " + MODES[index];
    }

    static void requireQueued(String response, int index) throws IOException {
        if (!("OK UI QUEUED " + MODES[index]).equals(response)) {
            throw new IOException(response != null && response.contains("busy")
                    ? "设备正忙，请结束传输或退出 USB 模式后重试"
                    : "设备未接受切屏指令，请确认已更新配套固件");
        }
    }

    static String field(String response, String key) {
        Matcher m = Pattern.compile("\"" + Pattern.quote(key) + "\"\\s*:\\s*\"([^\"]*)\"")
                .matcher(response == null ? "" : response);
        return m.find() ? m.group(1) : "";
    }

    static boolean completed(String response, int index) {
        return response != null && Pattern.compile("\"ok\"\\s*:\\s*true\\b").matcher(response).find()
                && field(response, "screen").equals(MODES[index])
                && field(response, "state").equals("done");
    }

    static String label(String screen) {
        for (int i = 0; i < MODES.length; i++) if (MODES[i].equals(screen)) return LABELS[i];
        return "设备上的其他界面";
    }
}
