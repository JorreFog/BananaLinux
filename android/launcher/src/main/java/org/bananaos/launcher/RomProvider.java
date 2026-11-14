package org.bananaos.launcher;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * The ROMs as content:// URIs (launch.conf's data = content, %ROMURI%), for emulators that don't read the card's
 * paths themselves: content://org.bananaos.launcher.roms/<the file's absolute path>. Not exported: an emulator reads a
 * game only through the permission its launch intent grants it, and only files under the games' folder are served.
 */
public class RomProvider extends ContentProvider {
    static final String AUTHORITY = "org.bananaos.launcher.roms";

    static Uri uriFor(Context c, String path) {
        return new Uri.Builder().scheme("content").authority(AUTHORITY).encodedPath(Uri.encode(path, "/")).build();
    }

    private static File fileOf(Uri u) throws FileNotFoundException {
        File f = new File(u.getPath());
        try {
            String canon = f.getCanonicalPath();
            String roms = Core.roms != null ? Core.roms.getCanonicalPath() : null;
            if (roms == null || !canon.startsWith(roms + "/")) throw new FileNotFoundException("not a game: " + u);
        } catch (java.io.IOException e) { throw new FileNotFoundException(e.getMessage()); }
        return f;
    }

    @Override public boolean onCreate() { return true; }

    @Override
    public ParcelFileDescriptor openFile(Uri u, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(fileOf(u), ParcelFileDescriptor.parseMode(mode));
    }

    @Override
    public Cursor query(Uri u, String[] proj, String sel, String[] args, String sort) {
        File f;
        try { f = fileOf(u); } catch (FileNotFoundException e) { return null; }
        MatrixCursor c = new MatrixCursor(new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE });
        c.addRow(new Object[] { f.getName(), f.length() });
        return c;
    }

    @Override public String getType(Uri u) { return "application/octet-stream"; }
    @Override public Uri insert(Uri u, ContentValues v) { return null; }
    @Override public int delete(Uri u, String s, String[] a) { return 0; }
    @Override public int update(Uri u, ContentValues v, String s, String[] a) { return 0; }
}
