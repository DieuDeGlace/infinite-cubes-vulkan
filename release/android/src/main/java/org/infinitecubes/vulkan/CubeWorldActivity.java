package org.infinitecubes.vulkan;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

public final class CubeWorldActivity extends NativeActivity {
    private static final int PICK_GAME_FOLDER = 1;
    private static final int UPGRADE_GAME_FOLDER_GRANT = 2;
    private static final int READ_FOLDER = Intent.FLAG_GRANT_READ_URI_PERMISSION;
    private static final int FOLDER_MODES = READ_FOLDER | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
    private static final String OFFERED_OPTIONS_GRANT = "offered_options_grant";
    private static final String GAME_FOLDER = "game_data_folder";
    private static final String FOLDER_HELP = "Choose the folder in Documents that contains resource1.dat and sprites.";
    private static final String WAITING_FOR_WINDOW = "Waiting for the game window";

    private volatile GameDataFolder gameDataFolder;
    private final AtomicBoolean folderSelectionChanged = new AtomicBoolean();
    private AlertDialog loadingDialog;
    private String startupMessage = WAITING_FOR_WINDOW;
    private String folderMessage = FOLDER_HELP;

    @Override
    protected void onCreate(Bundle state) {
        if (state == null) {
            Context application = getApplicationContext();
            new Thread(() -> CrashLog.collect(application), "Cube World crash log").start();
        }
        SharedPreferences settings = getPreferences(MODE_PRIVATE);
        String savedFolder = settings.getString(GAME_FOLDER, null);
        if (savedFolder != null) {
            Uri folder = Uri.parse(savedFolder);
            if (DocumentsContract.isTreeUri(folder))
                gameDataFolder = new GameDataFolder(getContentResolver(), folder);
        }
        super.onCreate(state);
        if (gameDataFolder != null && !gameDataFolder.canWriteOptions() &&
                !settings.getBoolean(OFFERED_OPTIONS_GRANT, false)) {
            settings.edit().putBoolean(OFFERED_OPTIONS_GRANT, true).apply();
            chooseGameFolder(UPGRADE_GAME_FOLDER_GRANT, Uri.parse(savedFolder));
        }
    }

    @Override
    protected void onDestroy() {
        dismissLoadingDialog();
        super.onDestroy();
    }

    // Called from the native game thread. Saves and settings use the app's own directory.
    public boolean hasGameDataFolder() {
        return gameDataFolder != null;
    }

    public int openGameDataFile(String path) {
        GameDataFolder folder = gameDataFolder;
        return folder == null ? -1 : folder.openFile(path);
    }

    public String gameDataListing(String path) {
        GameDataFolder folder = gameDataFolder;
        return folder == null ? null : folder.listing(path);
    }

    public boolean canWriteOptions() {
        GameDataFolder folder = gameDataFolder;
        return folder != null && folder.canWriteOptions();
    }

    public boolean saveOptions(String contents) {
        GameDataFolder folder = gameDataFolder;
        return folder != null && folder.saveOptions(contents);
    }

    // Android delivers the folder result before resuming the native activity.
    public boolean takeGameDataFolderChange() {
        return folderSelectionChanged.getAndSet(false);
    }

    // A null message means the first frame was presented.
    public void showStartupMessage(String message, boolean failed, boolean chooseFolder) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed())
                return;
            if (failed)
                showGameError(message, chooseFolder);
            else if (message == null)
                dismissLoadingDialog();
            else
                showLoadingDialog(message);
        });
    }

    private void showLoadingDialog(String message) {
        startupMessage = message;
        if (loadingDialog != null) {
            loadingDialog.setMessage(message);
            return;
        }
        loadingDialog = showLogDialog(new AlertDialog.Builder(this)
                .setTitle("Starting Cube World")
                .setMessage(message)
                .setCancelable(false)
                .setNegativeButton("Close", (dismissed, which) -> finish()));
    }

    private void dismissLoadingDialog() {
        if (loadingDialog != null) {
            loadingDialog.dismiss();
            loadingDialog = null;
        }
    }

    private void showGameError(String message, boolean chooseFolder) {
        dismissLoadingDialog();
        startupMessage = message;
        AlertDialog.Builder dialog = new AlertDialog.Builder(this)
                .setTitle("Cube World could not continue")
                .setMessage(message)
                .setOnCancelListener(dismissed -> finish());
        if (chooseFolder) {
            folderMessage = message;
            Uri documents = DocumentsContract.buildDocumentUri("com.android.externalstorage.documents",
                    "primary:Documents");
            dialog.setPositiveButton("Choose folder", (dismissed, which) -> chooseGameFolder(PICK_GAME_FOLDER, documents))
                    .setNegativeButton("Close", (dismissed, which) -> finish());
        } else {
            dialog.setPositiveButton(android.R.string.ok, (dismissed, which) -> finish());
        }
        showLogDialog(dialog);
    }

    private AlertDialog showLogDialog(AlertDialog.Builder builder) {
        AlertDialog dialog = builder.setNeutralButton("Copy log", null).show();
        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(button -> copyStartupLog());
        return dialog;
    }

    private void copyStartupLog() {
        String text = startupMessage;
        File files = getExternalFilesDir(null);
        if (files != null) {
            try (FileInputStream log = new FileInputStream(new File(files, "log_startup.txt"))) {
                byte[] data = log.readNBytes(65536);
                if (data.length != 0)
                    text += "\n\n" + new String(data, StandardCharsets.UTF_8);
            } catch (IOException | SecurityException ignored) {
                // The current message is available even before the native log is open.
            }
        }
        getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("Cube World startup log", text));
    }

    private void chooseGameFolder(int request, Uri startFolder) {
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                .addFlags(FOLDER_MODES | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        picker.putExtra(DocumentsContract.EXTRA_INITIAL_URI, startFolder);
        try {
            startActivityForResult(picker, request);
        } catch (ActivityNotFoundException error) {
            if (request == PICK_GAME_FOLDER)
                showGameError("Android could not open the folder picker.", false);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_GAME_FOLDER && request != UPGRADE_GAME_FOLDER_GRANT)
            return;
        Uri folder = data == null ? null : data.getData();
        if (result != RESULT_OK || folder == null || !DocumentsContract.isTreeUri(folder)) {
            if (request == PICK_GAME_FOLDER)
                showGameError(folderMessage, true);
            return;
        }

        try {
            int modes = data.getFlags() & FOLDER_MODES;
            getContentResolver().takePersistableUriPermission(folder, modes | READ_FOLDER);
        } catch (SecurityException error) {
            if (request == PICK_GAME_FOLDER)
                showGameError("Android could not keep access to this folder. " + FOLDER_HELP, true);
            return;
        }

        SharedPreferences settings = getPreferences(MODE_PRIVATE);
        String previous = settings.getString(GAME_FOLDER, null);
        settings.edit().putString(GAME_FOLDER, folder.toString())
                .putBoolean(OFFERED_OPTIONS_GRANT, true).apply();
        if (previous != null && !previous.equals(folder.toString())) {
            Uri oldFolder = Uri.parse(previous);
            int oldModes = GameDataFolder.persistedModes(getContentResolver(), oldFolder);
            if (oldModes != 0) {
                try {
                    getContentResolver().releasePersistableUriPermission(oldFolder, oldModes);
                } catch (SecurityException ignored) {
                    // The previous grant may already have been revoked.
                }
            }
        }
        gameDataFolder = new GameDataFolder(getContentResolver(), folder);
        folderSelectionChanged.set(true);
        showLoadingDialog(WAITING_FOR_WINDOW);
    }
}
