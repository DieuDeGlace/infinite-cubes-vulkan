package org.infinitecubes.vulkan;

import android.content.ContentResolver;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.TreeMap;

final class GameDataFolder {
    private static final String OPTIONS_FILE = "options.cfg";
    private static final String[] COLUMNS = {
        Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE,
        Document.COLUMN_LAST_MODIFIED
    };

    private final ContentResolver resolver;
    private final Uri tree;
    private final String rootId;
    private final boolean writable;
    private final Map<String, Map<String, Entry>> directories = new HashMap<>();

    GameDataFolder(ContentResolver resolver, Uri tree) {
        this.resolver = resolver;
        this.tree = tree;
        this.rootId = DocumentsContract.getTreeDocumentId(tree);
        this.writable = (persistedModes(resolver, tree) & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0;
    }

    static int persistedModes(ContentResolver resolver, Uri folder) {
        for (UriPermission grant : resolver.getPersistedUriPermissions()) {
            if (grant.getUri().equals(folder)) {
                int modes = 0;
                if (grant.isReadPermission())
                    modes |= Intent.FLAG_GRANT_READ_URI_PERMISSION;
                if (grant.isWritePermission())
                    modes |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
                return modes;
            }
        }
        return 0;
    }

    boolean canWriteOptions() {
        return writable;
    }

    synchronized boolean saveOptions(String contents) {
        if (!writable)
            return false;
        try {
            // Settings might have been replaced by a file manager while the app was paused.
            directories.remove(rootId);
            Entry entry = children(rootId).get(OPTIONS_FILE);
            if (entry != null && entry.directory)
                return false;
            Uri document;
            if (entry == null) {
                Uri parent = DocumentsContract.buildDocumentUriUsingTree(tree, rootId);
                document = DocumentsContract.createDocument(resolver, parent, "application/octet-stream", OPTIONS_FILE);
                directories.remove(rootId);
            } else {
                document = DocumentsContract.buildDocumentUriUsingTree(tree, entry.id);
            }
            if (document == null)
                return false;
            try (OutputStream file = resolver.openOutputStream(document, "wt")) {
                if (file == null)
                    return false;
                file.write(contents.getBytes(StandardCharsets.UTF_8));
            }
            return true;
        } catch (IOException | SecurityException | IllegalArgumentException error) {
            return false;
        }
    }

    // Native code owns the descriptor after detachFd().
    int openFile(String path) {
        try {
            Uri document = findFile(path);
            if (document == null)
                return -1;
            try (ParcelFileDescriptor file = resolver.openFileDescriptor(document, "r")) {
                return file == null ? -1 : file.detachFd();
            }
        } catch (IOException | SecurityException | IllegalArgumentException error) {
            return -1;
        }
    }

    // The tree and each file's name, size and modification time in one directory, re-read from the provider. Native
    // code fingerprints it to tell whether a packed copy of the directory is still current. Null if it is missing.
    synchronized String listing(String path) {
        String directoryId = rootId;
        for (String name : path.split("/")) {
            if (name.isEmpty() || name.equals("."))
                continue;
            Entry entry = name.equals("..") ? null : children(directoryId).get(name);
            if (entry == null || !entry.directory)
                return null;
            directoryId = entry.id;
        }
        directories.remove(directoryId);
        StringBuilder text = new StringBuilder(tree.toString()).append('\n');
        for (Map.Entry<String, Entry> child : children(directoryId).entrySet()) {
            Entry entry = child.getValue();
            text.append(child.getKey()).append('\t').append(entry.size).append('\t').append(entry.modified).append('\n');
        }
        return text.toString();
    }

    private synchronized Uri findFile(String path) {
        String parentId = rootId;
        String[] parts = path.split("/", -1);
        for (int i = 0; i < parts.length; ++i) {
            String name = parts[i];
            if (name.equals("."))
                continue;
            if (name.isEmpty() || name.equals(".."))
                return null;

            Entry entry = children(parentId).get(name);
            if (entry == null)
                return null;
            if (i == parts.length - 1)
                return entry.directory ? null : DocumentsContract.buildDocumentUriUsingTree(tree, entry.id);
            if (!entry.directory)
                return null;
            parentId = entry.id;
        }
        return null;
    }

    // Index each directory once, using the same case-insensitive names as the Windows game.
    private Map<String, Entry> children(String parentId) {
        Map<String, Entry> cached = directories.get(parentId);
        if (cached != null)
            return cached;

        Map<String, Entry> children = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
        Uri query = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        try (Cursor rows = resolver.query(query, COLUMNS, null, null, null)) {
            if (rows == null)
                return children;
            while (rows.moveToNext()) {
                String id = rows.getString(0);
                String name = rows.getString(1);
                if (id != null && name != null)
                    children.put(name, new Entry(id, Document.MIME_TYPE_DIR.equals(rows.getString(2)),
                                                 rows.isNull(3) ? -1 : rows.getLong(3),
                                                 rows.isNull(4) ? -1 : rows.getLong(4)));
            }
        }
        directories.put(parentId, children);
        return children;
    }

    private static final class Entry {
        final String id;
        final boolean directory;
        final long size, modified;

        Entry(String id, boolean directory, long size, long modified) {
            this.id = id;
            this.directory = directory;
            this.size = size;
            this.modified = modified;
        }
    }
}
