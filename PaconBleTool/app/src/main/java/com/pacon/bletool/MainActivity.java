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
import android.graphics.Matrix;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.content.res.ColorStateList;
import android.media.ExifInterface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
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
import android.widget.Switch;
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
    private static final int UI_BACKGROUND = Color.rgb(12, 17, 23);
    private static final int UI_CARD = Color.rgb(23, 30, 39);
    private static final int UI_ACCENT = Color.rgb(132, 225, 195);
    private static final int REQUEST_BLE_PERMISSIONS = 10;
    private static final int REQUEST_MEDIA_FILE = 20;
    private static final String DEVICE_NAME = "PACON-BLE-TEST";
    private static final int LCD_WIDTH = 475;
    private static final int LCD_HEIGHT = 466;
    private static final int LCD_FRAME_BYTES = LCD_WIDTH * LCD_HEIGHT * 2;
    private static final int MAX_MEDIA_HEX_CHARS = 216; // legacy compatibility path
    private static final int MAX_MEDIA_FPS = 12;
    private static final int[] RADAR_RANGE_KM = {5, 10, 15, 25, 35, 50};
    private static final int[] SCREEN_TIMEOUT_SECONDS = {15, 30, 60, 120, 300, 0};
    private static final String[] SCREEN_TIMEOUT_LABELS = {
            "15 秒", "30 秒", "1 分钟", "2 分钟", "5 分钟", "永不关闭（仍会降亮）"
    };
    private static final int MEDIA_BINARY_HEADER_BYTES = 4;
    private static final int MEDIA_BINARY_WINDOW_PACKETS = 8;
    private static final int MEDIA_BINARY_ACK_TIMEOUT_MS = 8000;
    /* Some phones still schedule scan radio work briefly after stopScan().
     * Give the CCCD transaction time to settle before the automatic catalog
     * request, and keep every control request on mediaExecutor. */
    private static final long AUTO_MEDIA_REFRESH_DELAY_MS = 1200L;
    /* Diagnostic A/B build: keep the freshly established GATT link completely
     * idle.  The user can still request PING, status, or the media catalog
     * manually after confirming that the connection itself stays alive. */
    private static final boolean AUTO_MEDIA_REFRESH_ON_CONNECT = false;
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
    private boolean bleConnecting;
    private boolean bleConnected;
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
    private TextView connectionStatus;
    private TextView logText;
    private TextView noticeText;
    private TextView overviewText;
    private TextView modeStatus;
    private LinearLayout debugPanel;
    private final LinearLayout[] companionPages = new LinearLayout[3];
    private final Button[] navButtons = new Button[3];
    private final Button[] modeButtons = new Button[3];
    private Button overviewButton;
    private boolean screenSwitching;
    private String confirmedScreen = "";
    private Button scanButton;
    private Button connectButton;
    private Button disconnectButton;
    private Button cameraButton;
    private ScrollView pageScroll;
    private ScrollView logScroll;
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
    private LinearLayout wifiSettingsPanel;
    private LinearLayout radarSettingsPanel;
    private LinearLayout clockSettingsPanel;
    private SeekBar brightnessSeek;
    private TextView brightnessValue;
    private Spinner screenTimeoutSpinner;
    private SeekBar rangeSeek;
    private TextView rangeValue;
    private EditText latitudeEdit;
    private EditText longitudeEdit;
    private EditText wifiSsidEdit;
    private EditText wifiPasswordEdit;
    private TextView settingsStatus;
    private TextView wifiSettingsStatus;
    private TextView radarSettingsStatus;
    private TextView clockSettingsStatus;
    private EditText customDateEdit;
    private EditText customTimeEdit;
    private EditText alarmTimeEdit;
    private Spinner watchStyleSpinner;
    private ListView wifiListView;
    private ArrayAdapter<WifiEntry> wifiAdapter;
    private final ArrayList<WifiEntry> wifiEntries = new ArrayList<>();
    private final ArrayList<MediaEntry> mediaEntries = new ArrayList<>();
    private Uri lastMediaUri;
    private byte[] lastPreparedMedia;
    private String lastPreparedDisplayName;
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
            return (current ? "正在播放 · " : "") + name + "\n" + size + " · "
                    + frames + " 帧 · " + fps + " fps";
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

    private static final class WifiEntry {
        final int index;
        final String ssid;
        final boolean selected;

        WifiEntry(int index, String ssid, boolean selected) {
            this.index = index;
            this.ssid = ssid;
            this.selected = selected;
        }

        @Override public String toString() {
            return (selected ? "●  " : "○  ") + ssid;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

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
        pageScroll = new ScrollView(this);
        pageScroll.setFillViewport(true);
        pageScroll.setClipToPadding(false);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(22), dp(20), dp(24));
        root.setBackgroundColor(UI_BACKGROUND);

        TextView title = new TextView(this);
        title.setText("PACON");
        title.setTextSize(30);
        title.setLetterSpacing(0.10f);
        title.setTextColor(Color.rgb(245, 247, 255));
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(title, new LinearLayout.LayoutParams(-1, -2));
        TextView subtitle = label("你的随身伙伴，一触即达");
        subtitle.setTextColor(Color.rgb(146, 161, 176));
        subtitle.setPadding(0, dp(6), 0, dp(22));
        root.addView(subtitle);
        for (int i = 0; i < companionPages.length; i++) {
            companionPages[i] = new LinearLayout(this);
            companionPages[i].setOrientation(LinearLayout.VERTICAL);
            root.addView(companionPages[i], new LinearLayout.LayoutParams(-1, -2));
        }
        LinearLayout devicePage = companionPages[0];
        LinearLayout mediaPage = companionPages[1];
        LinearLayout preferencesPage = companionPages[2];
        LinearLayout connectionCard = card(devicePage, "我的设备", "打开 PACON 蓝牙后，扫描并连接");

        deviceText = new TextView(this);
        deviceText.setText("尚未发现设备");
        deviceText.setTextSize(16);
        deviceText.setTextColor(Color.rgb(224, 230, 245));
        connectionCard.addView(deviceText, new LinearLayout.LayoutParams(-1, -2));

        connectionStatus = label("● 未连接");
        connectionStatus.setTextSize(15);
        connectionCard.addView(connectionStatus, new LinearLayout.LayoutParams(-1, dp(38)));

        LinearLayout scanRow = new LinearLayout(this);
        scanButton = button("扫描 PACON");
        scanButton.setTag("primary");
        connectButton = button("连接");
        disconnectButton = button("断开");
        scanRow.addView(scanButton, weightParams());
        scanRow.addView(connectButton, weightParams());
        scanRow.addView(disconnectButton, weightParams());
        connectionCard.addView(scanRow);
        overviewText = label("连接后可查看电量和当前界面");
        overviewText.setPadding(0, dp(14), 0, dp(6));
        connectionCard.addView(overviewText);
        overviewButton = button("刷新设备状态");
        connectionCard.addView(overviewButton);

        LinearLayout modeCard = card(devicePage, "切换界面", "在手机上选择 PACON 当前显示的内容");
        LinearLayout modes = new LinearLayout(this);
        String[] modeLabels = {"主界面", "流体", "OuO"};
        for (int i = 0; i < modeButtons.length; i++) {
            final int mode = i;
            modeButtons[i] = button(modeLabels[i]);
            modeButtons[i].setCompoundDrawablesWithIntrinsicBounds(null,
                    new CompanionIcon(i, UI_ACCENT, dp(28)), null, null);
            modeButtons[i].setCompoundDrawablePadding(dp(10));
            LinearLayout.LayoutParams modeParams = new LinearLayout.LayoutParams(0, dp(100), 1);
            modeParams.setMargins(dp(3), dp(4), dp(3), dp(4));
            modes.addView(modeButtons[i], modeParams);
            modeButtons[i].setOnClickListener(v -> requestDeviceScreen(mode));
        }
        modeCard.addView(modes);
        modeStatus = label("请先连接设备");
        modeStatus.setPadding(0, dp(12), 0, 0);
        modeCard.addView(modeStatus);

        LinearLayout testRow = new LinearLayout(this);
        Button pingButton = button("PING");
        Button statusButton = button("GET STATUS");
        testRow.addView(pingButton, weightParams());
        testRow.addView(statusButton, weightParams());
        debugPanel = new LinearLayout(this);
        debugPanel.setOrientation(LinearLayout.VERTICAL);
        debugPanel.addView(testRow);

        LinearLayout cameraCard = card(debugPanel, "遥控快门测试", "仅用于调试：需先在系统蓝牙中配对；相机须支持音量键拍照");
        cameraButton = button("触发快门");
        cameraButton.setEnabled(false);
        cameraCard.addView(cameraButton, new LinearLayout.LayoutParams(-1, dp(50)));

        LinearLayout preferencesCard = card(preferencesPage, "设备偏好", "管理显示、网络、雷达和时钟");

        Button settingsToggle = button("设备与显示");
        preferencesCard.addView(settingsToggle, new LinearLayout.LayoutParams(-1, -2));
        settingsPanel = new LinearLayout(this);
        settingsPanel.setOrientation(LinearLayout.VERTICAL);
        settingsPanel.setPadding(dp(12), dp(8), dp(12), dp(8));
        settingsPanel.setBackground(roundBackground(UI_CARD, 12));

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

        LinearLayout timeoutRow = new LinearLayout(this);
        timeoutRow.setGravity(Gravity.CENTER_VERTICAL);
        timeoutRow.addView(label("自动息屏"), new LinearLayout.LayoutParams(dp(88), -2));
        screenTimeoutSpinner = new Spinner(this);
        ArrayAdapter<String> timeoutAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item, SCREEN_TIMEOUT_LABELS);
        timeoutAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        screenTimeoutSpinner.setAdapter(timeoutAdapter);
        screenTimeoutSpinner.setSelection(screenTimeoutIndex(60));
        timeoutRow.addView(screenTimeoutSpinner, weightParams());
        Button applyTimeoutButton = button("应用");
        timeoutRow.addView(applyTimeoutButton,
                new LinearLayout.LayoutParams(dp(76), dp(46)));
        settingsPanel.addView(timeoutRow);

        settingsStatus = label("未读取设备设置");
        settingsStatus.setTextColor(Color.rgb(166, 176, 202));
        settingsPanel.addView(settingsStatus);
        settingsPanel.setVisibility(View.GONE);
        preferencesCard.addView(settingsPanel, new LinearLayout.LayoutParams(-1, -2));

        Button wifiSettingsToggle = button("Wi-Fi 管理");
        preferencesCard.addView(wifiSettingsToggle, new LinearLayout.LayoutParams(-1, -2));
        wifiSettingsPanel = new LinearLayout(this);
        wifiSettingsPanel.setOrientation(LinearLayout.VERTICAL);
        wifiSettingsPanel.setPadding(dp(12), dp(8), dp(12), dp(8));
        wifiSettingsPanel.setBackground(roundBackground(UI_CARD, 12));

        wifiListView = new ListView(this);
        wifiAdapter = new ArrayAdapter<WifiEntry>(this,
                android.R.layout.simple_list_item_1, wifiEntries) {
            @Override public View getView(int position, View convertView,
                                          android.view.ViewGroup parent) {
                WifiEntry entry = getItem(position);
                TextView text = convertView instanceof TextView
                        ? (TextView) convertView : new TextView(MainActivity.this);
                text.setText(entry == null ? "" : entry.toString());
                text.setTextColor(entry != null && entry.selected
                        ? Color.rgb(10, 132, 255) : Color.rgb(226, 231, 244));
                text.setTextSize(15);
                text.setGravity(Gravity.CENTER_VERTICAL);
                text.setPadding(dp(14), dp(8), dp(14), dp(8));
                text.setMinHeight(dp(48));
                text.setBackground(roundBackground(Color.rgb(25, 32, 52), 10));
                return text;
            }
        };
        wifiListView.setAdapter(wifiAdapter);
        wifiListView.setBackgroundColor(Color.TRANSPARENT);
        wifiSettingsPanel.addView(wifiListView,
                new LinearLayout.LayoutParams(-1, dp(210)));

        wifiSsidEdit = settingsEdit("新增 Wi-Fi 名称");
        wifiPasswordEdit = settingsEdit("Wi-Fi 密码（开放网络可留空）");
        wifiPasswordEdit.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        wifiSettingsPanel.addView(wifiSsidEdit);
        wifiSettingsPanel.addView(wifiPasswordEdit);
        LinearLayout wifiButtons = new LinearLayout(this);
        Button refreshWifiButton = button("刷新列表");
        Button saveWifiButton = button("保存网络");
        wifiButtons.addView(refreshWifiButton, weightParams());
        wifiButtons.addView(saveWifiButton, weightParams());
        wifiSettingsPanel.addView(wifiButtons);
        wifiSettingsStatus = label("点击网络连接，长按删除");
        wifiSettingsStatus.setTextColor(Color.rgb(166, 176, 202));
        wifiSettingsPanel.addView(wifiSettingsStatus);
        wifiSettingsPanel.setVisibility(View.GONE);
        preferencesCard.addView(wifiSettingsPanel, new LinearLayout.LayoutParams(-1, -2));

        Button radarSettingsToggle = button("雷达设置");
        preferencesCard.addView(radarSettingsToggle, new LinearLayout.LayoutParams(-1, -2));
        radarSettingsPanel = new LinearLayout(this);
        radarSettingsPanel.setOrientation(LinearLayout.VERTICAL);
        radarSettingsPanel.setPadding(dp(12), dp(8), dp(12), dp(8));
        radarSettingsPanel.setBackground(roundBackground(UI_CARD, 12));

        LinearLayout rangeRow = new LinearLayout(this);
        rangeRow.setGravity(Gravity.CENTER_VERTICAL);
        rangeRow.addView(label("雷达半径"), new LinearLayout.LayoutParams(dp(88), -2));
        rangeSeek = new SeekBar(this);
        rangeSeek.setMax(RADAR_RANGE_KM.length - 1);
        rangeSeek.setProgress(3);
        rangeRow.addView(rangeSeek, weightParams());
        rangeValue = label(RADAR_RANGE_KM[3] + " km");
        rangeValue.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        rangeRow.addView(rangeValue, new LinearLayout.LayoutParams(dp(64), -2));
        radarSettingsPanel.addView(rangeRow);

        latitudeEdit = settingsEdit("纬度");
        longitudeEdit = settingsEdit("经度");
        radarSettingsPanel.addView(latitudeEdit);
        radarSettingsPanel.addView(longitudeEdit);

        LinearLayout settingsButtons = new LinearLayout(this);
        Button refreshSettingsButton = button("读取");
        Button autoLocationButton = button("自动定位");
        Button saveSettingsButton = button("保存");
        settingsButtons.addView(refreshSettingsButton, weightParams());
        settingsButtons.addView(autoLocationButton, weightParams());
        settingsButtons.addView(saveSettingsButton, weightParams());
        radarSettingsPanel.addView(settingsButtons);
        radarSettingsStatus = label("未读取雷达设置");
        radarSettingsStatus.setTextColor(Color.rgb(166, 176, 202));
        radarSettingsPanel.addView(radarSettingsStatus);
        radarSettingsPanel.setVisibility(View.GONE);
        preferencesCard.addView(radarSettingsPanel, new LinearLayout.LayoutParams(-1, -2));

        Button clockSettingsToggle = button("时钟与闹钟");
        preferencesCard.addView(clockSettingsToggle, new LinearLayout.LayoutParams(-1, -2));
        clockSettingsPanel = new LinearLayout(this);
        clockSettingsPanel.setOrientation(LinearLayout.VERTICAL);
        clockSettingsPanel.setPadding(dp(12), dp(8), dp(12), dp(8));
        clockSettingsPanel.setBackground(roundBackground(UI_CARD, 12));

        clockSettingsStatus = label("未读取时钟状态");
        clockSettingsStatus.setTextColor(Color.rgb(166, 176, 202));
        clockSettingsPanel.addView(clockSettingsStatus);
        customDateEdit = settingsEdit("自定义日期 YYYY-MM-DD");
        customTimeEdit = settingsEdit("自定义时间 HH:MM:SS");
        clockSettingsPanel.addView(customDateEdit);
        clockSettingsPanel.addView(customTimeEdit);
        LinearLayout clockReadWriteRow = new LinearLayout(this);
        Button refreshClockButton = button("读取时钟");
        Button setCustomClockButton = button("写入自定义");
        clockReadWriteRow.addView(refreshClockButton, weightParams());
        clockReadWriteRow.addView(setCustomClockButton, weightParams());
        clockSettingsPanel.addView(clockReadWriteRow);
        LinearLayout syncRow = new LinearLayout(this);
        Button syncBleClockButton = button("蓝牙校时");
        Button syncWifiClockButton = button("Wi-Fi 校时");
        syncRow.addView(syncBleClockButton, weightParams());
        syncRow.addView(syncWifiClockButton, weightParams());
        clockSettingsPanel.addView(syncRow);

        alarmTimeEdit = settingsEdit("每日闹钟 HH:MM");
        clockSettingsPanel.addView(alarmTimeEdit);
        LinearLayout alarmRow = new LinearLayout(this);
        Button setAlarmButton = button("设置闹钟");
        Button disableAlarmButton = button("关闭闹钟");
        Button stopAlarmButton = button("停止响铃");
        alarmRow.addView(setAlarmButton, weightParams());
        alarmRow.addView(disableAlarmButton, weightParams());
        alarmRow.addView(stopAlarmButton, weightParams());
        clockSettingsPanel.addView(alarmRow);
        Button testAlarmButton = button("测试响铃");
        debugPanel.addView(testAlarmButton,
                new LinearLayout.LayoutParams(-1, dp(44)));

        LinearLayout watchStyleRow = new LinearLayout(this);
        watchStyleRow.setGravity(Gravity.CENTER_VERTICAL);
        watchStyleRow.addView(label("表盘样式"), new LinearLayout.LayoutParams(dp(88), -2));
        watchStyleSpinner = new Spinner(this);
        ArrayAdapter<String> styleAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item,
                new String[]{"KKD1 · 狂三双臂指针", "KKD2 · 旋转人物表盘"});
        styleAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        watchStyleSpinner.setAdapter(styleAdapter);
        watchStyleRow.addView(watchStyleSpinner, weightParams());
        Button applyWatchStyleButton = button("应用");
        watchStyleRow.addView(applyWatchStyleButton,
                new LinearLayout.LayoutParams(dp(74), dp(44)));
        clockSettingsPanel.addView(watchStyleRow);
        clockSettingsPanel.setVisibility(View.GONE);
        preferencesCard.addView(clockSettingsPanel, new LinearLayout.LayoutParams(-1, -2));

        commandEdit = new EditText(this);
        commandEdit.setSingleLine(true);
        commandEdit.setHint("输入命令，例如 GET HELP");
        debugPanel.addView(commandEdit, new LinearLayout.LayoutParams(-1, dp(52)));
        Button sendButton = button("发送命令");
        debugPanel.addView(sendButton, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout mediaCard = card(mediaPage, "屏幕素材", "上传照片或 RGB565 动画，让主界面更有个性");

        uploadButton = button("添加图片或动画");
        uploadButton.setTag("primary");
        uploadButton.setEnabled(false);
        mediaCard.addView(uploadButton, new LinearLayout.LayoutParams(-1, dp(52)));

        LinearLayout mediaRow = new LinearLayout(this);
        mediaListButton = button("刷新素材");
        cancelUploadButton = button("取消上传");
        retryUploadButton = button("重试");
        cancelUploadButton.setEnabled(false);
        retryUploadButton.setEnabled(false);
        mediaRow.addView(mediaListButton, weightParams());
        mediaRow.addView(cancelUploadButton, weightParams());
        mediaRow.addView(retryUploadButton, weightParams());
        mediaCard.addView(mediaRow);

        uploadProgress = new ProgressBar(this, null,
                android.R.attr.progressBarStyleHorizontal);
        uploadProgress.setMax(100);
        uploadProgress.setProgress(0);
        uploadProgress.setProgressTintList(ColorStateList.valueOf(UI_ACCENT));
        mediaCard.addView(uploadProgress, new LinearLayout.LayoutParams(-1, dp(12)));
        uploadStatus = new TextView(this);
        uploadStatus.setText("尚无传输任务 · 点击刷新查看设备素材");
        uploadStatus.setTextColor(Color.rgb(166, 176, 202));
        mediaCard.addView(uploadStatus, new LinearLayout.LayoutParams(-1, -2));

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
        final float[] mediaListTouchY = {0f};
        mediaListView.setOnTouchListener((view, event) -> {
            android.view.ViewParent parent = view.getParent();
            if (parent == null) return false;
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    mediaListTouchY[0] = event.getY();
                    parent.requestDisallowInterceptTouchEvent(true);
                    break;
                case MotionEvent.ACTION_MOVE:
                    boolean keepListGesture = NestedListScrollPolicy.disallowParentIntercept(
                            mediaListTouchY[0], event.getY(),
                            mediaListView.canScrollVertically(-1),
                            mediaListView.canScrollVertically(1));
                    parent.requestDisallowInterceptTouchEvent(keepListGesture);
                    mediaListTouchY[0] = event.getY();
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    parent.requestDisallowInterceptTouchEvent(false);
                    break;
                default:
                    break;
            }
            // Preserve ListView scrolling, item clicks and long-press deletion.
            return false;
        });
        LinearLayout catalogCard = card(mediaPage, "设备上的素材", "点击播放 · 长按删除");
        TextView mediaEmpty = label("还没有素材记录\n连接设备后，点击「刷新素材」");
        mediaEmpty.setGravity(Gravity.CENTER);
        mediaEmpty.setPadding(0, dp(32), 0, dp(32));
        catalogCard.addView(mediaEmpty);
        catalogCard.addView(mediaListView, new LinearLayout.LayoutParams(-1, dp(280)));
        mediaListView.setEmptyView(mediaEmpty);

        LinearLayout debugCard = card(preferencesPage, "高级选项", "日常使用无需开启调试模式");
        Switch debugSwitch = new Switch(this);
        debugSwitch.setText("调试模式");
        debugSwitch.setTextSize(16);
        debugSwitch.setTextColor(Color.WHITE);
        debugSwitch.setPadding(0, dp(12), 0, dp(12));
        debugSwitch.setChecked(getPreferences(MODE_PRIVATE).getBoolean("debug_mode", false));
        debugCard.addView(debugSwitch, new LinearLayout.LayoutParams(-1, dp(56)));
        debugCard.addView(debugPanel, new LinearLayout.LayoutParams(-1, -2));
        debugPanel.setVisibility(debugSwitch.isChecked() ? View.VISIBLE : View.GONE);
        debugSwitch.setOnCheckedChangeListener((v, checked) -> {
            getPreferences(MODE_PRIVATE).edit().putBoolean("debug_mode", checked).apply();
            debugPanel.setVisibility(checked ? View.VISIBLE : View.GONE);
            if (!checked) {
                commandEdit.clearFocus();
                ((android.view.inputmethod.InputMethodManager)getSystemService(INPUT_METHOD_SERVICE))
                        .hideSoftInputFromWindow(commandEdit.getWindowToken(), 0);
            }
        });

        TextView logTitle = label("运行日志");
        logTitle.setTextSize(15);
        logTitle.setTypeface(null, android.graphics.Typeface.BOLD);
        LinearLayout.LayoutParams logTitleParams = new LinearLayout.LayoutParams(-1, -2);
        logTitleParams.topMargin = dp(12);
        debugPanel.addView(logTitle, logTitleParams);

        logScroll = new ScrollView(this);
        logScroll.setFillViewport(true);
        logScroll.setNestedScrollingEnabled(true);
        logScroll.setVerticalScrollBarEnabled(true);
        logScroll.setScrollbarFadingEnabled(false);
        logScroll.setBackground(roundBackground(Color.rgb(16, 23, 41), 12));
        logScroll.setOnTouchListener((view, event) -> {
            boolean dragging = event.getActionMasked() == MotionEvent.ACTION_DOWN
                    || event.getActionMasked() == MotionEvent.ACTION_MOVE;
            view.getParent().requestDisallowInterceptTouchEvent(dragging);
            return false;
        });
        logText = new TextView(this);
        logText.setTextSize(13);
        logText.setTextColor(Color.rgb(166, 176, 202));
        logText.setTextIsSelectable(true);
        logText.setPadding(dp(12), dp(10), dp(12), dp(18));
        logScroll.addView(logText, new ScrollView.LayoutParams(-1, -2));
        debugPanel.addView(logScroll, new LinearLayout.LayoutParams(-1, dp(280)));
        LinearLayout logActions = new LinearLayout(this);
        Button copyLogButton = button("复制日志");
        Button clearLogButton = button("清空日志");
        logActions.addView(copyLogButton, weightParams());
        logActions.addView(clearLogButton, weightParams());
        debugPanel.addView(logActions);
        copyLogButton.setOnClickListener(v -> {
            android.content.ClipboardManager clipboard = (android.content.ClipboardManager)
                    getSystemService(CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(android.content.ClipData.newPlainText("PACON 日志", logText.getText()));
            noticeText.setText("日志已复制；分享前请检查设备信息");
        });
        clearLogButton.setOnClickListener(v -> logText.setText(""));
        TextView version = label("PACON Companion  ·  0.2\n通过蓝牙连接你的 PACON");
        version.setTextColor(Color.rgb(146, 161, 176));
        version.setGravity(Gravity.CENTER);
        version.setPadding(0, dp(12), 0, dp(12));
        preferencesPage.addView(version);
        styleUi(root);
        pageScroll.addView(root, new ScrollView.LayoutParams(-1, -2));
        LinearLayout shell = new LinearLayout(this);
        shell.setOrientation(LinearLayout.VERTICAL);
        shell.setBackgroundColor(UI_BACKGROUND);
        shell.addView(pageScroll, new LinearLayout.LayoutParams(-1, 0, 1));
        noticeText = label("准备好后，扫描附近的 PACON");
        noticeText.setTextSize(12);
        noticeText.setMaxLines(2);
        noticeText.setPadding(dp(24), dp(6), dp(24), dp(6));
        shell.addView(noticeText, new LinearLayout.LayoutParams(-1, -2));
        LinearLayout navigation = new LinearLayout(this);
        navigation.setPadding(dp(16), dp(8), dp(16), dp(8));
        String[] tabLabels = {"设备", "素材", "设置"};
        for (int i = 0; i < navButtons.length; i++) {
            final int tab = i;
            navButtons[i] = button(tabLabels[i]);
            styleNode(navButtons[i]);
            navigation.addView(navButtons[i], new LinearLayout.LayoutParams(0, dp(48), 1));
            navButtons[i].setOnClickListener(v -> showCompanionPage(tab));
        }
        shell.addView(navigation, new LinearLayout.LayoutParams(-1, -2));
        setContentView(shell);
        shell.setOnApplyWindowInsetsListener((view, insets) -> {
            int topInset;
            int bottomInset;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
                topInset = bars.top;
                bottomInset = bars.bottom;
            } else {
                topInset = insets.getSystemWindowInsetTop();
                bottomInset = insets.getSystemWindowInsetBottom();
            }
            int imeInset = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                    ? insets.getInsets(WindowInsets.Type.ime()).bottom : 0;
            shell.setPadding(0, topInset, 0, Math.max(bottomInset, imeInset));
            return insets;
        });
        shell.requestApplyInsets();
        showCompanionPage(0);
        updateConnectionUi("未连接", false, false);

        View.OnFocusChangeListener revealEditor = (view, hasFocus) -> {
            if (!hasFocus || pageScroll == null) return;
            pageScroll.postDelayed(() -> {
                int target = Math.max(0, view.getBottom() + ((View)view.getParent()).getTop()
                        - pageScroll.getHeight() / 3);
                pageScroll.smoothScrollTo(0, target);
                view.requestRectangleOnScreen(new android.graphics.Rect(
                        0, 0, view.getWidth(), view.getHeight()), true);
            }, 280L);
        };
        wifiSsidEdit.setOnFocusChangeListener(revealEditor);
        wifiPasswordEdit.setOnFocusChangeListener(revealEditor);
        customDateEdit.setOnFocusChangeListener(revealEditor);
        customTimeEdit.setOnFocusChangeListener(revealEditor);
        alarmTimeEdit.setOnFocusChangeListener(revealEditor);

        scanButton.setOnClickListener(v -> startScan());
        connectButton.setOnClickListener(v -> connectSelected());
        disconnectButton.setOnClickListener(v -> disconnect());
        pingButton.setOnClickListener(v -> writeCommand("PING"));
        statusButton.setOnClickListener(v -> writeCommand("GET STATUS"));
        cameraButton.setOnClickListener(v -> writeCommand("CAMERA SHUTTER"));
        overviewButton.setOnClickListener(v -> refreshOverview());
        settingsToggle.setOnClickListener(v -> {
            boolean visible = settingsPanel.getVisibility() == View.VISIBLE;
            settingsPanel.setVisibility(visible ? View.GONE : View.VISIBLE);
            wifiSettingsPanel.setVisibility(View.GONE);
            radarSettingsPanel.setVisibility(View.GONE);
            clockSettingsPanel.setVisibility(View.GONE);
            if (!visible) refreshDeviceSettings();
        });
        wifiSettingsToggle.setOnClickListener(v -> {
            boolean visible = wifiSettingsPanel.getVisibility() == View.VISIBLE;
            settingsPanel.setVisibility(View.GONE);
            radarSettingsPanel.setVisibility(View.GONE);
            clockSettingsPanel.setVisibility(View.GONE);
            wifiSettingsPanel.setVisibility(visible ? View.GONE : View.VISIBLE);
            if (!visible) refreshWifiProfiles();
        });
        radarSettingsToggle.setOnClickListener(v -> {
            boolean visible = radarSettingsPanel.getVisibility() == View.VISIBLE;
            settingsPanel.setVisibility(View.GONE);
            wifiSettingsPanel.setVisibility(View.GONE);
            clockSettingsPanel.setVisibility(View.GONE);
            radarSettingsPanel.setVisibility(visible ? View.GONE : View.VISIBLE);
            if (!visible) refreshRadarSettings();
        });
        clockSettingsToggle.setOnClickListener(v -> {
            boolean visible = clockSettingsPanel.getVisibility() == View.VISIBLE;
            settingsPanel.setVisibility(View.GONE);
            wifiSettingsPanel.setVisibility(View.GONE);
            radarSettingsPanel.setVisibility(View.GONE);
            clockSettingsPanel.setVisibility(visible ? View.GONE : View.VISIBLE);
            if (!visible) refreshClockSettings();
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
        applyTimeoutButton.setOnClickListener(v -> setScreenTimeout());
        rangeSeek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (rangeValue != null) {
                    int index = Math.max(0, Math.min(RADAR_RANGE_KM.length - 1, progress));
                    rangeValue.setText(RADAR_RANGE_KM[index] + " km");
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) {
                setSkyOrbRange(seekBar.getProgress());
            }
        });
        refreshSettingsButton.setOnClickListener(v -> {
            setRadarSettingsStatus("正在读取雷达设置…");
            refreshRadarSettings();
        });
        autoLocationButton.setOnClickListener(v -> {
            setRadarSettingsStatus("正在请求 IP 自动定位…");
            setAutoLocation();
        });
        saveSettingsButton.setOnClickListener(v -> {
            setRadarSettingsStatus("正在保存雷达设置…");
            saveSettings();
        });
        refreshWifiButton.setOnClickListener(v -> refreshWifiProfiles());
        saveWifiButton.setOnClickListener(v -> saveWifiProfile());
        refreshClockButton.setOnClickListener(v -> refreshClockSettings());
        setCustomClockButton.setOnClickListener(v -> setCustomClock());
        syncBleClockButton.setOnClickListener(v -> syncClockFromPhone());
        syncWifiClockButton.setOnClickListener(v -> syncClockFromWifi());
        setAlarmButton.setOnClickListener(v -> setAlarm());
        disableAlarmButton.setOnClickListener(v -> sendClockCommand("SET ALARM OFF", "闹钟已关闭"));
        stopAlarmButton.setOnClickListener(v -> sendClockCommand("STOP ALARM", "响铃已停止"));
        testAlarmButton.setOnClickListener(v -> sendClockCommand("TEST ALARM", "正在测试蜂鸣器"));
        applyWatchStyleButton.setOnClickListener(v ->
                applyWatchStyle(watchStyleSpinner.getSelectedItemPosition()));
        wifiListView.setOnItemClickListener((parent, view, position, id) -> {
            if (position < 0 || position >= wifiEntries.size()) return;
            selectWifiProfile(wifiEntries.get(position));
        });
        wifiListView.setOnItemLongClickListener((parent, view, position, id) -> {
            if (position < 0 || position >= wifiEntries.size()) return true;
            showDeleteWifiDialog(wifiEntries.get(position));
            return true;
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
                setUploadStatus("正在取消上传…");
            }
        });
        retryUploadButton.setOnClickListener(v -> {
            if (lastMediaUri != null && !mediaUploading) {
                startMediaUploadPrepared(lastMediaUri, lastPreparedMedia,
                        lastPreparedDisplayName);
            }
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

    private LinearLayout card(LinearLayout parent, String title, String description) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(18), dp(18), dp(18), dp(18));
        card.setBackground(roundBackground(UI_CARD, 22));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.bottomMargin = dp(16);
        parent.addView(card, params);
        TextView heading = label(title);
        heading.setTextSize(19);
        heading.setTypeface(null, android.graphics.Typeface.BOLD);
        card.addView(heading);
        TextView detail = label(description);
        detail.setTextSize(13);
        detail.setTextColor(Color.rgb(146, 161, 176));
        detail.setPadding(0, dp(6), 0, dp(16));
        card.addView(detail);
        return card;
    }

    private void showCompanionPage(int selected) {
        for (int i = 0; i < companionPages.length; i++) {
            companionPages[i].setVisibility(i == selected ? View.VISIBLE : View.GONE);
            navButtons[i].setTextColor(i == selected ? UI_ACCENT : Color.rgb(146, 161, 176));
            navButtons[i].setSelected(i == selected);
            navButtons[i].setBackground(roundBackground(i == selected ? UI_CARD : UI_BACKGROUND, 16));
        }
        pageScroll.scrollTo(0, 0);
        if (selected != 2) {
            View focus = getCurrentFocus();
            if (focus != null) {
                ((android.view.inputmethod.InputMethodManager)getSystemService(INPUT_METHOD_SERVICE))
                        .hideSoftInputFromWindow(focus.getWindowToken(), 0);
                focus.clearFocus();
            }
        }
    }

    private void updateModeControls() {
        boolean enabled = bleConnected && settingsReady() && !screenSwitching;
        for (int i = 0; i < modeButtons.length; i++) {
            Button button = modeButtons[i];
            if (button == null) continue;
            button.setEnabled(enabled);
            button.setAlpha(enabled ? 1f : 0.42f);
            boolean selected = UiModeProtocol.MODES[i].equals(confirmedScreen);
            button.setSelected(selected);
            GradientDrawable background = roundBackground(Color.rgb(32, 43, 52), 16);
            if (selected) background.setStroke(dp(2), UI_ACCENT);
            button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x3384E1C3), background, null));
        }
        if (overviewButton != null) overviewButton.setEnabled(enabled);
        if (uploadButton != null) uploadButton.setEnabled(enabled);
        if (mediaListButton != null) mediaListButton.setEnabled(enabled);
        if (retryUploadButton != null) retryUploadButton.setEnabled(enabled && lastMediaUri != null);
        if (cameraButton != null) {
            cameraButton.setEnabled(enabled);
            cameraButton.setAlpha(enabled ? 1f : 0.42f);
        }
    }

    private void requestDeviceScreen(int index) {
        if (!bleConnected || !settingsReady() || screenSwitching) return;
        final BluetoothGatt session = gatt;
        screenSwitching = true;
        modeStatus.setText("正在切换到" + UiModeProtocol.LABELS[index] + "…");
        updateModeControls();
        mediaExecutor.execute(() -> {
            String result;
            boolean completed = false;
            try {
                if (gatt != session) throw new IOException("连接已变更，请重试");
                UiModeProtocol.requireQueued(sendCommandAndWait(UiModeProtocol.command(index), 8000), index);
                for (int attempt = 0; attempt < 12; attempt++) {
                    if (gatt != session) throw new IOException("连接已断开");
                    String state = sendCommandAndWait("GET UI", 3000);
                    if ("busy".equals(UiModeProtocol.field(state, "state"))) {
                        throw new IOException("设备正忙，请稍后重试");
                    }
                    if (UiModeProtocol.completed(state, index)) { completed = true; break; }
                    android.os.SystemClock.sleep(150);
                }
                if (!completed) throw new IOException("未确认切换完成，请刷新设备状态");
                result = "已切换到" + UiModeProtocol.LABELS[index];
            } catch (Exception e) {
                result = "切换失败：" + e.getMessage();
            }
            final String message = result;
            final boolean success = completed;
            runOnUiThread(() -> {
                screenSwitching = false;
                if (session == gatt && bleConnected) {
                    if (success) confirmedScreen = UiModeProtocol.MODES[index];
                    modeStatus.setText(message);
                    noticeText.setText(message);
                }
                updateModeControls();
            });
        });
    }

    private void refreshOverview() {
        if (!bleConnected || !settingsReady() || screenSwitching) return;
        final BluetoothGatt session = gatt;
        overviewButton.setEnabled(false);
        overviewText.setText("正在读取设备状态…");
        mediaExecutor.execute(() -> {
            try {
                if (gatt != session) throw new IOException("连接已变更");
                String response = sendCommandAndWait("GET STATUS", 8000);
                int battery = parseJsonInt(response, "battery_pct", -1);
                boolean valid = parseJsonBoolean(response, "battery_valid", false);
                String batteryText = valid && battery >= 0 ? "电量 " + battery + "%" : "电量暂不可用";
                if (parseJsonBoolean(response, "charging", false)) batteryText += " · 充电中";
                final String summary = batteryText;
                String ui = sendCommandAndWait("GET UI", 3000);
                final String screen = UiModeProtocol.field(ui, "screen");
                runOnUiThread(() -> {
                    if (session != gatt || !bleConnected) return;
                    overviewText.setText(summary);
                    confirmedScreen = screen;
                    modeStatus.setText(screen.isEmpty() ? "当前固件未提供切屏状态，请更新固件"
                            : "当前界面：" + UiModeProtocol.label(screen));
                    updateModeControls();
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (session != gatt || !bleConnected) return;
                    overviewText.setText("读取失败：" + e.getMessage());
                    updateModeControls();
                });
            }
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

    private void updateConnectionUi(String text, boolean connecting, boolean connected) {
        bleConnecting = connecting;
        bleConnected = connected;
        if (connectionStatus != null) {
            connectionStatus.setText("● " + text);
            connectionStatus.setTextColor(connected ? Color.rgb(48, 209, 88) :
                    connecting ? Color.rgb(255, 190, 75) : Color.rgb(255, 105, 97));
        }
        boolean canConnect = selectedDevice != null && !connecting && !connected;
        if (connectButton != null) {
            connectButton.setEnabled(canConnect);
            connectButton.setAlpha(canConnect ? 1.0f : 0.42f);
        }
        boolean canDisconnect = connecting || connected;
        if (disconnectButton != null) {
            disconnectButton.setEnabled(canDisconnect);
            disconnectButton.setAlpha(canDisconnect ? 1.0f : 0.42f);
        }
        if (cameraButton != null) {
            boolean canCamera = connected && gatt != null && commandCharacteristic != null;
            cameraButton.setEnabled(canCamera);
            cameraButton.setAlpha(canCamera ? 1.0f : 0.42f);
        }
        if (!connected) {
            confirmedScreen = "";
            if (modeStatus != null) modeStatus.setText("请先连接设备");
            if (overviewText != null) overviewText.setText("连接后可查看电量和当前界面");
        } else if (modeStatus != null && confirmedScreen.isEmpty()) {
            modeStatus.setText("选择一个界面，或刷新设备状态");
        }
        if (scanButton != null) scanButton.setEnabled(!connecting && !connected);
        updateModeControls();
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

    private void setWifiSettingsStatus(String text) {
        runOnUiThread(() -> {
            if (wifiSettingsStatus != null) wifiSettingsStatus.setText(text);
        });
    }

    private void setRadarSettingsStatus(String text) {
        runOnUiThread(() -> {
            if (radarSettingsStatus != null) radarSettingsStatus.setText(text);
        });
    }

    private void setClockSettingsStatus(String text) {
        runOnUiThread(() -> {
            if (clockSettingsStatus != null) clockSettingsStatus.setText(text);
        });
    }

    private void refreshDeviceSettings() {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("GET SETTINGS", 8000);
                int brightness = parseJsonInt(response, "brightness", -1);
                int screenTimeout = parseJsonInt(response, "screen_timeout", 60);
                runOnUiThread(() -> {
                    if (brightness >= 0) {
                        brightnessSeek.setProgress(brightness);
                        brightnessValue.setText(brightness + "%");
                    }
                    screenTimeoutSpinner.setSelection(screenTimeoutIndex(screenTimeout));
                    settingsStatus.setText("设备设置已读取");
                });
            } catch (Exception e) {
                setSettingsStatus("读取失败：" + e.getMessage());
            }
        });
    }

    private static int screenTimeoutIndex(int seconds) {
        for (int index = 0; index < SCREEN_TIMEOUT_SECONDS.length; index++) {
            if (SCREEN_TIMEOUT_SECONDS[index] == seconds) return index;
        }
        return 2;
    }

    private void setScreenTimeout() {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        int index = screenTimeoutSpinner == null ? 2
                : screenTimeoutSpinner.getSelectedItemPosition();
        index = Math.max(0, Math.min(SCREEN_TIMEOUT_SECONDS.length - 1, index));
        final int seconds = SCREEN_TIMEOUT_SECONDS[index];
        setSettingsStatus("正在应用息屏时间…");
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("SET SCREEN TIMEOUT " + seconds, 8000);
                requireResponse(response, "OK SCREEN TIMEOUT ");
                setSettingsStatus(seconds == 0 ? "已设为永不关闭（20 秒后仍会降亮）"
                        : "无操作 " + seconds + " 秒后关闭屏幕");
            } catch (Exception e) {
                setSettingsStatus("设置失败：" + e.getMessage());
            }
        });
    }

    private void refreshClockSettings() {
        if (!settingsReady()) {
            setClockSettingsStatus("请先连接 PACON");
            return;
        }
        setClockSettingsStatus("正在读取 PCF85063 与闹钟状态…");
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("GET CLOCK", 8000);
                applyClockResponse(response);
            } catch (Exception e) {
                setClockSettingsStatus("读取失败：" + e.getMessage());
            }
        });
    }

    private String applyClockResponse(String response) throws IOException {
        String value = parseJsonString(response, "time");
        String source = parseJsonString(response, "source");
        String sync = parseJsonString(response, "sync");
        String alarm = parseJsonString(response, "alarm");
        boolean rtcValid = parseJsonBoolean(response, "rtc_valid", false);
        boolean alarmEnabled = parseJsonBoolean(response, "alarm_enabled", false);
        boolean ringing = parseJsonBoolean(response, "ringing", false);
        int style = parseJsonInt(response, "style", 0);
        if (value.isEmpty()) throw new IOException("设备返回的时钟状态不完整");
        runOnUiThread(() -> {
            if (value.length() >= 19) {
                customDateEdit.setText(value.substring(0, 10));
                customTimeEdit.setText(value.substring(11, 19));
            }
            alarmTimeEdit.setText(alarm);
            watchStyleSpinner.setSelection(Math.max(0, Math.min(1, style)));
            String styleText = style == 1 ? "KKD2 · 旋转人物表盘" : "KKD1 · 狂三双臂指针";
            String sourceText;
            switch (source) {
                case "custom": sourceText = "自定义时间"; break;
                case "ble": sourceText = "手机蓝牙校时"; break;
                case "wifi": sourceText = "Wi-Fi 校时"; break;
                default: sourceText = "RTC 原始时间"; break;
            }
            String syncText = "";
            if ("waiting_wifi".equals(sync)) syncText = "；等待 Wi-Fi";
            else if ("syncing".equals(sync)) syncText = "；网络校时中";
            else if ("failed".equals(sync)) syncText = "；网络校时失败";
            clockSettingsStatus.setText((rtcValid ? value : "RTC 尚未有效")
                    + " · " + sourceText + syncText + " · 闹钟 "
                    + (alarmEnabled ? alarm : "关闭") + (ringing ? "（响铃中）" : "")
                    + " · 当前 " + styleText);
        });
        return sync;
    }

    private void applyWatchStyle(int requestedStyle) {
        if (!settingsReady()) {
            setClockSettingsStatus("请先连接 PACON");
            return;
        }
        final int expected = Math.max(0, Math.min(1, requestedStyle));
        final String expectedName = expected == 1 ? "KKD2" : "KKD1";
        setClockSettingsStatus("正在切换到 " + expectedName + "…");
        mediaExecutor.execute(() -> {
            try {
                sendSettingCommandChecked("SET WATCH STYLE " + expected);
                Thread.sleep(350L);
                String response = sendCommandAndWait("GET CLOCK", 8000);
                int actual = parseJsonInt(response, "style", -1);
                applyClockResponse(response);
                if (actual != expected) {
                    setClockSettingsStatus("切换失败：请求 " + expectedName
                            + "，设备仍回报 " + (actual == 1 ? "KKD2" : "KKD1"));
                } else {
                    setClockSettingsStatus("已确认设备正在使用 " + expectedName);
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } catch (Exception e) {
                setClockSettingsStatus("表盘切换失败：" + e.getMessage());
            }
        });
    }

    private void setCustomClock() {
        String date = customDateEdit.getText().toString().trim();
        String time = customTimeEdit.getText().toString().trim();
        if (!date.matches("\\d{4}-\\d{2}-\\d{2}") ||
                !time.matches("\\d{2}:\\d{2}:\\d{2}")) {
            setClockSettingsStatus("格式应为 YYYY-MM-DD 和 HH:MM:SS");
            return;
        }
        sendClockCommand("SET TIME " + date + " " + time + " CUSTOM", "自定义时间已写入 RTC");
    }

    private void syncClockFromPhone() {
        String now = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())
                .format(new Date());
        sendClockCommand("SET TIME " + now + " BLE", "已用手机本地时间校准 RTC");
    }

    private void syncClockFromWifi() {
        if (!settingsReady()) {
            setClockSettingsStatus("请先连接 PACON");
            return;
        }
        setClockSettingsStatus("正在请求 Wi-Fi 网络时间…");
        mediaExecutor.execute(() -> {
            try {
                sendSettingCommandChecked("SYNC WIFI TIME");
                for (int attempt = 0; attempt < 20; attempt++) {
                    String response = sendCommandAndWait("GET CLOCK", 8000);
                    String state = applyClockResponse(response);
                    if ("ok".equals(state) || "failed".equals(state)) return;
                    Thread.sleep(1000L);
                }
                setClockSettingsStatus("网络校时仍未完成，请检查 PACON 的 Wi-Fi 连接");
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                setClockSettingsStatus("网络校时已取消");
            } catch (Exception e) {
                setClockSettingsStatus("网络校时失败：" + e.getMessage());
            }
        });
    }

    private void setAlarm() {
        String alarm = alarmTimeEdit.getText().toString().trim();
        if (!alarm.matches("(?:[01]\\d|2[0-3]):[0-5]\\d")) {
            setClockSettingsStatus("闹钟格式应为 HH:MM，例如 07:30");
            return;
        }
        sendClockCommand("SET ALARM " + alarm, "每日闹钟已设置为 " + alarm);
    }

    private void sendClockCommand(String command, String success) {
        if (!settingsReady()) {
            setClockSettingsStatus("请先连接 PACON");
            return;
        }
        setClockSettingsStatus("正在发送…");
        mediaExecutor.execute(() -> {
            try {
                sendSettingCommandChecked(command);
                setClockSettingsStatus(success);
                Thread.sleep(250L);
                applyClockResponse(sendCommandAndWait("GET CLOCK", 8000));
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } catch (Exception e) {
                setClockSettingsStatus("操作失败：" + e.getMessage());
            }
        });
    }

    private void refreshRadarSettings() {
        if (!settingsReady()) {
            setRadarSettingsStatus("请先连接 PACON");
            return;
        }
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("GET RADAR", 8000);
                applyRadarResponse(response);
            } catch (Exception e) {
                setRadarSettingsStatus("读取失败：" + e.getMessage());
            }
        });
    }

    private void refreshWifiProfiles() {
        if (!settingsReady()) {
            setWifiSettingsStatus("请先连接 PACON");
            return;
        }
        setWifiSettingsStatus("正在读取已保存网络…");
        mediaExecutor.execute(() -> {
            try {
                String listResponse = sendCommandAndWait("GET WIFI LIST", 8000);
                int count = Math.max(0, parseJsonInt(listResponse, "count", 0));
                int selected = parseJsonInt(listResponse, "selected", -1);
                ArrayList<WifiEntry> loaded = new ArrayList<>();
                for (int i = 0; i < count; i++) {
                    String itemResponse = sendCommandAndWait("GET WIFI " + i, 8000);
                    String ssid = parseJsonString(itemResponse, "ssid");
                    if (!ssid.isEmpty()) {
                        boolean isSelected = parseJsonBoolean(itemResponse, "selected", i == selected);
                        loaded.add(new WifiEntry(i, ssid, isSelected));
                    }
                }
                runOnUiThread(() -> {
                    wifiEntries.clear();
                    wifiEntries.addAll(loaded);
                    wifiAdapter.notifyDataSetChanged();
                    wifiSettingsStatus.setText(loaded.isEmpty()
                            ? "暂无已保存网络"
                            : "共 " + loaded.size() + " 个；点击连接，长按删除");
                });
            } catch (Exception e) {
                setWifiSettingsStatus("读取失败：" + e.getMessage());
            }
        });
    }

    private void saveWifiProfile() {
        if (!settingsReady()) {
            setWifiSettingsStatus("请先连接 PACON");
            return;
        }
        String ssid = wifiSsidEdit.getText().toString().trim();
        String password = wifiPasswordEdit.getText().toString();
        if (ssid.isEmpty()) {
            setWifiSettingsStatus("请输入 Wi-Fi 名称");
            return;
        }
        setWifiSettingsStatus("正在保存 " + ssid + "…");
        mediaExecutor.execute(() -> {
            try {
                sendSettingCommandChecked("SET WIFI " + ssid + "|" + password);
                runOnUiThread(() -> {
                    wifiSsidEdit.setText("");
                    wifiPasswordEdit.setText("");
                });
                setWifiSettingsStatus("已保存，正在刷新…");
                refreshWifiProfiles();
            } catch (Exception e) {
                setWifiSettingsStatus("保存失败：" + e.getMessage());
            }
        });
    }

    private void selectWifiProfile(WifiEntry entry) {
        if (!settingsReady()) {
            setWifiSettingsStatus("请先连接 PACON");
            return;
        }
        setWifiSettingsStatus("正在连接 " + entry.ssid + "…");
        mediaExecutor.execute(() -> {
            try {
                sendSettingCommandChecked("SELECT WIFI " + entry.index);
                setWifiSettingsStatus("已发起连接：" + entry.ssid);
                refreshWifiProfiles();
            } catch (Exception e) {
                setWifiSettingsStatus("连接失败：" + e.getMessage());
            }
        });
    }

    private void showDeleteWifiDialog(WifiEntry entry) {
        new AlertDialog.Builder(this)
                .setTitle("删除已保存网络？")
                .setMessage(entry.ssid)
                .setNegativeButton("取消", null)
                .setPositiveButton("删除", (dialog, which) -> {
                    setWifiSettingsStatus("正在删除 " + entry.ssid + "…");
                    mediaExecutor.execute(() -> {
                        try {
                            sendSettingCommandChecked("DELETE WIFI " + entry.index);
                            setWifiSettingsStatus("已删除，正在刷新…");
                            refreshWifiProfiles();
                        } catch (Exception e) {
                            setWifiSettingsStatus("删除失败：" + e.getMessage());
                        }
                    });
                })
                .show();
    }

    private void setDeviceBrightness(int percent) {
        if (!settingsReady()) {
            setSettingsStatus("请先连接 PACON");
            return;
        }
        setSettingsStatus("正在应用亮度…");
        mediaExecutor.execute(() -> sendSettingCommand("SET BRIGHTNESS " + percent, false));
    }

    private void setSkyOrbRange(int range) {
        if (!settingsReady()) {
            setRadarSettingsStatus("请先连接 PACON");
            return;
        }
        setRadarSettingsStatus("正在应用量程…");
        mediaExecutor.execute(() -> sendSettingCommand("SET RANGE " + range, true));
    }

    private void setAutoLocation() {
        if (!settingsReady()) {
            setRadarSettingsStatus("请先连接 PACON");
            return;
        }
        mediaExecutor.execute(this::pollAutoLocation);
    }

    private void pollAutoLocation() {
        try {
            sendSettingCommandChecked("SET AUTO_LOCATION");
            for (int attempt = 0; attempt < 22; attempt++) {
                String response = sendCommandAndWait("GET RADAR", 8000);
                String state = applyRadarResponse(response);
                if ("ready".equals(state) || "failed".equals(state) ||
                        "waiting_wifi".equals(state)) return;
                Thread.sleep(1000L);
            }
            setRadarSettingsStatus("自动定位仍未完成，请检查网络后重试");
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            setRadarSettingsStatus("自动定位已取消");
        } catch (Exception e) {
            setRadarSettingsStatus("自动定位失败：" + e.getMessage());
        }
    }

    private String applyRadarResponse(String response) throws IOException {
        int range = parseJsonInt(response, "range", -1);
        String lat = parseJsonNumber(response, "lat");
        String lon = parseJsonNumber(response, "lon");
        boolean locationValid = parseJsonBoolean(response, "location_valid", false);
        boolean locationAuto = parseJsonBoolean(response, "location_auto", false);
        String locationState = parseJsonString(response, "location_state");
        String locationError = parseJsonString(response, "location_error");
        if (range < 0 || (locationValid && (lat.isEmpty() || lon.isEmpty()))) {
            throw new IOException("设备返回的雷达设置不完整");
        }
        if (locationState.isEmpty()) locationState = locationValid ? "ready" : "unset";
        final String finalState = locationState;
        runOnUiThread(() -> {
            int rangeIndex = Math.max(0, Math.min(RADAR_RANGE_KM.length - 1, range));
            rangeSeek.setProgress(rangeIndex);
            rangeValue.setText(RADAR_RANGE_KM[rangeIndex] + " km");
            if (locationValid) {
                latitudeEdit.setText(lat);
                longitudeEdit.setText(lon);
                radarSettingsStatus.setText(locationAuto
                        ? "雷达设置已读取（IP 自动定位）"
                        : "雷达设置已读取（手动位置）");
                return;
            }
            latitudeEdit.setText("");
            longitudeEdit.setText("");
            switch (finalState) {
                case "requested":
                case "pending":
                    radarSettingsStatus.setText("正在通过公网 IP 自动定位…");
                    break;
                case "waiting_wifi":
                    radarSettingsStatus.setText("自动定位正在等待 Wi-Fi 联网");
                    break;
                case "failed":
                    radarSettingsStatus.setText("自动定位失败：" + locationErrorText(locationError));
                    break;
                default:
                    radarSettingsStatus.setText("位置尚未生成；联网后点自动定位");
                    break;
            }
        });
        return locationState;
    }

    private static String locationErrorText(String error) {
        switch (error == null ? "" : error) {
            case "client_init": return "网络客户端初始化失败";
            case "network_open": return "定位服务连接失败";
            case "network_read": return "定位服务没有返回数据";
            case "http_status": return "定位服务拒绝请求";
            case "invalid_response": return "定位服务响应无有效坐标";
            default: return "未知网络错误";
        }
    }

    private void saveSettings() {
        if (!settingsReady()) {
            setRadarSettingsStatus("请先连接 PACON");
            return;
        }
        String lat = latitudeEdit.getText().toString().trim();
        String lon = longitudeEdit.getText().toString().trim();
        mediaExecutor.execute(() -> {
            try {
                if (lat.isEmpty() && lon.isEmpty()) throw new IOException("请填写经纬度，或使用自动定位");
                if (lat.isEmpty() || lon.isEmpty()) throw new IOException("经纬度需同时填写");
                sendSettingCommandChecked("SET LOCATION " + lat + " " + lon);
                setRadarSettingsStatus("雷达设置已保存");
            } catch (Exception e) {
                setRadarSettingsStatus("保存失败：" + e.getMessage());
            }
        });
    }

    private void sendSettingCommand(String command, boolean radar) {
        try {
            sendSettingCommandChecked(command);
            if (radar) setRadarSettingsStatus("设置已应用");
            else setSettingsStatus("设置已应用");
        } catch (Exception e) {
            if (radar) setRadarSettingsStatus("设置失败：" + e.getMessage());
            else setSettingsStatus("设置失败：" + e.getMessage());
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

    private static boolean parseJsonBoolean(String response, String key, boolean fallback) {
        Matcher matcher = Pattern.compile("\\\"" + Pattern.quote(key)
                + "\\\"\\s*:\\s*(true|false)", Pattern.CASE_INSENSITIVE)
                .matcher(response == null ? "" : response);
        return matcher.find() ? Boolean.parseBoolean(matcher.group(1)) : fallback;
    }

    private void styleUi(LinearLayout root) {
        getWindow().setStatusBarColor(UI_BACKGROUND);
        getWindow().setNavigationBarColor(UI_BACKGROUND);
        getWindow().getDecorView().setSystemUiVisibility(0);
        styleNode(root);
    }

    private void styleNode(View view) {
        if (view instanceof Switch) {
            Switch toggle = (Switch)view;
            toggle.setThumbTintList(new ColorStateList(
                    new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
                    new int[]{UI_ACCENT, Color.rgb(146, 161, 176)}));
        } else if (view instanceof Button) {
            Button button = (Button) view;
            button.setAllCaps(false);
            button.setTextColor(Color.WHITE);
            button.setTextSize(14);
            button.setMinHeight(dp(48));
            button.setMinimumWidth(0);
            button.setMinWidth(0);
            button.setPadding(dp(10), dp(8), dp(10), dp(8));
            boolean primary = "primary".equals(button.getTag());
            button.setTextColor(primary ? UI_BACKGROUND : Color.rgb(230, 237, 243));
            button.setBackgroundTintList(null);
            button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x3384E1C3),
                    roundBackground(primary ? UI_ACCENT : Color.rgb(32, 43, 52), 14), null));
            if (button.getLayoutParams() instanceof LinearLayout.LayoutParams) {
                LinearLayout.LayoutParams params = (LinearLayout.LayoutParams)button.getLayoutParams();
                if (params.topMargin == 0 && params.bottomMargin == 0) {
                    params.topMargin = dp(5);
                    params.bottomMargin = dp(5);
                    button.setLayoutParams(params);
                }
            }
        } else if (view instanceof Spinner) {
            view.setBackgroundTintList(ColorStateList.valueOf(UI_ACCENT));
        } else if (view instanceof SeekBar) {
            ((SeekBar)view).setProgressTintList(ColorStateList.valueOf(UI_ACCENT));
            ((SeekBar)view).setThumbTintList(ColorStateList.valueOf(UI_ACCENT));
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
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, -2, 1.0f);
        params.setMargins(dp(3), dp(4), dp(3), dp(4));
        return params;
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
                deviceText.setText("PACON · " + device.getAddress());
                if (!bleConnecting && !bleConnected) {
                    updateConnectionUi("已发现，未连接", false, false);
                }
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
            /* Low-latency scanning must not continue while the same radio is
             * establishing and servicing GATT.  On the Xiaomi test phone it
             * starved acknowledged writes until the 5 s supervision timeout. */
            stopScan();
            disconnect();
            /* disconnect() intentionally raises the upload-cancel flag.  A
             * fresh connection must clear that stale flag or catalog control
             * commands would skip their wait and report a false timeout. */
            mediaCancelRequested = false;
            log("正在连接...");
            updateConnectionUi("正在连接…", true, false);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                gatt = selectedDevice.connectGatt(this, false, gattCallback,
                        android.bluetooth.BluetoothDevice.TRANSPORT_LE);
            } else {
                gatt = selectedDevice.connectGatt(this, false, gattCallback);
            }
        } catch (SecurityException e) {
            updateConnectionUi("连接失败", false, false);
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
        if (gatt != null) {
            try {
                gatt.disconnect();
                gatt.close();
            } catch (SecurityException ignored) {
                // The connection is already being torn down.
            }
        }
        gatt = null;
        commandCharacteristic = null;
        responseCharacteristic = null;
        mediaDataCharacteristic = null;
        if (uploadButton != null) uploadButton.setEnabled(false);
        if (mediaListButton != null) mediaListButton.setEnabled(false);
        if (cancelUploadButton != null) cancelUploadButton.setEnabled(false);
        if (retryUploadButton != null) retryUploadButton.setEnabled(false);
        updateConnectionUi("已断开", false, false);
        log("已断开");
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt callbackGatt, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                runOnUiThread(() -> {
                    updateConnectionUi("已连接，正在发现服务…", true, false);
                    log("已连接，正在发现服务");
                });
                try {
                    /* PACON's compact 2.4 GHz RF path has substantially less
                     * margin at BLE 2M on the target Xiaomi phone.  The phone
                     * otherwise upgrades to 2M automatically and the link
                     * reaches its five-second supervision timeout. */
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        callbackGatt.setPreferredPhy(BluetoothDevice.PHY_LE_1M_MASK,
                                BluetoothDevice.PHY_LE_1M_MASK,
                                BluetoothDevice.PHY_OPTION_NO_PREFERRED);
                    }
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
                runOnUiThread(() -> {
                    /* A manual reconnect closes the previous GATT before the
                     * new one is assigned.  Ignore that stale callback so it
                     * cannot overwrite the new connection's visible state. */
                    if (gatt != callbackGatt) return;
                    gatt = null;
                    commandCharacteristic = null;
                    responseCharacteristic = null;
                    mediaDataCharacteristic = null;
                    try {
                        callbackGatt.close();
                    } catch (SecurityException ignored) { }
                    updateConnectionUi("连接已断开", false, false);
                    log("连接断开，status=" + status);
                });
            }
        }

        @Override
        public void onPhyUpdate(BluetoothGatt callbackGatt, int txPhy, int rxPhy,
                                int status) {
            runOnUiThread(() -> log("BLE PHY tx=" + txPhy + " rx=" + rxPhy
                    + " status=" + status));
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
                runOnUiThread(() -> {
                    updateConnectionUi("服务发现失败", false, false);
                    log("服务发现失败，status=" + status);
                });
                return;
            }
            BluetoothGattService service = callbackGatt.getService(SERVICE_UUID);
            if (service == null) {
                runOnUiThread(() -> {
                    updateConnectionUi("服务不可用", false, false);
                    log("未找到 PACON 自定义服务");
                });
                return;
            }
            commandCharacteristic = service.getCharacteristic(COMMAND_UUID);
            responseCharacteristic = service.getCharacteristic(RESPONSE_UUID);
            mediaDataCharacteristic = service.getCharacteristic(MEDIA_DATA_UUID);
            if (commandCharacteristic == null || responseCharacteristic == null) {
                runOnUiThread(() -> {
                    updateConnectionUi("服务不完整", false, false);
                    log("未找到命令或响应特征");
                });
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
                        updateConnectionUi("已连接，可用", false, true);
                        uploadButton.setEnabled(true);
                        if (mediaListButton != null) mediaListButton.setEnabled(true);
                    });
                }
                runOnUiThread(() -> log(status == BluetoothGatt.GATT_SUCCESS
                        ? "响应通知已开启" : "响应通知开启失败，status=" + status));
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    if (AUTO_MEDIA_REFRESH_ON_CONNECT) {
                        /* Let the CCCD write settle before issuing the first
                         * acknowledged catalog command after reconnect. */
                        mainHandler.postDelayed(MainActivity.this::refreshMediaList,
                                AUTO_MEDIA_REFRESH_DELAY_MS);
                    } else {
                        runOnUiThread(() -> log(
                                "A/B 测试：已关闭连接后的自动命令，请先静置 15 秒"));
                    }
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
        /* Catalog refresh, settings, uploads and interactive buttons all share
         * Android's single acknowledged-GATT-write lane.  Queue PING and other
         * manual commands on the same single-thread executor instead of racing
         * an automatic media refresh from the UI thread. */
        mediaExecutor.execute(() -> {
            try {
                log(">> " + command);
                String response = sendCommandAndWait(command, 8000L);
                if (response.startsWith("ERR")) throw new IOException(response);
                if ("CAMERA SHUTTER".equals(command)) log("快门指令已发送，请在手机相机中确认");
            } catch (IOException e) {
                log("命令失败: " + e.getMessage());
            }
        });
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
            Uri uri = data.getData();
            String displayName = queryDisplayName(uri);
            if (isRgb565Name(displayName)) {
                startMediaUploadPrepared(uri, null, displayName);
            } else {
                prepareImageCrop(uri, displayName);
            }
        }
    }

    private void prepareImageCrop(Uri uri, String displayName) {
        if (mediaUploading) return;
        setUploadStatus("正在读取图片…");
        mediaExecutor.execute(() -> {
            try {
                Bitmap bitmap = decodePreviewBitmap(uri, 2400);
                runOnUiThread(() -> showCropDialog(uri, displayName, bitmap));
            } catch (Exception e) {
                setUploadStatus("图片读取失败");
                log("图片读取失败: " + e.getMessage());
            }
        });
    }

    private void showCropDialog(Uri uri, String displayName, Bitmap bitmap) {
        CropImageView cropView = new CropImageView(this, bitmap);
        cropView.setLayoutParams(new LinearLayout.LayoutParams(-1, dp(470)));
        TextView hint = label("单指拖动，双指缩放；椭圆框就是圆屏可见区域");
        hint.setPadding(dp(14), dp(8), dp(14), dp(8));
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setBackgroundColor(Color.rgb(10, 14, 24));
        content.addView(cropView, new LinearLayout.LayoutParams(-1, dp(470)));
        content.addView(hint, new LinearLayout.LayoutParams(-1, -2));
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("选择 PACON 图片裁剪位置")
                .setView(content)
                .setNegativeButton("取消", null)
                .setPositiveButton("转换并上传", null)
                .create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE)
                .setOnClickListener(v -> {
                    try {
                        Bitmap cropped = cropView.createCroppedBitmap(LCD_WIDTH, LCD_HEIGHT);
                        byte[] converted;
                        try {
                            converted = convertBitmapToRgb565(cropped);
                        } finally {
                            cropped.recycle();
                        }
                        dialog.dismiss();
                        startMediaUploadPrepared(uri, converted, displayName);
                    } catch (Exception e) {
                        setUploadStatus("图片转换失败");
                        log("图片转换失败: " + e.getMessage());
                    }
                }));
        dialog.setOnDismissListener(ignored -> bitmap.recycle());
        dialog.show();
    }

    private Bitmap decodePreviewBitmap(Uri uri, int maxDimension) throws IOException {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("无法打开图片");
            BitmapFactory.decodeStream(input, null, bounds);
        }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
            throw new IOException("无法识别图片格式");
        }
        int sample = 1;
        while (Math.max(bounds.outWidth / sample, bounds.outHeight / sample) > maxDimension) {
            sample *= 2;
        }
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = sample;
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("无法重新打开图片");
            Bitmap decoded = BitmapFactory.decodeStream(input, null, options);
            if (decoded == null) throw new IOException("无法解码图片");
            return applyExifOrientation(uri, decoded);
        }
    }

    private Bitmap applyExifOrientation(Uri uri, Bitmap decoded) {
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) return decoded;
            int orientation = new ExifInterface(input).getAttributeInt(
                    ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL);
            Matrix matrix = new Matrix();
            switch (orientation) {
                case ExifInterface.ORIENTATION_FLIP_HORIZONTAL: matrix.setScale(-1, 1); break;
                case ExifInterface.ORIENTATION_ROTATE_180: matrix.setRotate(180); break;
                case ExifInterface.ORIENTATION_FLIP_VERTICAL: matrix.setScale(1, -1); break;
                case ExifInterface.ORIENTATION_TRANSPOSE:
                    matrix.setRotate(90); matrix.postScale(-1, 1); break;
                case ExifInterface.ORIENTATION_ROTATE_90: matrix.setRotate(90); break;
                case ExifInterface.ORIENTATION_TRANSVERSE:
                    matrix.setRotate(-90); matrix.postScale(-1, 1); break;
                case ExifInterface.ORIENTATION_ROTATE_270: matrix.setRotate(-90); break;
                default: return decoded;
            }
            Bitmap oriented = Bitmap.createBitmap(decoded, 0, 0,
                    decoded.getWidth(), decoded.getHeight(), matrix, true);
            if (oriented != decoded) decoded.recycle();
            return oriented;
        } catch (Exception ignored) {
            return decoded;
        }
    }

    private void startMediaUploadPrepared(Uri uri, byte[] prepared, String displayName) {
        if (mediaUploading || screenSwitching) {
            noticeText.setText("请等待当前操作完成");
            return;
        }
        lastMediaUri = uri;
        lastPreparedMedia = prepared;
        lastPreparedDisplayName = displayName;
        mediaUploading = true;
        updateModeControls();
        mediaCancelRequested = false;
        if (uploadButton != null) uploadButton.setEnabled(false);
        if (mediaListButton != null) mediaListButton.setEnabled(false);
        if (cancelUploadButton != null) cancelUploadButton.setEnabled(true);
        if (retryUploadButton != null) retryUploadButton.setEnabled(false);
        setUploadProgress(0, "正在准备素材…");
        mediaExecutor.execute(() -> uploadMediaFile(uri, prepared, displayName));
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
                log("素材已刷新，共 " + entries.size() + " 项；长按可删除");
            } catch (Exception e) {
                log("素材读取失败：" + e.getMessage());
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
                log("正在播放：" + name);
            } catch (Exception e) {
                log("播放失败：" + e.getMessage());
            }
        });
    }

    private void deleteMedia(String name) {
        if (mediaUploading || gatt == null || commandCharacteristic == null) return;
        if (mediaListButton != null) mediaListButton.setEnabled(false);
        mediaExecutor.execute(() -> {
            try {
                String response = sendCommandAndWait("MEDIA_DELETE " + name, 5000);
                String expectedDeleteResponse = "OK MEDIA_DELETED " + name;
                if (!expectedDeleteResponse.equals(response)) {
                    throw new IOException("删除确认与所选文件不一致: " + response);
                }
                log("已删除素材：" + name);
                refreshMediaList();
            } catch (Exception e) {
                log("删除失败：" + e.getMessage());
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

    private void uploadMediaFile(Uri uri, byte[] prepared, String preparedDisplayName) {
        boolean completed = false;
        try {
            requestUploadConnectionPriority(true);
            setUploadProgress(0, "正在读取素材…");
            String displayName = preparedDisplayName == null
                    ? queryDisplayName(uri) : preparedDisplayName;
            byte[] media = prepared == null ? readMediaBytes(uri, displayName) : prepared;
            if (media.length == 0 || media.length % LCD_FRAME_BYTES != 0) {
                throw new IOException("文件大小必须是单帧 " + LCD_FRAME_BYTES
                        + " 字节的整数倍，且文件应为原始 RGB565");
            }
            int frames = media.length / LCD_FRAME_BYTES;
            int fps = inferFps(displayName);
            String remoteName = mediaRemoteName(displayName);
            setUploadProgress(0, "正在上传 " + remoteName);
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
                    setUploadProgress(percent, "正在上传");
                    log("媒体上传进度 " + percent + "%");
                }
            }
            }
            String end = sendCommandAndWait("MEDIA_END", 8000);
            requireResponse(end, "OK MEDIA_COMMITTED");
            completed = true;
            setUploadProgress(100, "上传完成");
            log("媒体上传完成，设备将重新扫描媒体目录");
            /* The firmware commits the file first and schedules its display
             * reader rescan on the main task.  Refresh after a short settle
             * period so the new file appears without requiring another tap. */
            mainHandler.postDelayed(this::refreshMediaList, 800L);
        } catch (Exception e) {
            setUploadStatus(mediaCancelRequested ? "上传已取消" : "上传失败，请重试");
            log("媒体上传失败: " + e.getMessage());
            boolean canceled = mediaCancelRequested;
            mediaCancelRequested = false;
            sendCommandAndWaitQuietly("MEDIA_ABORT");
            mediaCancelRequested = canceled;
        } finally {
            requestUploadConnectionPriority(false);
            mediaUploading = false;
            runOnUiThread(this::updateModeControls);
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
                setUploadProgress(percent, "正在上传");
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
        // Keep diagnostics bounded even when their panel is hidden. Never
        // expose a Wi-Fi password in the shareable log.
        final String safeMessage = message.replaceAll("(?i)(SET WIFI\\s+).*", "$1[已隐藏]");
        if (noticeText != null && !message.startsWith(">>") && !message.startsWith("<<")
                && !message.startsWith("BLE ") && !message.startsWith("设置:")
                && !message.startsWith("命令写入完成")) {
            noticeText.setText(safeMessage);
        }
        if (logText.length() > 24000) logText.setText(logText.getText().subSequence(logText.length() - 16000, logText.length()));
        final boolean stickLogToBottom;
        if (logScroll != null && logScroll.getChildCount() > 0) {
            int oldMaxScroll = Math.max(0,
                    logScroll.getChildAt(0).getHeight() - logScroll.getHeight());
            stickLogToBottom = logScroll.getScrollY() >= oldMaxScroll - dp(24);
        } else {
            stickLogToBottom = false;
        }
        String time = new SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(new Date());
        logText.append(time + "  " + safeMessage + "\n");
        if (logScroll != null && stickLogToBottom && debugPanel.getVisibility() == View.VISIBLE) {
            logScroll.post(() -> {
                if (logScroll.getChildCount() == 0) return;
                int maxScroll = Math.max(0,
                        logScroll.getChildAt(0).getHeight() - logScroll.getHeight());
                logScroll.scrollTo(0, maxScroll);
            });
        }
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
