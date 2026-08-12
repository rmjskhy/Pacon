package com.pacon.bletool;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.text.InputType;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** PACON BLE control client with image conversion and windowed RGB565 upload. */
public class MainActivity extends Activity {
    private static final int REQUEST_BLE_PERMISSIONS = 10;
    private static final int REQUEST_MEDIA_FILE = 20;
    private static final String DEVICE_NAME = "PACON-BLE-TEST";
    private static final int LCD_WIDTH = 475;
    private static final int LCD_HEIGHT = 466;
    private static final int LCD_FRAME_BYTES = LCD_WIDTH * LCD_HEIGHT * 2;
    private static final int MAX_MEDIA_HEX_CHARS = 216; // legacy compatibility path
    private static final int MAX_MEDIA_FPS = 12;
    private static final int MEDIA_BINARY_HEADER_BYTES = 4;
    private static final int MEDIA_BINARY_WINDOW_PACKETS = 8;
    private static final int MEDIA_BINARY_ACK_TIMEOUT_MS = 8000;
    /* NAND directory/stat operations can briefly wait behind the display
     * reader, so catalog queries need more time than a small control command. */
    private static final long MEDIA_CATALOG_TIMEOUT_MS = 15000L;
    private static final UUID SERVICE_UUID = UUID.fromString(
            "014e4f43-4150-5091-2a4d-8c5b2143709a");
    private static final UUID COMMAND_UUID = UUID.fromString(
            "034e4f43-4150-5091-2a4d-8c5b2143709a");
    private static final UUID RESPONSE_UUID = UUID.fromString(
            "044e4f43-4150-5091-2a4d-8c5b2143709a");
    private static final UUID MEDIA_DATA_UUID = UUID.fromString(
            "054e4f43-4150-5091-2a4d-8c5b2143709a");
    private static final UUID CCCD_UUID = UUID.fromString(
            "00002902-0000-1000-8000-00805f9b34fb");

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner scanner;
    private BluetoothDevice selectedDevice;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic commandCharacteristic;
    private BluetoothGattCharacteristic responseCharacteristic;
    private BluetoothGattCharacteristic mediaDataCharacteristic;
    private boolean scanning;
    private int negotiatedMtu = 23;
    private volatile boolean mediaUploading;
    private volatile boolean mediaCancelRequested;
    private final ExecutorService mediaExecutor = Executors.newSingleThreadExecutor();
    private final Object responseLock = new Object();
    /* Android permits only one acknowledged GATT write at a time.  The
     * firmware may notify before Android delivers onCharacteristicWrite, so
     * response completion alone is not enough to start the next command. */
    private final Object commandWriteLock = new Object();
    private boolean commandWritePending;
    private String pendingResponse;
    private String pendingMediaAck;

    private TextView deviceText;
    private TextView logText;
    private EditText commandEdit;
    private Button uploadButton;
    private Button mediaListButton;
    private Button cancelUploadButton;
    private Button retryUploadButton;
    private ProgressBar uploadProgress;
    private TextView uploadStatus;
    private ListView mediaListView;
    private ArrayAdapter<MediaEntry> mediaAdapter;
    private LinearLayout settingsPanel;
    private SeekBar brightnessSeek;
    private TextView brightnessValue;
    private SeekBar rangeSeek;
    private TextView rangeValue;
    private EditText latitudeEdit;
    private EditText longitudeEdit;
    private EditText wifiSsidEdit;
    private EditText wifiPasswordEdit;
    private TextView settingsStatus;
    private final ArrayList<MediaEntry> mediaEntries = new ArrayList<>();
    private Uri lastMediaUri;
    private volatile String currentMediaName;

    private static final class MediaEntry {
        final String name;
        final long bytes;
        final int frames;
        final int fps;
        boolean current;

        MediaEntry(String name, long bytes, int frames, int fps) {
            this.name = name;
            this.bytes = bytes;
            this.frames = frames;
            this.fps = fps;
            this.current = false;
        }

        String displayText() {
            String size = bytes >= 1024L * 1024L
                    ? String.format(Locale.US, "%.1f MiB", bytes / (1024.0 * 1024.0))
                    : String.format(Locale.US, "%.1f KiB", bytes / 1024.0);
            return (current ? "[PLAYING] " : "") + name + " / " + size + " / "
                    + frames + " frame" + (frames == 1 ? "" : "s") + " / " + fps + " fps";
        }

        @Override
        public String toString() {
            String size = bytes >= 1024L * 1024L
                    ? String.format(Locale.US, "%.1f MiB", bytes / (1024.0 * 1024.0))
                    : String.format(Locale.US, "%.1f KiB", bytes / 1024.0);
            return name + "  ·  " + size + "  ·  " + frames + " frame"
                    + (frames == 1 ? "" : "s") + "  ·  " + fps + " fps";
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        BluetoothManager manager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = manager == null ? null : manager.getAdapter();
        buildUi();
        if (bluetoothAdapter == null) {
            log("Bluetooth LE 不可用");
        } else {
            log("就绪：点击“扫描 PACON”");
        }
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(18), dp(18), dp(20));
        root.setBackgroundColor(Color.rgb(9, 14, 28));

        TextView title = new TextView(this);
        title.setText("PACON BLE Tool");
        title.setTextSize(24);
        title.setTextColor(Color.rgb(245, 247, 255));
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(title, new LinearLayout.LayoutParams(-1, -2));

        deviceText = new TextView(this);
        deviceText.setText("设备：未选择");
        deviceText.setTextSize(16);
        deviceText.setTextColor(Color.rgb(224, 230, 245));
        root.addView(deviceText, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout scanRow = new LinearLayout(this);
        Button scanButton = button("扫描 PACON");
        Button connectButton = button("连接");
        Button disconnectButton = button("断开");
        scanRow.addView(scanButton, weightParams());
        scanRow.addView(connectButton, weightParams());
        scanRow.addView(disconnectButton, weightParams());
        root.addView(scanRow);

        LinearLayout testRow = new LinearLayout(this);
        Button pingButton = button("PING");
        Button statusButton = button("GET STATUS");
        testRow.addView(pingButton, weightParams());
        testRow.addView(statusButton, weightParams());
        root.addView(testRow);

        Button settingsToggle = button("设备设置");
        root.addView(settingsToggle, new LinearLayout.LayoutParams(-1, -2));
        settingsPanel = new LinearLayout(this);
        settingsPanel.setOrientation(LinearLayout.VERTICAL);
        settingsPanel.setPadding(dp(12), dp(8), dp(12), dp(8));
        settingsPanel.setBackground(roundBackground(Color.rgb(18, 24, 42), 12));

        LinearLayout brightnessRow = new LinearLayout(this);
        brightnessRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView brightnessLabel = label("屏幕亮度");
        brightnessRow.addView(brightnessLabel, new LinearLayout.LayoutParams(dp(88), -2));
        brightnessSeek = new SeekBar(this);
        brightnessSeek.setMax(100);
        brightnessSeek.setProgress(50);
        brightnessRow.addView(brightnessSeek, weightParams());
        brightnessValue = label("50%");
        brightnessValue.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        brightnessRow.addView(brightnessValue, new LinearLayout.LayoutParams(dp(48), -2));
        settingsPanel.addView(brightnessRow);

        LinearLayout rangeRow = new LinearLayout(this);
        rangeRow.setGravity(Gravity.CENTER_VERTICAL);
        rangeRow.addView(label("SkyOrb 量程"), new LinearLayout.LayoutParams(dp(88), -2));
        rangeSeek = new SeekBar(this);
        rangeSeek.setMax(3);
        rangeSeek.setProgress(3);
        rangeRow.addView(rangeSeek, weightParams());
        rangeValue = label("3");
        rangeValue.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        rangeRow.addView(rangeValue, new LinearLayout.LayoutParams(dp(48), -2));
        settingsPanel.addView(rangeRow);

        latitudeEdit = settingsEdit("纬度");
        longitudeEdit = settingsEdit("经度");
        settingsPanel.addView(latitudeEdit);
        settingsPanel.addView(longitudeEdit);

        wifiSsidEdit = settingsEdit("Wi-Fi 名称");
        wifiPasswordEdit = settingsEdit("Wi-Fi 密码");
        wifiPasswordEdit.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        settingsPanel.addView(wifiSsidEdit);
        settingsPanel.addView(wifiPasswordEdit);

        LinearLayout settingsButtons = new LinearLayout(this);
        Button refreshSettingsButton = button("读取");
        Button autoLocationButton = button("自动定位");
        Button saveSettingsButton = button("保存");
        settingsButtons.addView(refreshSettingsButton, weightParams());
        settingsButtons.addView(autoLocationButton, weightParams());
        settingsButtons.addView(saveSettingsButton, weightParams());
        settingsPanel.addView(settingsButtons);
        settingsStatus = label("未读取设置");
        settingsStatus.setTextColor(Color.rgb(166, 176, 202));
        settingsPanel.addView(settingsStatus);
        settingsPanel.setVisibility(View.GONE);
        root.addView(settingsPanel, new LinearLayout.LayoutParams(-1, -2));

        commandEdit = new EditText(this);
        commandEdit.setSingleLine(true);
        commandEdit.setHint("输入命令，例如 GET HELP");
        root.addView(commandEdit, new LinearLayout.LayoutParams(-1, -2));
        Button sendButton = button("发送命令");
        root.addView(sendButton, new LinearLayout.LayoutParams(-1, -2));

        uploadButton = button("UPLOAD / CONVERT IMAGE");
        uploadButton.setEnabled(false);
        root.addView(uploadButton, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout mediaRow = new LinearLayout(this);
        mediaListButton = button("MEDIA LIST");
        cancelUploadButton = button("CANCEL");
        retryUploadButton = button("RETRY");
        cancelUploadButton.setEnabled(false);
        retryUploadButton.setEnabled(false);
        mediaRow.addView(mediaListButton, weightParams());
        mediaRow.addView(cancelUploadButton, weightParams());
        mediaRow.addView(retryUploadButton, weightParams());
        root.addView(mediaRow);

        uploadProgress = new ProgressBar(this, null,
                android.R.attr.progressBarStyleHorizontal);
        uploadProgress.setMax(100);
        uploadProgress.setProgress(0);
        root.addView(uploadProgress, new LinearLayout.LayoutParams(-1, -2));
        uploadStatus = new TextView(this);
        uploadStatus.setText("Media idle");
        uploadStatus.setTextColor(Color.rgb(166, 176, 202));
        root.addView(uploadStatus, new LinearLayout.LayoutParams(-1, -2));

        mediaListView = new ListView(this);
        mediaAdapter = new ArrayAdapter<MediaEntry>(this,
                android.R.layout.simple_list_item_1, mediaEntries) {
            @Override
            public View getView(int position, View convertView,
                                android.view.ViewGroup parent) {
                MediaEntry entry = getItem(position);
                // Do not use the platform's simple_list_item_1 styling here:
                // on some Android themes it forces black text on the dark UI.
                TextView text = convertView instanceof TextView
                        ? (TextView) convertView : new TextView(MainActivity.this);
                text.setText(entry == null ? "" : entry.displayText());
                text.setTextColor(entry != null && entry.current
                        ? Color.rgb(48, 209, 88) : Color.rgb(226, 231, 244));
                text.setTextSize(14);
                text.setGravity(Gravity.CENTER_VERTICAL);
                text.setPadding(dp(14), dp(10), dp(14), dp(10));
                text.setMinHeight(dp(54));
                text.setBackground(roundBackground(Color.rgb(25, 32, 52), 10));
                return text;
            }
        };
        mediaListView.setBackgroundColor(Color.TRANSPARENT);
        mediaListView.setClipToPadding(false);
        mediaListView.setAdapter(mediaAdapter);
        root.addView(mediaListView, new LinearLayout.LayoutParams(-1, 240));

        ScrollView scroll = new ScrollView(this);
        logText = new TextView(this);
        logText.setTextSize(13);
        logText.setTextColor(Color.rgb(166, 176, 202));
        scroll.addView(logText);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        styleUi(root);
        setContentView(root);

        scanButton.setOnClickListener(v -> startScan());
        connectButton.setOnClickListener(v -> connectSelected());
        disconnectButton.setOnClickListener(v -> disconnect());
        pingButton.setOnClickListener(v -> writeCommand("PING"));
        statusButton.setOnClickListener(v -> writeCommand("GET STATUS"));
        settingsToggle.setOnClickListener(v -> {
            boolean visible = settingsPanel.getVisibility() == View.VISIBLE;
            settingsPanel.setVisibility(visible ? View.GONE : View.VISIBLE);
            if (!visible) refreshSettings();
        });
        brightnessSeek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (brightnessValue != null) brightnessValue.setText(progress + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) {
                setDeviceBrightness(seekBar.getProgress());
            }
        });
        rangeSeek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (rangeValue != null) rangeValue.setText(String.valueOf(progress));
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) {
                setSkyOrbRange(seekBar.getProgress());
            }
        });
        refreshSettingsButton.setOnClickListener(v -> {
            setSettingsStatus("正在读取设置…");
            log("设置操作：读取");
            refreshSettings();
        });
        autoLocationButton.setOnClickListener(v -> {
            setSettingsStatus("正在请求自动定位…");
            log("设置操作：自动定位");
            setAutoLocation();
        });
        saveSettingsButton.setOnClickListener(v -> {
            setSettingsStatus("正在保存设置…");
            log("设置操作：保存");
            saveSettings();
        });
        sendButton.setOnClickListener(v -> writeCommand(commandEdit.getText().toString().trim()));
        uploadButton.setOnClickListener(v -> chooseMediaFile());
        mediaListButton.setOnClickListener(v -> refreshMediaList());
        cancelUploadButton.setOnClickListener(v -> {
            if (mediaUploading) {
                mediaCancelRequested = true;
                synchronized (responseLock) {
                    responseLock.notifyAll();
                }
                setUploadStatus("Cancel requested");
            }
        });
        retryUploadButton.setOnClickListener(v -> {
            if (lastMediaUri != null && !mediaUploading) startMediaUpload(lastMediaUri);
        });
        mediaListView.setOnItemLongClickListener((parent, view, position, id) -> {
            if (position < 0 || position >= mediaEntries.size()) return true;
            showDeleteMediaDialog(mediaEntries.get(position).name);
            return true;
        });
        mediaListView.setOnItemClickListener((parent, view, position, id) -> {
            if (position < 0 || position >= mediaEntries.size()) return;
            playMedia(mediaEntries.get(position).name);
        });
    }

    private Button button(String text) {
        Button button = new Button(this);
        button.setText(text);
        return button;
    }

    private TextView label(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(Color.rgb(226, 231, 244));
        view.setTextSize(14);
        view.setGravity(Gravity.CENTER_VERTICAL);
        return view;
    }

    private EditText settingsEdit(String hint) {
        EditText edit = new EditText(this);
        edit.setHint(hint);
        edit.setSingleLine(true);
        edit.setTextSize(14);
        edit.setPadding(dp(10), 0, dp(10), 0);
        edit.setTextColor(Color.rgb(245, 247, 255));
        edit.setHintTextColor(Color.rgb(126, 137, 164));
        edit.setBackground(roundBackground(Color.rgb(25, 32, 52), 10));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, dp(42));
        params.topMargin = dp(6);
        edit.setLayoutParams(params);
        return edit;
    }

    private boolean settingsReady() {
        return gatt != null && commandCharacteristic != null && !mediaUploading;
    }

    private void setSettingsStatus(String text) {
        runOnUiThread(() -> {
            if (settingsStatus != null) settingsStatus.setText(text);
        });
    }

    private void refreshSettings() {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("GET SETTINGS", 8000);
                int brightness = parseJsonInt(response, "brightness", -1);
                int range = parseJsonInt(response, "range", -1);
                String lat = parseJsonNumber(response, "lat");
                String lon = parseJsonNumber(response, "lon");
                String ssid = parseJsonString(response, "ssid");
                runOnUiThread(() -> {
                    if (brightness >= 0) {
                        brightnessSeek.setProgress(brightness);
                        brightnessValue.setText(brightness + "%");
                    }
                    if (range >= 0) {
                        rangeSeek.setProgress(Math.min(3, range));
                        rangeValue.setText(String.valueOf(Math.min(3, range)));
                    }
                    if (!lat.isEmpty() && !"0.000000".equals(lat)) latitudeEdit.setText(lat);
                    if (!lon.isEmpty() && !"0.000000".equals(lon)) longitudeEdit.setText(lon);
                    if (!ssid.isEmpty()) wifiSsidEdit.setText(ssid);
                    settingsStatus.setText("设置已读取");
                });
            } catch (Exception e) {
                runOnUiThread(() -> settingsStatus.setText("读取失败：" + e.getMessage()));
            }
        });
    }

    private void setDeviceBrightness(int percent) {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        setSettingsStatus("正在应用亮度…");
        mediaExecutor.execute(() -> sendSettingCommand("SET BRIGHTNESS " + percent));
    }

    private void setSkyOrbRange(int range) {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        setSettingsStatus("正在应用量程…");
        mediaExecutor.execute(() -> sendSettingCommand("SET RANGE " + range));
    }

    private void setAutoLocation() {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        mediaExecutor.execute(() -> sendSettingCommand("SET AUTO_LOCATION"));
    }

    private void saveSettings() {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        String lat = latitudeEdit.getText().toString().trim();
        String lon = longitudeEdit.getText().toString().trim();
        String ssid = wifiSsidEdit.getText().toString().trim();
        String password = wifiPasswordEdit.getText().toString();
        mediaExecutor.execute(() -> {
            try {
                if (!lat.isEmpty() || !lon.isEmpty()) {
                    if (lat.isEmpty() || lon.isEmpty()) throw new IOException("经纬度需同时填写");
                    sendSettingCommandChecked("SET LOCATION " + lat + " " + lon);
                }
                if (!ssid.isEmpty() || !password.isEmpty()) {
                    if (ssid.isEmpty() || password.isEmpty()) throw new IOException("Wi-Fi 名称和密码需同时填写");
                    sendSettingCommandChecked("SET WIFI " + ssid + "|" + password);
                }
                runOnUiThread(() -> settingsStatus.setText("设置已保存"));
            } catch (Exception e) {
                runOnUiThread(() -> settingsStatus.setText("保存失败：" + e.getMessage()));
            }
        });
    }

    private void sendSettingCommand(String command) {
        try {
            sendSettingCommandChecked(command);
        } catch (Exception e) {
            runOnUiThread(() -> settingsStatus.setText("设置失败：" + e.getMessage()));
        }
    }

    private void sendSettingCommandChecked(String command) throws IOException {
        String response = sendCommandAndWait(command, 8000);
        if (response == null || response.startsWith("ERR")) {
            throw new IOException(response == null ? "无响应" : response);
        }
        log("设置: " + response);
    }

    private static int parseJsonInt(String response, String key, int fallback) {
        Matcher matcher = Pattern.compile("\\\"" + Pattern.quote(key)
                + "\\\"\\s*:\\s*(-?\\d+)").matcher(response == null ? "" : response);
        if (!matcher.find()) return fallback;
        try { return Integer.parseInt(matcher.group(1)); }
        catch (NumberFormatException e) { return fallback; }
    }

    private static String parseJsonNumber(String response, String key) {
        Matcher matcher = Pattern.compile("\\\"" + Pattern.quote(key)
                + "\\\"\\s*:\\s*(-?\\d+(?:\\.\\d+)?)").matcher(response == null ? "" : response);
        return matcher.find() ? matcher.group(1) : "";
    }

    private static String parseJsonString(String response, String key) {
        Matcher matcher = Pattern.compile("\\\"" + Pattern.quote(key)
                + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"")
                .matcher(response == null ? "" : response);
        return matcher.find() ? matcher.group(1) : "";
    }

    private void styleUi(LinearLayout root) {
        getWindow().setStatusBarColor(Color.rgb(9, 14, 28));
        getWindow().setNavigationBarColor(Color.rgb(9, 14, 28));
        styleNode(root);
    }

    private void styleNode(View view) {
        if (view instanceof Button) {
            Button button = (Button) view;
            button.setAllCaps(false);
            button.setTextColor(Color.WHITE);
            button.setTextSize(14);
            button.setMinHeight(dp(44));
            button.setPadding(dp(8), 0, dp(8), 0);
            button.setBackground(roundBackground(Color.rgb(38, 47, 72), 12));
        } else if (view instanceof EditText) {
            EditText edit = (EditText) view;
            edit.setTextColor(Color.rgb(245, 247, 255));
            edit.setHintTextColor(Color.rgb(126, 137, 164));
            edit.setBackground(roundBackground(Color.rgb(25, 32, 52), 12));
            edit.setPadding(dp(12), 0, dp(12), 0);
        } else if (view instanceof ListView) {
            ((ListView) view).setDivider(null);
        }
        if (view instanceof android.view.ViewGroup) {
            android.view.ViewGroup group = (android.view.ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) styleNode(group.getChildAt(i));
        }
    }

    private GradientDrawable roundBackground(int color, int radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private LinearLayout.LayoutParams weightParams() {
        return new LinearLayout.LayoutParams(0, -2, 1.0f);
    }

    private boolean hasBlePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)
                    == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                    == PackageManager.PERMISSION_GRANTED;
        }
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                == PackageManager.PERMISSION_GRANTED;
    }

    private void requestBlePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            requestPermissions(new String[]{Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT}, REQUEST_BLE_PERMISSIONS);
        } else {
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION},
                    REQUEST_BLE_PERMISSIONS);
        }
    }

    private void startScan() {
        if (bluetoothAdapter == null) return;
        if (!hasBlePermissions()) {
            requestBlePermissions();
            return;
        }
        try {
            scanner = bluetoothAdapter.getBluetoothLeScanner();
            if (scanner == null) {
                log("无法取得 BLE 扫描器");
                return;
            }
            if (scanning) scanner.stopScan(scanCallback);
            List<ScanFilter> filters = new ArrayList<>();
            filters.add(new ScanFilter.Builder().setDeviceName(DEVICE_NAME).build());
            ScanSettings settings = new ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            scanner.startScan(filters, settings, scanCallback);
            scanning = true;
            log("开始扫描 " + DEVICE_NAME);
            mainHandler.postDelayed(this::stopScan, 8000);
        } catch (SecurityException e) {
            log("扫描权限不足：" + e.getMessage());
        }
    }

    private void stopScan() {
        if (!scanning || scanner == null) return;
        if (hasBlePermissions()) {
            try {
                scanner.stopScan(scanCallback);
            } catch (SecurityException ignored) {
                // Permission may have been revoked while scanning.
            }
        }
        scanning = false;
        log("扫描结束");
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            if (device == null) return;
            selectedDevice = device;
            runOnUiThread(() -> {
                deviceText.setText("设备：" + DEVICE_NAME + "\n" + device.getAddress());
                log("发现 " + DEVICE_NAME + "（" + device.getAddress() + "）");
            });
        }

        @Override
        public void onScanFailed(int errorCode) {
            runOnUiThread(() -> log("扫描失败，错误码=" + errorCode));
        }
    };

    private void connectSelected() {
        if (selectedDevice == null) {
            log("请先扫描并选择 PACON");
            return;
        }
        if (!hasBlePermissions()) {
            requestBlePermissions();
            return;
        }
        try {
            disconnect();
            /* disconnect() intentionally raises the upload-cancel flag.  A
             * fresh connection must clear that stale flag or catalog control
             * commands would skip their wait and report a false timeout. */
            mediaCancelRequested = false;
            log("正在连接...");
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                gatt = selectedDevice.connectGatt(this, false, gattCallback,
                        android.bluetooth.BluetoothDevice.TRANSPORT_LE);
            } else {
                gatt = selectedDevice.connectGatt(this, false, gattCallback);
            }
        } catch (SecurityException e) {
            log("连接权限不足：" + e.getMessage());
        }
    }

    private void disconnect() {
        mediaCancelRequested = true;
        synchronized (responseLock) {
            responseLock.notifyAll();
        }
        synchronized (commandWriteLock) {
            commandWritePending = false;
            commandWriteLock.notifyAll();
        }
        if (gatt == null) return;
        try {
            gatt.disconnect();
            gatt.close();
        } catch (SecurityException ignored) {
            // The connection is already being torn down.
        }
        gatt = null;
        commandCharacteristic = null;
        responseCharacteristic = null;
        mediaDataCharacteristic = null;
        if (uploadButton != null) uploadButton.setEnabled(false);
        if (mediaListButton != null) mediaListButton.setEnabled(false);
        if (cancelUploadButton != null) cancelUploadButton.setEnabled(false);
        if (retryUploadButton != null) retryUploadButton.setEnabled(false);
        log("已断开");
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt callbackGatt, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                runOnUiThread(() -> log("已连接，正在发现服务"));
                try {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP
                            && callbackGatt.requestMtu(247)) {
                        runOnUiThread(() -> log("BLE MTU 247 请求已发送"));
                    } else {
                        callbackGatt.discoverServices();
                    }
                } catch (SecurityException e) {
                    runOnUiThread(() -> log("发现服务权限不足"));
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                synchronized (commandWriteLock) {
                    commandWritePending = false;
                    commandWriteLock.notifyAll();
                }
                runOnUiThread(() -> log("连接断开，status=" + status));
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt callbackGatt, int mtu, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                negotiatedMtu = mtu;
                runOnUiThread(() -> log("BLE MTU=" + mtu));
            } else {
                runOnUiThread(() -> log("BLE MTU 协商失败，使用默认 MTU"));
            }
            try {
                callbackGatt.discoverServices();
            } catch (SecurityException e) {
                runOnUiThread(() -> log("发现服务权限不足"));
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt callbackGatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                runOnUiThread(() -> log("服务发现失败，status=" + status));
                return;
            }
            BluetoothGattService service = callbackGatt.getService(SERVICE_UUID);
            if (service == null) {
                runOnUiThread(() -> log("未找到 PACON 自定义服务"));
                return;
            }
            commandCharacteristic = service.getCharacteristic(COMMAND_UUID);
            responseCharacteristic = service.getCharacteristic(RESPONSE_UUID);
            mediaDataCharacteristic = service.getCharacteristic(MEDIA_DATA_UUID);
            if (commandCharacteristic == null || responseCharacteristic == null) {
                runOnUiThread(() -> log("未找到命令或响应特征"));
                return;
            }
            log(mediaDataCharacteristic == null
                    ? "Binary media channel unavailable; using compatibility upload"
                    : "Binary media channel ready; using windowed upload");
            enableNotifications(callbackGatt);
            runOnUiThread(() -> log("PACON 服务已就绪，通知正在开启"));
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt callbackGatt, BluetoothGattDescriptor descriptor,
                                      int status) {
            if (descriptor.getUuid().equals(CCCD_UUID)) {
                if (status == BluetoothGatt.GATT_SUCCESS && uploadButton != null) {
                    runOnUiThread(() -> {
                        uploadButton.setEnabled(true);
                        if (mediaListButton != null) mediaListButton.setEnabled(true);
                    });
                }
                runOnUiThread(() -> log(status == BluetoothGatt.GATT_SUCCESS
                        ? "响应通知已开启" : "响应通知开启失败，status=" + status));
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    /* Let the CCCD write settle before issuing the first
                     * acknowledged catalog command after reconnect. */
                    mainHandler.postDelayed(MainActivity.this::refreshMediaList, 250L);
                }
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt callbackGatt,
                                          BluetoothGattCharacteristic characteristic, int status) {
            if (characteristic.getUuid().equals(COMMAND_UUID)) {
                synchronized (commandWriteLock) {
                    commandWritePending = false;
                    commandWriteLock.notifyAll();
                }
                runOnUiThread(() -> log("命令写入完成，status=" + status));
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt callbackGatt,
                                            BluetoothGattCharacteristic characteristic) {
            if (characteristic.getUuid().equals(RESPONSE_UUID)) {
                logNotification(characteristic.getValue());
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt callbackGatt,
                                            BluetoothGattCharacteristic characteristic,
                                            byte[] value) {
            if (characteristic.getUuid().equals(RESPONSE_UUID)) {
                logNotification(value);
            }
        }
    };

    private void enableNotifications(BluetoothGatt callbackGatt) {
        try {
            callbackGatt.setCharacteristicNotification(responseCharacteristic, true);
            BluetoothGattDescriptor descriptor = responseCharacteristic.getDescriptor(CCCD_UUID);
            if (descriptor == null) {
                runOnUiThread(() -> log("响应特征缺少 CCCD 描述符"));
                return;
            }
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            callbackGatt.writeDescriptor(descriptor);
        } catch (SecurityException e) {
            runOnUiThread(() -> log("开启通知权限不足"));
        }
    }

    private void writeCommand(String command) {
        if (command == null || command.isEmpty()) return;
        if (mediaUploading) {
            log("媒体上传进行中，请等待完成");
            return;
        }
        if (gatt == null || commandCharacteristic == null) {
            log("尚未连接或服务未就绪");
            return;
        }
        byte[] value = command.getBytes(StandardCharsets.UTF_8);
        if (value.length > Math.max(20, negotiatedMtu - 3)) {
            log("命令超过当前 BLE MTU 限制");
            return;
        }
        writeCommandInternal(command, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT, true);
    }

    private boolean writeCommandInternal(String command, int writeType, boolean logCommand) {
        if (command == null || command.isEmpty() || gatt == null || commandCharacteristic == null) {
            return false;
        }
        byte[] value = command.getBytes(StandardCharsets.UTF_8);
        if (value.length > Math.max(20, negotiatedMtu - 3)) {
            log("BLE 写入数据超过当前 MTU: " + value.length + " bytes");
            return false;
        }
        try {
            if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) {
                final long deadline = System.currentTimeMillis() + 3000L;
                synchronized (commandWriteLock) {
                    while (commandWritePending) {
                        long remaining = deadline - System.currentTimeMillis();
                        if (remaining <= 0L) {
                            log("BLE command write queue timeout");
                            return false;
                        }
                        try {
                            commandWriteLock.wait(remaining);
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            return false;
                        }
                    }
                    commandWritePending = true;
                }
            }
            commandCharacteristic.setWriteType(writeType);
            commandCharacteristic.setValue(value);
            if (!gatt.writeCharacteristic(commandCharacteristic)) {
                if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) {
                    synchronized (commandWriteLock) {
                        commandWritePending = false;
                        commandWriteLock.notifyAll();
                    }
                }
                log("命令未排入 BLE 写队列");
                return false;
            } else {
                if (logCommand) log(">> " + command);
                return true;
            }
        } catch (SecurityException e) {
            if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) {
                synchronized (commandWriteLock) {
                    commandWritePending = false;
                    commandWriteLock.notifyAll();
                }
            }
            log("写入权限不足：" + e.getMessage());
            return false;
        }
    }

    private void chooseMediaFile() {
        if (gatt == null || commandCharacteristic == null) {
            log("请先连接 PACON 并等待服务就绪");
            return;
        }
        if (mediaUploading) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_MEDIA_FILE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MEDIA_FILE && resultCode == RESULT_OK && data != null
                && data.getData() != null) {
            startMediaUpload(data.getData());
        }
    }

    private void startMediaUpload(Uri uri) {
        if (mediaUploading) return;
        lastMediaUri = uri;
        mediaUploading = true;
        mediaCancelRequested = false;
        if (uploadButton != null) uploadButton.setEnabled(false);
        if (mediaListButton != null) mediaListButton.setEnabled(false);
        if (cancelUploadButton != null) cancelUploadButton.setEnabled(true);
        if (retryUploadButton != null) retryUploadButton.setEnabled(false);
        setUploadProgress(0, "Preparing media");
        mediaExecutor.execute(() -> uploadMediaFile(uri));
    }

    private void refreshMediaList() {
        if (mediaUploading) {
            log("Upload is active; wait until it finishes");
            return;
        }
        if (gatt == null || commandCharacteristic == null) {
            log("Connect to PACON before listing media");
            return;
        }
        if (mediaListButton != null) mediaListButton.setEnabled(false);
        mediaExecutor.execute(() -> {
            try {
                String statusResponse = sendCommandAndWait("GET STATUS", 8000);
                int currentIndex = parseMediaIndex(statusResponse);
                String listResponse = sendCommandAndWait("MEDIA_LIST", MEDIA_CATALOG_TIMEOUT_MS);
                int count = parseMediaListCount(listResponse);
                ArrayList<MediaEntry> entries = new ArrayList<>();
                for (int index = 0; index < count; index++) {
                    String info = sendCommandAndWait("MEDIA_INFO " + index, MEDIA_CATALOG_TIMEOUT_MS);
                    MediaEntry entry = parseMediaInfo(info);
                    entry.current = index == currentIndex;
                    entries.add(entry);
                }
                runOnUiThread(() -> {
                    mediaEntries.clear();
                    mediaEntries.addAll(entries);
                    if (mediaAdapter != null) mediaAdapter.notifyDataSetChanged();
                    if (mediaListButton != null) mediaListButton.setEnabled(
                            gatt != null && commandCharacteristic != null);
                });
                log("Media list refreshed: " + entries.size() + " item(s). Long-press an item to delete.");
            } catch (Exception e) {
                log("Media list failed: " + e.getMessage());
                runOnUiThread(() -> {
                    if (mediaListButton != null) mediaListButton.setEnabled(
                            gatt != null && commandCharacteristic != null);
                });
            }
        });
    }

    private void showDeleteMediaDialog(String name) {
        new AlertDialog.Builder(this)
                .setTitle("Delete media")
                .setMessage(name)
                .setNegativeButton("CANCEL", null)
                .setPositiveButton("DELETE", (dialog, which) -> deleteMedia(name))
                .show();
    }

    private void playMedia(String name) {
        if (mediaUploading || gatt == null || commandCharacteristic == null) return;
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("MEDIA_PLAY " + name,
                        MEDIA_CATALOG_TIMEOUT_MS);
                requireResponse(response, "OK MEDIA_PLAY ");
                currentMediaName = name;
                runOnUiThread(() -> {
                    for (MediaEntry entry : mediaEntries) {
                        entry.current = name.equals(entry.name);
                    }
                    if (mediaAdapter != null) mediaAdapter.notifyDataSetChanged();
                });
                log("Playing media: " + name);
            } catch (Exception e) {
                log("Play failed: " + e.getMessage());
            }
        });
    }

    private void deleteMedia(String name) {
        if (mediaUploading || gatt == null || commandCharacteristic == null) return;
        if (mediaListButton != null) mediaListButton.setEnabled(false);
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("MEDIA_DELETE " + name, 5000);
                requireResponse(response, "OK MEDIA_DELETED");
                log("Deleted media: " + name);
                refreshMediaList();
            } catch (Exception e) {
                log("Delete failed: " + e.getMessage());
                runOnUiThread(() -> {
                    if (mediaListButton != null) mediaListButton.setEnabled(
                            gatt != null && commandCharacteristic != null);
                });
            }
        });
    }

    private static int parseMediaListCount(String response) throws IOException {
        Matcher matcher = Pattern.compile("^OK MEDIA_LIST (\\d+)$")
                .matcher(response == null ? "" : response);
        if (!matcher.matches()) throw new IOException("Unexpected MEDIA_LIST response: " + response);
        try {
            return Math.min(12, Integer.parseInt(matcher.group(1)));
        } catch (NumberFormatException e) {
            throw new IOException("Invalid media count", e);
        }
    }

    private static MediaEntry parseMediaInfo(String response) throws IOException {
        Matcher matcher = Pattern.compile("^OK MEDIA_INFO \\d+ (\\S+) (\\d+) (\\d+) (\\d+)$")
                .matcher(response == null ? "" : response);
        if (!matcher.matches()) throw new IOException("Unexpected MEDIA_INFO response: " + response);
        try {
            return new MediaEntry(matcher.group(1), Long.parseLong(matcher.group(2)),
                    Integer.parseInt(matcher.group(3)), Integer.parseInt(matcher.group(4)));
        } catch (NumberFormatException e) {
            throw new IOException("Invalid media info", e);
        }
    }

    private static int parseMediaIndex(String response) {
        Matcher matcher = Pattern.compile("\\\"media_index\\\"\\s*:\\s*(\\d+)")
                .matcher(response == null ? "" : response);
        if (!matcher.find()) return -1;
        try {
            return Integer.parseInt(matcher.group(1));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private void setUploadProgress(int percent, String status) {
        int clamped = Math.max(0, Math.min(100, percent));
        runOnUiThread(() -> {
            if (uploadProgress != null) uploadProgress.setProgress(clamped);
            if (uploadStatus != null) uploadStatus.setText(status + " (" + clamped + "%)");
        });
    }

    private void setUploadStatus(String status) {
        runOnUiThread(() -> {
            if (uploadStatus != null) uploadStatus.setText(status);
        });
    }

    private void uploadMediaFile(Uri uri) {
        boolean completed = false;
        try {
            requestUploadConnectionPriority(true);
            setUploadProgress(0, "Reading media");
            String displayName = queryDisplayName(uri);
            byte[] media = readMediaBytes(uri, displayName);
            if (media.length == 0 || media.length % LCD_FRAME_BYTES != 0) {
                throw new IOException("文件大小必须是单帧 " + LCD_FRAME_BYTES
                        + " 字节的整数倍，且文件应为原始 RGB565");
            }
            int frames = media.length / LCD_FRAME_BYTES;
            int fps = inferFps(displayName);
            String remoteName = mediaRemoteName(displayName);
            setUploadProgress(0, "Uploading " + remoteName);
            log("开始上传 " + remoteName + "，帧数=" + frames + "，fps=" + fps);

            String begin = sendCommandAndWait("MEDIA_BEGIN " + remoteName + " "
                    + media.length + " " + frames + " " + fps, 8000);
            requireResponse(begin, "OK MEDIA_READY");

            if (mediaDataCharacteristic != null) {
                uploadMediaBinary(media);
            } else {
            int offset = 0;
            int lastPercent = -1;
            while (offset < media.length) {
                if (mediaCancelRequested) throw new IOException("上传已取消");
                int chunkBytes = mediaChunkBytes(offset);
                if (chunkBytes <= 0) throw new IOException("当前 BLE MTU 太小，无法传输媒体");
                int count = Math.min(chunkBytes, media.length - offset);
                String command = "MEDIA_DATA " + offset + " "
                        + bytesToHex(media, offset, count);
                String response = sendCommandAndWait(command, 8000);
                requireResponse(response, "OK MEDIA_DATA ");
                String expected = parseMediaDataOffset(response);
                if (expected == null || Integer.parseInt(expected) != offset + count) {
                    throw new IOException("设备确认偏移异常: " + response);
                }
                offset += count;
                int percent = (int) ((long) offset * 100L / media.length);
                if (percent / 10 != lastPercent / 10) {
                    lastPercent = percent;
                    setUploadProgress(percent, "Uploading");
                    log("媒体上传进度 " + percent + "%");
                }
            }
            }
            String end = sendCommandAndWait("MEDIA_END", 8000);
            requireResponse(end, "OK MEDIA_COMMITTED");
            completed = true;
            setUploadProgress(100, "Upload complete");
            log("媒体上传完成，设备将重新扫描媒体目录");
            /* The firmware commits the file first and schedules its display
             * reader rescan on the main task.  Refresh after a short settle
             * period so the new file appears without requiring another tap. */
            mainHandler.postDelayed(this::refreshMediaList, 800L);
        } catch (Exception e) {
            setUploadStatus(mediaCancelRequested ? "Upload canceled" : "Upload failed");
            log("媒体上传失败: " + e.getMessage());
            boolean canceled = mediaCancelRequested;
            mediaCancelRequested = false;
            sendCommandAndWaitQuietly("MEDIA_ABORT");
            mediaCancelRequested = canceled;
        } finally {
            requestUploadConnectionPriority(false);
            mediaUploading = false;
            mediaCancelRequested = false;
            if (uploadButton != null) {
                runOnUiThread(() -> uploadButton.setEnabled(gatt != null
                        && commandCharacteristic != null));
            }
            if (cancelUploadButton != null) {
                runOnUiThread(() -> cancelUploadButton.setEnabled(false));
            }
            if (retryUploadButton != null) {
                final boolean retryEnabled = !completed && lastMediaUri != null;
                runOnUiThread(() -> retryUploadButton.setEnabled(retryEnabled));
            }
            if (mediaListButton != null) {
                runOnUiThread(() -> mediaListButton.setEnabled(gatt != null
                        && commandCharacteristic != null));
            }
        }
    }

    private void uploadMediaBinary(byte[] media) throws IOException {
        int payloadBytes = Math.max(1, negotiatedMtu - 3 - MEDIA_BINARY_HEADER_BYTES);
        int offset = 0;
        int lastPercent = -1;
        while (offset < media.length) {
            if (mediaCancelRequested) throw new IOException("上传已取消");
            int windowStart = offset;
            int packets = 0;
            synchronized (responseLock) {
                pendingMediaAck = null;
            }
            while (packets < MEDIA_BINARY_WINDOW_PACKETS && offset < media.length) {
                int count = Math.min(payloadBytes, media.length - offset);
                byte[] packet = new byte[MEDIA_BINARY_HEADER_BYTES + count];
                packet[0] = (byte) offset;
                packet[1] = (byte) (offset >>> 8);
                packet[2] = (byte) (offset >>> 16);
                packet[3] = (byte) (offset >>> 24);
                System.arraycopy(media, offset, packet, MEDIA_BINARY_HEADER_BYTES, count);
                writeMediaPacket(packet);
                offset += count;
                packets++;
                /* Give Android's GATT queue a scheduling point without
                 * returning to the old per-packet notification round trip. */
                if ((packets & 1) == 0) Thread.yield();
            }
            int ackOffset = offset;
            String ack = waitMediaAck(MEDIA_BINARY_ACK_TIMEOUT_MS);
            if (ack == null) {
                throw new IOException("媒体窗口确认超时，窗口起点=" + windowStart);
            }
            int confirmed = parseMediaAckOffset(ack);
            if (confirmed != ackOffset) {
                throw new IOException("媒体确认偏移异常: " + ack);
            }
            int percent = (int) ((long) offset * 100L / media.length);
            if (percent / 10 != lastPercent / 10) {
                lastPercent = percent;
                setUploadProgress(percent, "Uploading (BLE window)");
                log("媒体上传进度 " + percent + "%（BLE 窗口）");
            }
        }
    }

    private void writeMediaPacket(byte[] packet) throws IOException {
        if (gatt == null || mediaDataCharacteristic == null) {
            throw new IOException("媒体二进制通道未连接");
        }
        for (int retry = 0; retry < 20; retry++) {
            try {
                mediaDataCharacteristic.setWriteType(
                        BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
                mediaDataCharacteristic.setValue(packet);
                if (gatt.writeCharacteristic(mediaDataCharacteristic)) return;
            } catch (SecurityException e) {
                throw new IOException("BLE 写入权限不足", e);
            }
            try {
                Thread.sleep(3L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("上传线程被中断", e);
            }
        }
        throw new IOException("BLE 媒体数据未能排入写队列");
    }

    private void requestUploadConnectionPriority(boolean high) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP || gatt == null) return;
        try {
            gatt.requestConnectionPriority(high
                    ? BluetoothGatt.CONNECTION_PRIORITY_HIGH
                    : BluetoothGatt.CONNECTION_PRIORITY_BALANCED);
        } catch (SecurityException ignored) {
            // This is an optional performance hint; upload remains functional if rejected.
        }
    }

    private String waitMediaAck(long timeoutMs) throws IOException {
        long deadline = System.currentTimeMillis() + timeoutMs;
        synchronized (responseLock) {
            while (pendingMediaAck == null && !mediaCancelRequested) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) break;
                try {
                    responseLock.wait(remaining);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("上传线程被中断", e);
                }
            }
            return pendingMediaAck;
        }
    }

    private static int parseMediaAckOffset(String response) throws IOException {
        Matcher matcher = Pattern.compile("^OK MEDIA_BIN (\\d+)/\\d+$")
                .matcher(response == null ? "" : response);
        if (!matcher.matches()) throw new IOException("设备媒体确认异常: " + response);
        try {
            return Integer.parseInt(matcher.group(1));
        } catch (NumberFormatException e) {
            throw new IOException("设备媒体确认偏移异常: " + response, e);
        }
    }

    private String sendCommandAndWait(String command, long timeoutMs) throws IOException {
        synchronized (responseLock) {
            pendingResponse = null;
        }
        /* Control commands need an ordered GATT transaction.  Android can
         * defer WRITE_TYPE_NO_RESPONSE long enough for the device's notify
         * response to arrive after the five-second wait (MEDIA_LIST was the
         * first visible symptom).  Keep no-response writes for the binary
         * media data path, but use a normal write here. */
        if (!writeCommandInternal(command, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT, false)) {
            throw new IOException("BLE 写入失败");
        }
        long deadline = System.currentTimeMillis() + timeoutMs;
        synchronized (responseLock) {
            /* The cancel flag belongs to an active upload.  Do not let a
             * stale value from a reconnect abort a normal catalog query. */
            while (pendingResponse == null && !(mediaUploading && mediaCancelRequested)) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) break;
                try {
                    responseLock.wait(remaining);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("上传线程被中断");
                }
            }
            if (pendingResponse == null) throw new IOException("等待设备响应超时");
            return pendingResponse;
        }
    }

    private void sendCommandAndWaitQuietly(String command) {
        try {
            sendCommandAndWait(command, 3000);
        } catch (IOException ignored) {
            // The transfer is already in an error state.
        }
    }

    private int mediaChunkBytes(int offset) {
        String prefix = "MEDIA_DATA " + offset + " ";
        int availableHex = Math.min(MAX_MEDIA_HEX_CHARS,
                negotiatedMtu - 3 - prefix.getBytes(StandardCharsets.UTF_8).length);
        availableHex &= ~1;
        return availableHex / 2;
    }

    private static String bytesToHex(byte[] data, int offset, int count) {
        char[] hex = "0123456789ABCDEF".toCharArray();
        char[] output = new char[count * 2];
        for (int i = 0; i < count; i++) {
            int value = data[offset + i] & 0xFF;
            output[i * 2] = hex[value >>> 4];
            output[i * 2 + 1] = hex[value & 0x0F];
        }
        return new String(output);
    }

    private static void requireResponse(String response, String prefix) throws IOException {
        if (response == null || !response.startsWith(prefix)) {
            throw new IOException("设备响应异常: " + response);
        }
    }

    private static String parseMediaDataOffset(String response) {
        Matcher matcher = Pattern.compile("^OK MEDIA_DATA (\\d+)/\\d+$").matcher(response);
        return matcher.matches() ? matcher.group(1) : null;
    }

    private int inferFps(String name) {
        Matcher matcher = Pattern.compile("(?i)(?:^|[_-])(\\d{1,2})fps(?:[_\\-.]|$)")
                .matcher(name == null ? "" : name);
        if (matcher.find()) {
            int fps = Integer.parseInt(matcher.group(1));
            if (fps >= 1 && fps <= MAX_MEDIA_FPS) return fps;
        }
        return 8;
    }

    private static String sanitizeMediaName(String name) throws IOException {
        if (name == null || name.isEmpty()) name = "phone_media.rgb565";
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0) name = name.substring(slash + 1);
        name = name.replaceAll("[^A-Za-z0-9_.-]", "_");
        if (!name.toLowerCase(Locale.ROOT).endsWith(".rgb565")) {
            throw new IOException("只支持 .rgb565 原始媒体文件");
        }
        if (name.startsWith(".")) name = "media_" + name.substring(1);
        if (name.length() >= 60) name = name.substring(0, 53) + ".rgb565";
        return name;
    }

    private byte[] readMediaBytes(Uri uri, String displayName) throws IOException {
        if (isRgb565Name(displayName)) return readAllBytes(uri);
        Bitmap bitmap;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("无法打开图片");
            bitmap = BitmapFactory.decodeStream(input);
        }
        if (bitmap == null) throw new IOException("无法识别图片格式");
        try {
            log("正在转换图片：" + bitmap.getWidth() + "x" + bitmap.getHeight()
                    + " -> " + LCD_WIDTH + "x" + LCD_HEIGHT + " RGB565");
            return convertBitmapToRgb565(bitmap);
        } finally {
            bitmap.recycle();
        }
    }

    private byte[] convertBitmapToRgb565(Bitmap source) throws IOException {
        if (source.getWidth() <= 0 || source.getHeight() <= 0) {
            throw new IOException("图片尺寸无效");
        }
        float scale = Math.max((float) LCD_WIDTH / source.getWidth(),
                (float) LCD_HEIGHT / source.getHeight());
        int scaledWidth = Math.max(LCD_WIDTH, Math.round(source.getWidth() * scale));
        int scaledHeight = Math.max(LCD_HEIGHT, Math.round(source.getHeight() * scale));
        Bitmap scaled = Bitmap.createScaledBitmap(source, scaledWidth, scaledHeight, true);
        Bitmap crop = Bitmap.createBitmap(scaled,
                Math.max(0, (scaledWidth - LCD_WIDTH) / 2),
                Math.max(0, (scaledHeight - LCD_HEIGHT) / 2),
                LCD_WIDTH, LCD_HEIGHT);
        if (scaled != source && scaled != crop) scaled.recycle();
        int[] pixels = new int[LCD_WIDTH * LCD_HEIGHT];
        crop.getPixels(pixels, 0, LCD_WIDTH, 0, 0, LCD_WIDTH, LCD_HEIGHT);
        byte[] output = new byte[LCD_FRAME_BYTES];
        for (int index = 0; index < pixels.length; index++) {
            int argb = pixels[index];
            int red = (argb >>> 16) & 0xFF;
            int green = (argb >>> 8) & 0xFF;
            int blue = argb & 0xFF;
            int rgb565 = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >>> 3);
            output[index * 2] = (byte) (rgb565 & 0xFF);
            output[index * 2 + 1] = (byte) (rgb565 >>> 8);
        }
        if (crop != source) crop.recycle();
        return output;
    }

    private static boolean isRgb565Name(String name) {
        return name != null && name.toLowerCase(Locale.ROOT).endsWith(".rgb565");
    }

    private String mediaRemoteName(String sourceName) throws IOException {
        if (isRgb565Name(sourceName)) return sanitizeMediaName(sourceName);
        String name = sourceName == null ? "phone_image" : sourceName;
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0) name = name.substring(slash + 1);
        int dot = name.lastIndexOf('.');
        if (dot > 0) name = name.substring(0, dot);
        name = name.replaceAll("[^A-Za-z0-9_.-]", "_");
        if (name.isEmpty()) name = "phone_image";
        if (name.length() > 51) name = name.substring(0, 51);
        return name + ".rgb565";
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        }
        return "phone_media.rgb565";
    }

    private byte[] readAllBytes(Uri uri) throws IOException {
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("无法打开文件");
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) != -1) output.write(buffer, 0, read);
            return output.toByteArray();
        }
    }

    private void logNotification(byte[] value) {
        if (value == null) return;
        String raw = new String(value, StandardCharsets.UTF_8).trim();
        synchronized (responseLock) {
            if (raw.startsWith("OK MEDIA_BIN ")) pendingMediaAck = raw;
            pendingResponse = raw;
            responseLock.notifyAll();
        }
        if (mediaUploading && (raw.startsWith("OK MEDIA_DATA ")
                || raw.startsWith("OK MEDIA_BIN "))) return;
        String text = raw.replace("\r", "").replace("\n", "\\n");
        runOnUiThread(() -> log("<< " + text));
    }

    private void log(String message) {
        if (logText == null) return;
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(() -> log(message));
            return;
        }
        String time = new SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(new Date());
        logText.append(time + "  " + message + "\n");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_BLE_PERMISSIONS) {
            boolean granted = grantResults.length > 0;
            for (int result : grantResults) granted &= result == PackageManager.PERMISSION_GRANTED;
            log(granted ? "BLE 权限已授予，请再次点击扫描" : "BLE 权限未授予");
        }
    }

    @Override
    protected void onDestroy() {
        stopScan();
        disconnect();
        mediaExecutor.shutdownNow();
        super.onDestroy();
    }
}
